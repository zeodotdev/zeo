# Multi-Board Architecture Refactor — Phase M

Living plan for consolidating multi-board infrastructure around KiCad-native
primitives, introducing a dedicated mbschema editor, redesigning the project
manager launcher, and enabling concurrent multi-editor work.

## Current state

### Working (verified by runtime testing)
- `.kicad_multi` JSON load/save via `MULTI_BOARD_PROJECT`
- Sub-project scanning (connectors + pads) for MBS auto-generation
- MBS rendering via `SCH_MODULE_BLOCK` / `SCH_MODULE_PIN`
- Wire-based union-find cross-board net extraction
- Text-level PCB net sync with reverse propagation + conflict detection
- MBS save hook triggering extraction + sync
- **Multi-board netlist updater** (user-tested — exercised via normal workflow)
- **Component assignment manager** (user-tested — 90% functional)

### Present but untested
- **3D assembly viewer** — ~70% functional by code reading (rendering, collision,
  connector mating implemented; STEP export stubbed). Has not been exercised by
  the user. Completeness estimates are based on source inspection only.
- **Cross-board DRC** (`pcbnew/drc/drc_engine_cross_board.{h,cpp}`) — ~50%
  functional by code reading. `CheckConnectorMatching` partially implemented;
  `CheckSignalIntegrity` and `CheckPowerDistribution` are TODOs. Critical
  blocker: `GetBoardByUuid()` returns nullptr, so all checks fail at entry.
  **Not tested.**

### Incomplete or fragile
- **Directory-walking MBS lookup** (`sch_editor_control.cpp:635`,
  `files-io.cpp:1590`) walks up 6 levels for `.kicad_multi`; picks first
  found, breaks on deeper nesting.
- **Text-level PCB sync** is regex-based; fragile on escaped chars /
  non-standard whitespace.
- **Open-multi-board flow** in manager has no explicit handler.
- **Shared scaffold blocker**: `GetBoardByUuid()` unimplemented everywhere —
  modules assume boards are already in memory but never load them.

### Kiway constraint (narrowed)
The essential blocker is two lines: `KIWAY::Prj()` delegates to
`SETTINGS_MANAGER::Prj()`, which returns
`*m_projects_list.begin()->get()` — the first project in an internal
vector. No per-player or per-frame project context. Surrounding
architecture (SCHEMATIC + BOARD `SetProject()`, multi-project
`SETTINGS_MANAGER` storage, library adapters taking `PROJECT*` as
parameters) is more accommodating than comments suggest.

---

## Phase M0 — Pre-work (~0.25 day)

1. **Branch snapshot** — tag the current state before the refactor so we
   can diff cleanly and roll back if needed.
2. **No migration needed.** Existing test projects (`test_board_56`,
   `test_boards_50`, `test_boards_55`, `KiCAD_agent_test_board`) are
   disposable and will be recreated fresh after the refactor.

---

## Phase M4 status (2026-04-20)

**Landed** (build green):
- M4.1 (de facto): `SETTINGS_MANAGER` already supports holding multiple
  projects simultaneously. `LoadProject(path, aSetActive=false)` adds
  without eviction. `GetProject(path)` returns any loaded project by
  path. No changes needed.
- M4.2: `KIWAY_HOLDER::SetPrjOverride(PROJECT*)` added. All frames /
  dialogs / panels that derive from `KIWAY_HOLDER` (which is essentially
  everything) resolve `this->Prj()` to the overridden project when set.
  Default unchanged: falls back to `Kiway().Prj()`.

**Consumption path** (example):
```
Pgm().GetSettingsManager().LoadProject(subPath, /*aSetActive=*/false);
PROJECT* sub = Pgm().GetSettingsManager().GetProject(subPath);

KIWAY_PLAYER* frame = Kiway().Player(FRAME_SCH, true);
frame->SetPrjOverride(sub);
frame->OpenProjectFiles({ subSchematicPath });   // uses frame's Prj()
```

**M4.0 spike succeeded** (2026-04-20):

Verified by opening a multi-board project with two sub-boards, running
the temporary "Open First Sub-Board Schematic (peer)" action. Result:
- Fresh SCH_EDIT_FRAME launched alongside the existing project manager
- Title bar, schematic hierarchy, library lookups all resolve to the
  sub-project via `SetPrjOverride`
- Two concurrent SCH_EDIT_FRAMEs viable in one process
- `SCH_EDIT_FRAME` constructor binding to global `Prj()` at line 197
  self-corrects when `OpenProjectFiles` calls `SetSchematic()` with a
  new `SCHEMATIC(&Prj())` that picks up the override

Root fix that unblocked the spike: C-style cast instead of
`dynamic_cast` when casting `CreateKiWindow`'s return value. The eeschema
kiface DSO has hidden typeinfo visibility; `dynamic_cast` from the
manager binary fails even though the hierarchy is correct. This is
consistent with `Kiway::Player()`'s own pattern.

**M4.3 — Kiway peer-player support** ✓ landed.

- New APIs on `KIWAY`: `RegisterPeerPlayer(FRAME_T, wxWindowID)`,
  `UnregisterPeerPlayer(FRAME_T, wxWindowID)`,
  `GetAllPlayerFrames(FRAME_T)`.
- Mutex-protected `std::map<FRAME_T, std::vector<wxWindowID>>
  m_peerPlayerFrames` alongside the existing single-frame cache.
- `ExpressMail` now broadcasts to the primary frame + every peer frame
  matching `aDestination`. Single-instance editors see unchanged
  behavior; peer editors now receive cross-probe / broadcast mail.
- `SpawnPeerSchematicEditor` registers on create + unregisters on
  `wxEVT_CLOSE_WINDOW`.

**M4.4 — Sub-project open UX (minimum-viable)** ✓ landed.

- Menu: File → Open Sub-Board Schematic in New Window
- Duplicate-protection: if a peer frame is already open on the
  requested sub-project, raise the existing frame.
- Lifecycle: peer frames self-unregister on close.
- Full tree context menu ("Open Schematic in Peer" on any sub-project
  row) and PCB peer-open are deferred to the M3 launcher redesign.

**M4.5 — frame identity**: partially achieved by peer editor already
showing the sub-project name in its title (the SCH_EDIT_FRAME derives
title from `Prj()`, which is now correctly the sub-project). Task-
switcher entries already differentiate. No further work needed for MVP.

---

## Phase M1 status (2026-04-20)

**Landed** (build green):
- M1.1: PROJECT_FILE schema extended with `multi_board.container`,
  `multi_board.sub_projects[]`, `multi_board.cross_board_nets[]`,
  `multi_board.mbs_file`. Full JSON round-trip.
- M1.2: PROJECT_FILE accessors (`IsMultiBoardContainer`,
  `Get/Set/Add/RemoveSubProject`, `Get/SetCrossBoardNets`, `GetMbsFileName`,
  `ResolveSubProjectPath`). Shim in `multi_board_project.h` forwards its
  struct typedefs to the PROJECT_FILE versions (single source of truth).

**Deferred** (requires a dedicated session):
- M1.3: semantic Y migration (container becomes `Prj()`). Attempted,
  reverted. Blast radius was larger than a single-session budget:
    - `EnsureMbsFile` (MBS s-expr generator) needs relocation.
    - 10+ callers hold `MULTI_BOARD_PROJECT*` and read methods like
      `GetName()`, `GetRootDir()`, `LoadFromFile(path)`, `SaveToFile(path)`
      that have no PROJECT_FILE equivalent.
    - `kicad_manager_frame::m_multiBoardProject` side-stored state has to
      be replaced without breaking the existing sub-project switch flow.
    - SETTINGS_MANAGER's single-active-project assumption means loading a
      container as Prj() AND a sub-project as a second loaded PROJECT
      requires the M4 work.
- M1.4 (delete MULTI_BOARD_PROJECT) and M1.5 (retire `.kicad_multi`
  extension) both depend on M1.3.

## Phase M1 — File unification (~3 days, low-medium risk)

**Goal:** eliminate `.kicad_multi` and `_mbs.kicad_pro`; use standard
`.kicad_pro` with an extended `multi_board` section. Reconcile dual
schemas without losing work from either side.

### 1.1 Extend `PROJECT_FILE::multi_board` with new fields

The pre-existing schema (`project_file.h:76-157`) has three arrays:
`boards`, `cross_board_connections`, `component_assignments`.

Extend to include:
- `multi_board.container: bool` — distinguishes container project from
  sub-project (every `.kicad_pro` serializes `multi_board` even empty,
  so section presence is not a marker).
- `multi_board.sub_projects: SUB_PROJECT_INFO[]` — our existing list of
  sub-project references (uuid, relativePath, displayName, role).
- `multi_board.cross_board_nets: MB_CROSS_BOARD_NET[]` — our pad-level
  net model (uuid, name, endpoints[]). This is orthogonal to the
  existing `cross_board_connections` which is point-to-point.

### 1.2 Preserve existing structures

`BOARD_INFO`, `CROSS_BOARD_CONNECTION`, `COMPONENT_BOARD_ASSIGNMENT` stay.
They model a different topology (single-project multi-PCB) but their
consumers include two tested modules:
- **Multi-board netlist updater** — tested. Keep.
- **Component assignment manager** — tested. Keep.

For container projects, `boards[]` will typically be empty; sub-projects
have their own `.kicad_pcb` in their own `.kicad_pro`. The structures are
preserved for backward compatibility and for the untested modules.

### 1.3 Retire `MULTI_BOARD_PROJECT` class

Delete:
- `include/project/multi_board_project.h`
- `common/project/multi_board_project.cpp`

Callers that read multi-board state (`kicad_manager_frame.cpp`,
`kicad_manager_control.cpp`, `dialog_multi_board_setup.cpp`,
`files-io.cpp::syncCrossBoardNetsIfMbs`, MBS refresh + extractor code,
`cross_board_pcb_sync.cpp`) switch to `Prj().GetProjectFile()` and the
new helpers.

### 1.4 Registration cleanup

- Remove `FILEEXT::MultiBoardProjectFileExtension` from
  `common/wildcards_and_files_ext.cpp:138,264,272`.
- Remove `MULTI_BOARD_PROJECT` enum from `kicad/tree_file_type.h:68`.
- Update `kicad/project_template.cpp` — multi-board template emits
  `<name>.kicad_pro` with `multi_board.container = true` populated.

### 1.5 Testable outcome
- Run migration script on all four test projects → silent conversion.
- Open a migrated project → MBS opens, nets extract as before, PCB sync
  unchanged.
- Tested multi-board netlist updater + component assignment still work.
- New multi-board project: only `<name>.kicad_pro` at top level
  (no `.kicad_multi`, no `_mbs.kicad_pro`).

---

## Phase M2 — `.kicad_mbs` format + mbschema editor (~4-5 days, medium risk)

**Goal:** dedicated editor for multi-board schematics; MBS files use
`.kicad_mbs` extension. Removes the eeschema toolbar noise by design.

1. **New extension.** `FILEEXT::MbsFileExtension = "kicad_mbs"`. The on-disk
   format is identical to `.kicad_sch` — existing `SCH_IO_KICAD_SEXPR`
   parser/writer handle both. `_mbs.kicad_sch` files rename to
   `.kicad_mbs` during migration (M0).
2. **Create `mbschema/` directory.** Mirrors `eeschema/`:
   - `mbschema.cpp` — KIFACE entry point, implementing `KIFACE_GETTER()`
     and `CreateKiWindow()` dispatching `FRAME_MBSCH`.
   - `mbschema/CMakeLists.txt` mirroring `eeschema/CMakeLists.txt:643-750`
     (MACOSX_BUNDLE, `OSX_BUNDLE_BUILD_KIFACE_DIR`, install rules).
   - `mbschema/mbsch_edit_frame.{h,cpp}` — `MBSCH_EDIT_FRAME :
     SCH_BASE_FRAME`.
   - `mbschema/tools/mbsch_actions.{h,cpp}` — explicit trimmed action
     list: select, wire, label, place-module-block, refresh-interface,
     sync-to-PCBs, manage-sub-boards, ERC. **Excludes** symbol-place,
     component-edit, annotate, power-symbol, simulation.
   - `mbschema/tools/mbsch_editor_control.{h,cpp}` — MBS-specific
     handlers ported from `sch_editor_control.cpp`.
   - `mbschema/mbsch_menu.cpp` — trimmed menubar.
3. **Frame / KIFACE registration:**
   - `include/frame_type.h` — add `FRAME_MBSCH`.
   - `include/kiway.h` — add `FACE_MBSCH` to `FACE_T` enum.
   - `common/kiway.cpp:131` — add DSO naming case (`_mbschema.kiface`).
   - `common/kiway.cpp:~358` — `KifaceType()` maps `FRAME_MBSCH` →
     `FACE_MBSCH`.
   - Root `CMakeLists.txt` — `add_subdirectory(mbschema)`.
4. **Shared code strategy.** SCH_MODULE_BLOCK, SCH_MODULE_PIN, painter,
   connectivity, parser/writer all stay in `eeschema_kiface`. Mbschema
   links against it. Revisit factoring into `common/sch_shared/` later if
   we want hard separation.
5. **Port MBS-specific state** from eeschema to mbschema:
   - `eeschema/multi_board_net_extractor.{h,cpp}` → `mbschema/`
   - `eeschema/multi_board_mbs_refresh.{h,cpp}` → `mbschema/`
   - `RefreshMbsFromSubProjects` action → `mbschema/tools/`
   - `syncCrossBoardNetsIfMbs` hook → `mbschema/mbsch_edit_frame.cpp::OnSave()`
   - Remove corresponding declarations from `eeschema/sch_edit_frame.h`.
6. **Kiway dispatch.** `kicad_manager_control.cpp::EditMultiBoardSchematic`
   calls `Kiway().Player(FRAME_MBSCH, true)` instead of `FRAME_SCH`.
   File-open dispatch: `.kicad_mbs` → FRAME_MBSCH; `.kicad_sch` → FRAME_SCH.

**Testable:**
- Post-M1 MBS opens in `MBSCH_EDIT_FRAME` showing only MBS-relevant tools.
- Sub-project `.kicad_sch` still opens in eeschema (unchanged).
- Net extraction + sync continue to work end-to-end.

---

## Phase M3 — Launcher redesign (~2 days, low risk)

**Goal:** project tree in main area, dockable sidebar on left with action
buttons.

1. **Layout rebuild:** introduce `wxAuiManager` in `KICAD_MANAGER_FRAME`.
   `m_projectTreePane` becomes the center pane. New narrow sidebar
   (~200px) on the left holds action buttons.
2. **Retire `PANEL_KICAD_LAUNCHER`** icon grid (or repurpose as empty-state
   pane when no project is loaded). Sidebar buttons invoke existing
   `TOOL_ACTION`s — zero action API changes.
3. **Multi-board awareness:** sidebar shows Edit MBS / Manage Sub-Boards /
   Sync actions only when `Prj().GetProjectFile()` has
   `multi_board.container = true`.
4. **Tree pane renders sub-project nodes** as children of the multi-board
   root (fix for "sub-projects not rendered in tree" audit finding).
5. **AUI persistence:** dock positions stored in local settings.

**Testable:** AUI docking works, all existing action flows unregressed,
tree shows sub-projects under multi-board root.

---

## Phase M4 — Concurrent editor instances (~2-3 weeks MVP, 4-6 weeks full)

**Goal:** user can open multiple sub-project schematics / PCBs
simultaneously, each tied to its own `PROJECT`.

Revised scope based on deep-dive: **in-process is tractable**. The
"~1-2 months" initial estimate was based on conservatively counting
all 641 `Prj()` call sites; deep reading shows most are shallow
(path resolution, cached settings, library adapters that already accept
`PROJECT*`). Not separate-process.

### M4.1 Minimum-viable path (~2-3 weeks)

1. **Frame owns project.** Add `PROJECT* m_project` to `SCH_EDIT_FRAME`,
   `PCB_EDIT_FRAME`, `MBSCH_EDIT_FRAME`. Populated at
   `OpenProjectFiles()` time from the PROJECT instance owned by
   SETTINGS_MANAGER.
2. **Frame-local `Prj()`.** Override `KIWAY_PLAYER::Prj()` in each
   frame to return `*m_project`. Removes dependency on
   SETTINGS_MANAGER's "first in list."
3. **SETTINGS_MANAGER active-by-path.** Replace `Prj()` →
   `m_projects_list.front()` with a string field
   `m_activeProjectPath`. `Prj()` returns
   `*m_projects[m_activeProjectPath]`. `LoadProject(aPath,
   aSetActive=true)` updates the path without evicting other loaded
   projects.
4. **Fix shallow Prj() sites.** Sample-and-sweep through dialogs
   and tools that call global `Prj()` where a frame is in scope.
   Replace with `m_frame->Prj()`. Approximate count: 50-100.
5. **Library adapter smoothing.** Adapters
   (`SYMBOL_LIBRARY_ADAPTER`, `FOOTPRINT_LIBRARY_ADAPTER`) already
   take `PROJECT*`. Audit callers to ensure they pass the frame's
   project, not the global.
6. **Frame identity.** Titles include project basename (`<project> —
   Schematic Editor`). Task-switcher distinguishes windows.
7. **Sub-project switcher.** Current "Switch Sub-Board" action becomes
   "Open Sub-Board" — spawns new frame rather than replacing current.

### M4.2 Full production cleanup (2-3 additional weeks, optional)

1. Sweep remaining `Prj()` call sites to use frame context where one
   is available. Update dialogs to accept PROJECT explicitly where no
   frame is in scope.
2. Per-frame local library table caches (symbol/footprint) to avoid
   cross-project pollution.
3. Settings preloader per-project (already mostly correct; verify).
4. ExpressMail routing — inter-frame messages keyed by (project, frame
   type) tuple instead of just frame type.

### M4.3 Spike first

Before committing full scope, **half-day spike**: manually instantiate a
second `SCH_EDIT_FRAME` pointing at a second `PROJECT` in a debug build.
Catalog concrete failures. The 5-step MVP plan is based on inspection;
the spike validates nothing unexpected breaks.

**Testable:** fpv_drone.kicad_sch and zeo1.2_devboard.kicad_sch open
simultaneously in two schematic editors, plus the MBS in mbschema, plus
both PCBs in pcbnew — five independent windows, no cross-talk, closing
one doesn't affect others.

---

## Phase M5 — Cleanup + follow-ons (~2-3 days)

1. **Replace directory-walk MBS lookup** with explicit parent-project
   reference stored in the `.kicad_mbs` file (`(parent_project "...")`
   s-expr or in the `(title_block)` metadata).
2. **Replace text-level PCB sync** with BOARD-level edits via
   `PCB_IO_KICAD_SEXPR::LoadBoard()`. Robust handling of quoted strings,
   whitespace, escape sequences.
3. **Validate 3D assembly viewer** on a migrated multi-board project.
   If it works: adapt to load sub-project PCBs via their `.kicad_pro`
   paths instead of `BOARD_INFO`-by-UUID. If broken: decide whether to
   invest in fixing or defer.
4. **Validate cross-board DRC** similarly. The core blocker
   (`GetBoardByUuid`) is easy to fix once we have a sub-project →
   `BOARD` loader. But the check stubs (`CheckSignalIntegrity`,
   `CheckPowerDistribution`) are a separate investment.
5. **Evaluate CONNECTION_GRAPH integration** — register
   `SCH_MODULE_BLOCK_T` / `SCH_MODULE_PIN_T`, retire our custom
   union-find extractor if the graph integration is cheap. ~4 hours if
   we go for it; skip if the graph API makes it harder.
6. **Cross-board ERC rules** — walk `multi_board.cross_board_nets`,
   validate endpoint counts + pin directions + pad-count match between
   paired connectors.

---

## Dependencies + schedule

```
M0 (pre-work) ──► M1 (file unify) ─┬─► M2 (mbschema) ──┐
                                   └─► M3 (launcher) ──┼─► M5 (cleanup)
                                                       │
M4 (multi-editor, can start after M2) ────────────────┘
```

| Week | Focus |
|---|---|
| 0 | M0 (migration script, tag branch) |
| 1 | M1 (file unify) |
| 2 | M2 (mbschema) + M3 (launcher) parallel |
| 3 | M4 spike + MVP start |
| 4-5 | M4 MVP |
| 6 | M5 (cleanup, validation of untested modules) |

Total: 6 weeks to MVP. Optional M4.2 full cleanup extends to weeks 7-8.

---

## Risks + open questions (all resolved)

### R1 — Pre-existing PROJECT_FILE `multi_board` fields

**Resolution:** keep the existing structures (`BOARD_INFO`,
`CROSS_BOARD_CONNECTION`, `COMPONENT_BOARD_ASSIGNMENT`) alongside new
ones (`sub_projects`, `cross_board_nets`). The tested modules
(multi-board netlist updater, component assignment) rely on them.
Container projects typically leave `boards[]` empty since sub-projects
manage their own PCBs; the structures remain for the tested-but-partial
modules and for future uses.

### R2 — Container vs sub-project distinction

**Resolution:** add `multi_board.container: bool` flag. Section
presence alone is insufficient since every `.kicad_pro` serializes
`multi_board` by default.

### R3 — M4 effort revised

**Resolution:** in-process multi-project is tractable (2-3 weeks MVP,
not 1-2 months). The essential blocker is two lines in
`settings_manager.cpp:1138` and `kiway.cpp:213`. Surrounding
architecture (SCHEMATIC + BOARD already cache `PROJECT*`, library
adapters take `PROJECT*`, SETTINGS_MANAGER can hold multiple projects
in memory) is accommodating. Proceed with in-process path; spike
first.

### R4 — Mbschema kiface linkage

**Resolution:** mechanical ~9-step recipe mirroring eeschema. No
blockers.

### R5 — Test project migration

**Resolution:** none needed. Existing test projects discarded and
recreated post-refactor.

### R6 — Untested module validation (new)

**Risk:** 3D assembly viewer and cross-board DRC are present in the
code but have never been exercised by the user. Agent estimates of
70% / 50% completeness are from source reading, not runtime. Their
status post-refactor is unknown.

**Mitigation:** preserve the code through M1-M4 unchanged. Validate in
M5 on migrated test projects. If a module is functional with minor
adaptation, keep it. If fundamentally broken, decide whether to fix
(can be its own phase) or defer.

---

## Key decisions locked in

1. **Multi-project topology.** Each sub-project is a standalone
   `.kicad_pro` + `.kicad_sch` + `.kicad_pcb`. The multi-board parent
   is a `.kicad_pro` with `multi_board.container = true` and an MBS
   schematic (`.kicad_mbs`) at the same level. Matches Altium's peer
   model.
2. **Dedicated mbschema editor.** Not a mode in eeschema.
3. **In-process multi-project.** Not separate processes.
4. **Tested modules preserved.** Multi-board netlist updater and
   component assignment keep their current code paths.
5. **Untested modules preserved for later validation.** 3D assembly
   and cross-board DRC adapted in M5, not deleted.
