// ===========================================================================
//  Skeleton module implementation. See skeleton.h.
// ===========================================================================
#include "skeleton.h"
#include "../../src/log.h"
#include "../../src/d3d12_buffer.h"
#include <cmath>
#include <cstring>
#include <shared_mutex> // g_cb_maps_mtx

using namespace reshade::api;

// One-time shared setup after all geometry mods are loaded: identity bones + shared identity CB.
void FinalizeMergedRemap()
{
    if (g_merged_loaded || !g_d3d12_device) return;
    // A reload calls this again with mods rebuilt; the identity CB is the device's, not a mod's, so it is
    // made once. Re-making it would release a buffer the GPU may still be reading.
    if (!g_identity_cb && SUCCEEDED(CreateD3D12Buffer(g_d3d12_device, D3D12_HEAP_TYPE_UPLOAD, kBoneCBBytes,
                                           D3D12_RESOURCE_STATE_GENERIC_READ, &g_identity_cb))) {
        D3D12_RANGE nr = { 0, 0 };
        void*       m = nullptr;
        if (FAILED(g_identity_cb->Map(0, &nr, &m)) || !m) {
            g_identity_cb.Reset();
            return;
        }
        memset(m, 0, kBoneCBBytes);
        float* f = (float*)m;
        for (uint32_t b = 0; b < kMaxBones; ++b) {
            f[b * 12 + 0] = 1.f;
            f[b * 12 + 5] = 1.f;
            f[b * 12 + 10] = 1.f;
        }
        g_identity_cb_va = g_identity_cb->GetGPUVirtualAddress();
    }
    g_merged_loaded = true;
    logger::write("MergedRemap: finalized %zu total components across all mods", g_merged_comps.size());
}

bool LooksLikeBone(const float* m)
{
    float r0[3] = { m[0], m[1], m[2] }; // rotation row 0 (skip translation m[3])
    float r1[3] = { m[4], m[5], m[6] }; // rotation row 1
    float n0 = sqrtf(r0[0] * r0[0] + r0[1] * r0[1] + r0[2] * r0[2]);
    float n1 = sqrtf(r1[0] * r1[0] + r1[1] * r1[1] + r1[2] * r1[2]);
    if (n0 < 0.5f || n0 > 2.0f || n1 < 0.5f || n1 > 2.0f) return false;
    float dot = (r0[0] * r1[0] + r0[1] * r1[1] + r0[2] * r1[2]) / (n0 * n1);
    return (dot > -0.25f && dot < 0.25f); // rows ~orthogonal -> a rotation
}

struct GameCBMap {
    uint8_t* mapped = nullptr; // nullptr = looked at and not mappable (not an UPLOAD heap, or Map failed)
    uint64_t base = 0;
    uint64_t width = 0;
};
static std::unordered_map<ID3D12Resource*, GameCBMap> g_cb_maps;
static std::shared_mutex                              g_cb_maps_mtx;
static std::atomic<uint64_t>                          g_cb_maps_forgotten{ 0 }; // debug: entries erased by ForgetGameCB

static uint8_t* GameCBAt(ID3D12Resource* res, uint64_t va, bool map_on_demand, uint64_t* bytes_left)
{
    if (!res || !va) return nullptr;
    GameCBMap e;
    bool      have = false;
    {
        std::shared_lock l(g_cb_maps_mtx);
        auto             it = g_cb_maps.find(res);
        if (it != g_cb_maps.end()) {
            e = it->second;
            have = true;
        }
    }
    if (!have) {
        if (!map_on_demand) return nullptr;
        D3D12_HEAP_PROPERTIES hp = {};
        D3D12_HEAP_FLAGS      hf = {};
        res->GetHeapProperties(&hp, &hf);
        if (hp.Type == D3D12_HEAP_TYPE_UPLOAD) {
            D3D12_RANGE rr = { 0, 0 };
            void*       m = nullptr;
            if (SUCCEEDED(res->Map(0, &rr, &m)) && m) {
                e.mapped = (uint8_t*)m;
                e.base = res->GetGPUVirtualAddress();
                e.width = res->GetDesc().Width;
            }
        }
        std::unique_lock l(g_cb_maps_mtx);
        auto             ins = g_cb_maps.try_emplace(res, e);
        if (!ins.second) {
            if (e.mapped) {
                D3D12_RANGE none = { 0, 0 };
                res->Unmap(0, &none);
            }
            e = ins.first->second;
        }
    }
    if (!e.mapped) return nullptr;
    if (va < e.base) return nullptr;
    const uint64_t off = va - e.base;
    if (off >= e.width) return nullptr;
    *bytes_left = e.width - off;
    return e.mapped + off;
}

void ForgetGameCB(ID3D12Resource* res)
{
    if (!res) return;
    size_t erased;
    {
        std::unique_lock l(g_cb_maps_mtx);
        erased = g_cb_maps.erase(res);
    }
    if (erased && g_debug) {
        const uint64_t n = g_cb_maps_forgotten.fetch_add(1, std::memory_order_relaxed) + 1;
        if (n <= 4 || (n % 256) == 0)
            logger::write("CBMAP-FORGET #%llu: destroyed resource %p had a cached CB mapping -- erased, so a new "
                          "resource at this address maps afresh instead of reading the dead one",
                (unsigned long long)n, (void*)res);
    }
}

uint32_t SnapshotGameCB(ID3D12Resource* res, uint64_t va, uint8_t* dst, uint32_t max_bytes, bool map_on_demand)
{
    if (!dst || !max_bytes) return 0;
    uint64_t       left = 0;
    const uint8_t* at = GameCBAt(res, va, map_on_demand, &left);
    if (!at) return 0;
    const uint32_t n = (left < max_bytes) ? (uint32_t)left : max_bytes;
    memcpy(dst, at, n);
    return n;
}

bool ReadRootCbv(command_list* cmd_list, uint32_t param, void* dst, uint32_t n)
{
    const RootCbvBind& cb = CmdCbv(cmd_list).binds[param];
    uint8_t            raw[64];
    if (n > sizeof raw || !cb.res || SnapshotGameCB(cb.res, cb.va, raw, n, true) != n) return false;
    memcpy(dst, raw, n);
    return true;
}

// Diagnostic: bitmask of root params (0..15) whose bound CBV first matrix LooksLikeBone.
uint32_t BoneRootMask(command_list* cmd_list)
{
    RootCbvBind local[16] = {};
    {
        const CmdCbvBinds& cb = CmdCbv(cmd_list);
        for (int i = 0; i < 16; ++i) local[i] = cb.binds[i];
    }
    uint32_t mask = 0;
    for (int rp = 0; rp < 16; ++rp) {
        float m[12]; // one matrix, copied out of the mapping (SnapshotGameCB)
        if (SnapshotGameCB(local[rp].res, local[rp].va, (uint8_t*)m, sizeof(m)) != sizeof(m)) continue;
        if (LooksLikeBone(m)) mask |= (1u << rp);
    }
    return mask;
}
