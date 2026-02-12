# IPC Synchronization Plan: Making IPC the Source of Truth

## Executive Summary

After investigating the IPC architecture, I found that **the C++ API layer IS properly connected to the file-loaded document**. The `API_HANDLER_SCH` uses `m_frame->GetScreenForApi()` which returns the actual `SCH_SCREEN` containing all items from the loaded file. The issues reported (empty results, non-persisting changes) are likely caused by factors other than a fundamental disconnect between IPC and file content.

## Architecture Overview

### Data Flow

```
Agent Tool → run_shell sch <code> → Terminal Frame
                                      ↓
                                kipy.KiCad(socket_path)
                                      ↓
                                kicad.get_schematic()  → GetOpenDocuments API
                                      ↓
                                Schematic object (with DocumentSpecifier)
                                      ↓
                                sch.symbols.get_all()  → GetItems API
                                      ↓
                           API_HANDLER_SCH::handleGetItems()
                                      ↓
                           m_frame->GetScreenForApi()
                                      ↓
                           screen->Items()  ← ACTUAL DOCUMENT ITEMS
```

### Key Files

| File | Purpose |
|------|---------|
| `terminal/terminal_frame.cpp:577-688` | Handles `run_shell sch` commands, initializes kipy |
| `eeschema/api/api_handler_sch.cpp:684` | Uses `GetScreenForApi()` to access document |
| `eeschema/sch_edit_frame.cpp:1098-1119` | `GetScreenForApi()` returns actual document screen |
| `kipy/kicad.py:231-246` | `get_schematic()` queries open documents |
| `kipy/schematic/base.py` | Schematic class with lazy-loaded operation modules |

### Why IPC IS Connected to the Document

1. **API Handler Creation**: `API_HANDLER_SCH` is constructed with `SCH_EDIT_FRAME* m_frame` - the same frame that loads and manages files.

2. **Screen Access**: `GetScreenForApi()` returns:
   - If agent transaction active: The target sheet's screen
   - Otherwise: `GetCurrentSheet().LastScreen()` - the actual document screen

3. **Item Iteration**: `screen->Items()` is an `EE_RTREE&` containing all schematic items from the loaded file.

## Root Cause Analysis

The reported issues are likely NOT caused by IPC/file disconnect. Potential causes:

### 1. Sheet Context Issues
- `GetScreenForApi()` operates on the CURRENT sheet only
- For hierarchical schematics, items on other sheets won't be returned
- The agent's target sheet might differ from where items exist

### 2. Timing Issues
- If API calls happen during file loading, screen might be empty
- `OpenProjectFiles()` sets `SetScreen(nullptr)` at line 159 before loading

### 3. Python Code Generation Bugs
- Agent-generated Python code might have incorrect error handling
- kipy method calls might not properly serialize/deserialize responses

### 4. Document Specifier Mismatch
- kipy caches DocumentSpecifier from `get_schematic()`
- If file is reloaded with different UUID, validation might fail

## Proposed Solutions

### Phase 1: Diagnostics (Immediate)

Add logging to understand what's happening:

1. **In `sch_tool_handler.cpp`** - Add debug output to generated Python:
```python
print(f"[DEBUG] symbols count: {len(sch.symbols.get_all())}", file=sys.stderr)
print(f"[DEBUG] document: {sch.document}", file=sys.stderr)
```

2. **In `terminal_frame.cpp`** - Log the socket path and connection status

3. **In `api_handler_sch.cpp`** - Add `wxLogDebug` for screen item counts

### Phase 2: Robustness Improvements

1. **Refresh Document on Each Call**
   - Modify kipy `Schematic` operations to optionally refresh the document specifier
   - Add `sch.refresh_document()` method that re-queries `GetOpenDocuments`

2. **Wait for Document Ready**
   - Add a "document ready" check before IPC operations
   - Query document state until it's fully loaded

3. **Handle All Sheets**
   - Modify `GetItems` to optionally return items from all sheets
   - Add sheet navigation to agent tools

### Phase 3: Agent Transaction Enhancement

1. **Auto-sync on Transaction Start**
   - When `MAIL_AGENT_BEGIN_TRANSACTION` is received, capture current document state
   - Store document UUID for validation

2. **Document Change Notification**
   - When file is loaded/reloaded, send notification to agent
   - Agent can invalidate cached `sch` object

### Phase 4: kipy Improvements (Requires kipy Changes)

1. **Add `Schematic.refresh()` Method**
   - Re-fetches document specifier
   - Updates internal `_doc` reference

2. **Add Connection Health Check**
   - Ping before operations
   - Automatic reconnection on failure

3. **Sheet-Aware Operations**
   - Add optional `sheet_path` parameter to get_all methods
   - Support querying specific sheets

## Testing Plan

1. **Basic Connectivity Test**
   - Open schematic with symbols
   - Run `sch_live_summary`
   - Verify symbol count matches

2. **Reload Test**
   - Open schematic A
   - Run `sch_live_summary` (should work)
   - Close and open schematic B
   - Run `sch_live_summary` (should work with B's content)

3. **Hierarchical Test**
   - Open hierarchical schematic
   - Navigate to sub-sheet
   - Run `sch_live_summary`
   - Verify items from current sheet only

4. **Concurrent Edit Test**
   - Agent adds symbol via IPC
   - User adds symbol via UI
   - Both should be visible

## Implementation Priority

1. **High**: Add diagnostics to understand current failures
2. **High**: Fix any bugs in agent-generated Python code
3. **Medium**: Add document refresh capability to kipy
4. **Medium**: Add sheet-aware operations
5. **Low**: Document change notifications

## Fixes Applied

### 1. Added IPC Diagnostics (sch_tool_handler.cpp)
- Added debug output to `sch_live_summary` Python code
- Logs document specifier, sheet path, and symbol count to stderr
- Helps identify where data retrieval fails

### 2. Fixed sch_open_sheet Navigation (sch_crud_handler.cpp)
- **Issue**: `TypeError: CopyFrom() expected SheetPath got Sheet`
- **Cause**: `navigate_to()` expects a `SheetPath` proto, not a `SheetHierarchyNode`
- **Fix**: Use `target_sheet.path` when calling `navigate_to()`

### 3. Fixed Reference Parameter in sch_add (sch_crud_handler.cpp)
- **Issue**: Top-level `reference` parameter was ignored
- **Cause**: Only checked `properties.Reference`, not the top-level `reference` param
- **Fix**: Added handling for top-level `reference` parameter before checking `properties`

### 4. Added UUID Tracing for Close/Reopen Investigation

Added comprehensive logging to trace UUID flow during schematic loading. Run with `WXTRACE=SCHEMATIC` to see traces.

**Files modified:**

1. **sch_io_kicad_sexpr_parser.cpp** (lines ~2777-2795):
   - Logs when UUID is read from file
   - Logs whether current sheet is the root sheet
   - Logs the final root sheet UUID after it's set

2. **api_handler_sch.cpp** (handleGetOpenDocuments):
   - Logs the current sheet size and UUID being returned
   - Logs the current file name

3. **schematic.cpp** (SetRoot):
   - Logs root sheet UUID and screen UUID when SetRoot completes

4. **files-io.cpp** (OpenProjectFiles):
   - Logs UUID after SetSchematic is called
   - For multi-root path: Logs project file UUID, whether it's niluuid, and whether UUID override happens

**To trace the issue:**
```bash
WXTRACE=SCHEMATIC ./Zeo
```

Then perform close/reopen cycle and examine logs to see where UUID mismatch occurs.

## Conclusion

The IPC layer is fundamentally sound. The API handler correctly accesses the live document. Investigation should focus on:

1. Adding diagnostics to trace actual failures
2. Checking sheet context for hierarchical schematics
3. Verifying kipy method implementations
4. Testing timing scenarios during file operations

The plan above provides a path to robust IPC synchronization without requiring architectural changes to the C++ side.
