#pragma once

#include <memory>
#include <string>
#include <vector>

class CommandList;
class CommandListCommand;
class CommandListState;
class InputListener;

namespace MigotoPort {

void           SetIniRoot(const wchar_t* dir);
const wchar_t* IniRoot(); // nullptr when unset

void MigotoDir(wchar_t* out, unsigned long count);

std::shared_ptr<InputListener> GateKeyListener(std::shared_ptr<InputListener> inner, const std::wstring& ns,
    const std::wstring& section);

void SetKeyGate(bool (*fn)(const wchar_t* ns, const wchar_t* section));

size_t OverrideNamespaceCount();

void                WrapObservedCommands(std::vector<CommandList*>& lists);
void                WrapObservedList(CommandList* list);
CommandListCommand* UnwrapObserved(CommandListCommand* cmd);

typedef void (*CommandObserver)(const wchar_t* ini_line, void* user);
void SetCommandObserver(CommandObserver fn, void* user);
void ObserveCommand(const wchar_t* ini_line);
// The ini line of the command this thread is running, or null between commands (COPY-WHAT names its copy).
const wchar_t* CurrentIniLine();

bool ObserverActive();
bool AnyObserver();

unsigned SelectionInputsGen();
void     BumpSelectionInputs();
void     WatchSelectionVar(const float* v);
bool     SelectionVarWatched(const float* v);
unsigned SelectionVarsGen();
void     SweepSelectionVars();
void     WatchSelectionRes(const void* cr);
void     ClearSelectionVars();

std::vector<CommandListCommand*>* SetCommandTrace(std::vector<CommandListCommand*>* trace);
// `run = CommandList...`: the hook runs the target list itself and returns true, or returns false to leave it to theirs.
void SetRunExplicitHook(bool (*fn)(CommandListCommand* inner, ::CommandListState* state));

struct ObservedCmd {
    enum class Kind { Other,
        RunList,
        ResourceCopy };
    Kind           kind = Kind::Other;
    bool           post = false;
    const wchar_t* run_target = nullptr;   // linked list: ini_section, directly available
    const void*    run_explicit = nullptr; // explicit list: ExplicitCommandListSection*, name resolved upstream
    int            dst_type = 0;
    wchar_t        dst_shader = 0;
    unsigned       dst_slot = 0;
    const void*    dst_custom = nullptr;
    int            src_type = 0;
    const void*    src_custom = nullptr;
};

typedef void (*TypedCommandObserver)(const ObservedCmd& cmd, void* user);
void SetTypedCommandObserver(TypedCommandObserver fn, void* user);

void ShimUnmet(const wchar_t* what, const wchar_t* detail);
// Per-frame total, for the heartbeat. Reading it clears it.
unsigned ShimUnmetTakeCount();

} // namespace MigotoPort

bool PortSectionFolder(const wchar_t* section, std::wstring* ret);
bool PortIniNamespace(const wchar_t* abs_ini_path, std::wstring* ret);
void PortForEachIniFile(void (*cb)(const std::wstring& rel_path, const std::wstring& ns, void* user), void* user);
