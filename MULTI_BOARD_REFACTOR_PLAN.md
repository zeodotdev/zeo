# Multi-Board Architecture — Implementation Reference

Living reference for the multi-board (MBS) feature in Zeo. Documents
the current implementation, key architectural decisions other
implementers need to know, and the remaining work.

---

## What multi-board looks like

**Container topology.** A multi-board project is a `.kicad_pro` whose
`PROJECT_FILE::multi_board.container = true` flag is set, plus a
sibling `.kicad_mbs` schematic at the same level. The container's
`sub_projects[]` array lists each member sub-project (each is a
standalone `.kicad_pro` + `.kicad_sch` + `.kicad_pcb` in its own
subdirectory). The container's `cross_board_nets[]` array records
which connector pads on which sub-projects share a net.

```
my_radio.kicad_pro              <-- container (multi_board.container=true)
my_radio.kicad_mbs              <-- multi-board schematic
boards/
  fc/
    fc.kicad_pro                <-- sub-project (standard layout)
    fc.kicad_sch
    fc.kicad_pcb
  vtx/
    vtx.kicad_pro
    vtx.kicad_sch
    vtx.kicad_pcb
```

**Frames.** `FRAME_MBSCH` is the multi-board schematic editor; it's
implemented as `MBSCH_EDIT_FRAME` (subclass of `SCH_EDIT_FRAME`,
shares the eeschema kiface). `FRAME_SCH` is the regular sub-project
schematic editor. Both can run as Kiway peers in one process — see
M4 below.

**Items.** `SCH_MODULE_BLOCK` (subclass of `SCH_ITEM`) renders as a
rectangle on the MBS, one per connector reference per sub-project.
Each block owns `SCH_MODULE_PIN` children (subclass of
`SCH_HIERLABEL`), one per connector pad. Pins carry a
`(componentRef, pinNumber, netName, electricalType)` tuple persisted
via the `(electrical_type "...")` token in `.kicad_mbs`.

---

## Architecture

### Data flow

```
sub-project schematic ──▶ MBS canvas ──▶ container .kicad_pro
                          (refresh)      (cross_board_nets[])
                                                │
                                                ├──▶ sub-project PCBs
                                                │      (per-board pull
                                                │       in dialog_update_pcb,
                                                │       or bulk push from MBSCH)
                                                │
                                                └──▶ ERC / DRC / cross-probe
```

1. **Refresh** (`MBSCH_EDIT_FRAME` toolbar): scans each sub-project's
   schematic + PCB for connector-class symbols (J1, P3, etc.) and
   their pads, then diffs against the current MBS. Categories:
   `ADD_BLOCK`, `REMOVE_BLOCK`, `ADD_PIN`, `REMOVE_PIN`, `RENAME_PIN`,
   `PATH_DRIFT`, `UPGRADE_UUID`. Dialog shows category checkboxes +
   streaming console; new blocks attach to cursor for placement.
2. **MBS save** triggers `ExtractCrossBoardNets` (union-find on
   module pins, joins by shared local net name within a block AND
   by MBS wire connectivity), writes `cross_board_nets[]` into the
   container `.kicad_pro`.
3. **Sub-project sync**:
   - **Per-board** — `Update PCB from Schematic` runs the standard
     `BOARD_NETLIST_UPDATER`, then `ApplyCrossBoardNetsToBoard`
     overrides connector pad nets with MBS-declared values.
   - **Bulk** — MBSCH toolbar's `Sync to PCB` action does a
     text-level edit of every sub-project's `.kicad_pcb` directly
     (no PCB editor open required).
4. **Verification**:
   - **MBS ERC** (M5.8) — runs from the MBSCH frame; checks
     unconnected module pins, single-endpoint cross-board nets, and
     pin-type matrix conflicts (GND↔5V shorts).
   - **Sub-project ERC/DRC** — suppresses no-driver / unconnected-pad
     false positives on cross-board pins (M8.9.1, M8.9.2).
   - **Binding DRC** (M8.4) — auto-fires in pcbnew DRC; verifies
     each connector pad carries the MBS-declared net.
5. **Cross-probe / net highlight** — sender-side file-based fan-out
   (`MultiBoardCollectCrossBoardProbesForLocalNet`); works whether or
   not MBSCH is open.

### Key files

| Concern | File |
|---|---|
| Container + sub-project + cross-board-net types | `include/project/project_file.h` |
| Container scan helpers (connectors, pads, bindings, probes) | `include/project/multi_board_scan.h`, `common/project/multi_board_scan.cpp` |
| MBS items | `eeschema/sch_module_block.{h,cpp}`, `eeschema/sch_module_pin.{h,cpp}` |
| MBS frame | `eeschema/mbsch_edit_frame.{h,cpp}`, `eeschema/toolbars_mbsch_editor.cpp` |
| Refresh diff + apply | `eeschema/multi_board_mbs_refresh.{h,cpp}` |
| Refresh dialog | `eeschema/dialogs/dialog_mbs_refresh.{h,cpp}` |
| Cross-board net extractor | `eeschema/multi_board_net_extractor.{h,cpp}` |
| Bulk push to all sub-project PCBs (text-level) | `common/project/cross_board_pcb_sync.{h,cpp}` |
| In-memory cross-board apply (per-board pull) | `pcbnew/multi_board/cross_board_apply_to_board.{h,cpp}` |
| Cross-board ERC | `eeschema/erc/erc.cpp::TestCrossBoardConnectivity` |
| Cross-board binding DRC | `pcbnew/drc/drc_test_provider_cross_board_binding.cpp` |
| 3D assembly viewer | `3d-viewer/3d_viewer/3d_viewer_assembly.{h,cpp}`, `3d-viewer/dialogs/panel_3d_assembly.{h,cpp}` |
| Module block properties dialog | `eeschema/dialogs/dialog_module_block_properties.{h,cpp}` |
| Sub-project board loader | `pcbnew/multi_board/sub_project_board_loader.{h,cpp}` |
| Peer SCH/PCB spawning | `eeschema/multi_board_peer_open.h`, `eeschema/tools/sch_editor_control.cpp` (impl) |

---

## Locked-in decisions

1. **Multi-project topology, not multi-board-in-one-project.** Each
   sub-project is standalone; the container project just lists them.
   Matches Altium's peer model and lets sub-projects open independently.
2. **MBS editor is a subclass of `SCH_EDIT_FRAME`, not a separate
   kiface.** All MBS items live in the eeschema kiface so we share
   the painter, parser, connection graph, and selection tool. Subclass
   pattern lets us trim the menubar and override save / cross-probe
   without splitting the kiface.
3. **In-process multi-project**, not separate processes. M4 added
   peer-player support (`KIWAY::RegisterPeerPlayer`,
   `SetPrjOverride`) so multiple `SCH_EDIT_FRAME`s pinned to
   different sub-projects can coexist in one Zeo process.
4. **3D viewer composes multiple `BOARD_ADAPTER`s with per-instance
   transforms.** Not a merged super-BOARD, not an extended single-
   adapter. Renderers (`RENDER_3D_OPENGL`) gain "assembly pose"
   matrices so each instance draws into the same scene. Translation-
   only composition is landed (M6.C-phase-1); rotation / mating /
   collision pending.
5. **Sub-project board loader (`pcbnew/multi_board/sub_project_board_loader.{h,cpp}`)
   returns owner-managed `std::unique_ptr<BOARD>`, no shared cache.**
   Used by 3D viewer, cross-board DRC, and (via fallback) the
   netlist updater.
6. **MBS cross-board nets are authoritative** for connector-pad net
   assignments. When schematic-side sync says "net X" and MBS says
   "net Y" for the same pad, MBS wins; the per-board apply reports
   the override as a warning. Rationale: wiring two module pins
   together on the MBS is the user explicitly declaring "these are
   the same net everywhere" — that has to override per-board labels.
7. **Container lookup is a directory walk** (up to 6 levels from a
   sub-project's `.kicad_pro`). Replace with an explicit parent
   reference stored in the `.kicad_mbs` (M5.2 — see remaining work).
8. **M7.1 container libraries are physically replicated, not a runtime
   tier.** A library added at "container scope" is written to the
   container's `sym-lib-table` / `fp-lib-table` AND copied (with
   `shared=true`) into each sub-project's lib-table. Each sub-project
   stays individually openable / shippable — its lib-table is
   self-contained. Rationale: the alternative (a third runtime tier
   layered into `LIBRARY_MANAGER_ADAPTER`) coupled sub-project
   library resolution to the container's lifetime, which broke the
   "open one board standalone" workflow and made sub-projects
   undistributable on their own. Reconciliation runs on sub-project
   open: parse the container's lib-table file directly (no PROJECT
   load), add missing `shared` rows, drop orphaned `shared` rows.
   Local (`shared=false`) rows are never touched.

### Kiway constraint

`KIWAY::Prj()` still delegates to `SETTINGS_MANAGER::Prj()` which
returns the first project in the manager's list. Per-frame project
resolution is via `KIWAY_HOLDER::SetPrjOverride`. Callers inside
shared kiface code that hit `Kiway().Prj()` directly will get the
wrong project in multi-project sessions — see M4.2 in remaining work.

### Cross-probe / mail scoping

Two patterns coexist; pick the right one for new mail commands:

- **In-band `$PROJECT:` token** for cross-probe / selection packets
  whose target is determined by *content* (e.g., MBSCH originating a
  scoped probe to a specific sub-project). Receivers parse the token
  and silently skip non-matching projects.
- **Sender-aware peer filter** at the top of `KiwayMailIn` for
  request/response and mutating commands routed by frame type alone
  (`MAIL_SCH_GET_NETLIST`, `MAIL_PCB_UPDATE`, etc.). Compares
  `sender->Prj().GetProjectFullName()` against ours. KIWAY broadcasts
  to every peer of a frame type, so this filter is required to keep
  M4 multi-peer sessions deterministic.

---

## Implementation status

### Shipped

| Phase | Item |
|---|---|
| M0 | Pre-work, branch snapshot |
| M1 | Container model unified under `PROJECT_FILE::multi_board` |
| M2 | `.kicad_mbs` extension, `MBSCH_EDIT_FRAME` subclass, save hook |
| M4 (MVP) | Peer player support; `SetPrjOverride`; broadcast ExpressMail; File→Open Sub-Board Schematic in New Window |
| M5.1 | Shared sub-project board loader |
| M5.8 | MBS-side cross-board ERC (unconnected pin, single-endpoint net, pin-type matrix) |
| M6.A/B | 3D assembly skeleton ported to container model; assembly frame renders one sub-board at a time |
| M6.C-phase-1 | OpenGL multi-board composition with translation-only per-instance transforms |
| M6.D (partial) | Connector mating — see active 3D viewer work |
| M8.4 | Cross-board net binding DRC check |
| M8.6 | All MBS edit operations (delete, rotate, mirror, drag-corner resize, move-with-wire-pinning, single-click wire termination) |
| M8.9.1 | Sub-project ERC suppresses no-driver false positives on cross-board pins |
| M8.9.2 | Sub-project DRC suppresses unconnected-pad false positives on cross-board pins |
| M8.5 (refresh) | Refresh dialog redesigned with category toggles + `WX_HTML_REPORT_PANEL` |
| M8.3 | Per-board `Update PCB from Schematic` also pulls MBS cross-board nets in (with banner notice). MBS toolbar `Sync to PCB` retained for bulk N-board push. |
| Cross-cutting | Auto-disambig (`_<digits>`) + `{slash}` escape normalisation everywhere a net name is compared |
| Cross-cutting | Cross-board net extractor uses union-find (joins pins by local net name within a block AND by MBS wire) |
| Cross-cutting | Net-highlight propagation works whether or not MBSCH is open (sender-side file-based fan-out via `MultiBoardCollectCrossBoardProbesForLocalNet`) |
| Cross-cutting | Peer-mail project scope filter on `MAIL_SCH_GET_NETLIST` / `MAIL_PCB_UPDATE` / etc. |
| Cross-cutting | Module pin properties dialog (notebook with General + Pin Functions tabs); Open button spawns peer SCH editor |

### Code-read but not runtime-verified

- **Cross-board DRC engine** (`pcbnew/drc/drc_engine_cross_board.{h,cpp}`):
  `CheckConnectorMatching` partial; `CheckSignalIntegrity` and
  `CheckPowerDistribution` are TODO stubs. Engine still iterates
  legacy `BOARD_INFO` instead of `GetSubProjects()`. M8.4's binding
  check sidesteps this engine entirely; the engine remains for the
  pairwise N-board cross-checks it was designed for.

### Incomplete or fragile

- **Directory-walk container lookup** (M5.2). Used by:
  `eeschema/tools/sch_editor_control.cpp::RefreshMbsFromSubProjects`,
  `eeschema/files-io.cpp::syncCrossBoardNetsIfMbs`,
  `pcbnew/netlist_reader/board_netlist_updater.cpp::lookupCrossBoardNet`,
  and the new probe collectors in `multi_board_scan.cpp`. Replace
  with explicit parent reference.
- **Text-level PCB sync** (`common/project/cross_board_pcb_sync.cpp`)
  is regex-based; fragile on escaped characters and non-standard
  whitespace. M5.3 replaces with `PCB_IO_KICAD_SEXPR::LoadBoard`-based
  edits.
- **M4.2 sweep**: ~50–100 shallow `Prj()` call sites in dialogs
  and tools still resolve via the global. Works for single-project
  use; cross-talks across multiple projects.

---

## Remaining work

Items in priority order. Items marked **(separate agent)** are
parallelized — don't duplicate effort.

### P0 (gating user workflows)
None currently open.

### P1 (important for parity)

- **M5.5 — Port `DRC_ENGINE_CROSS_BOARD` to container model**
  (~1 session). Replace `GetBoardInfos()` iteration with
  `GetSubProjects()` + `GetCrossBoardNets()` so the engine can
  actually run on multi-board projects. Unlocks M5.6.
- **M5.6 — Flesh out cross-board DRC stubs** (~2-3 sessions).
  `CheckSignalIntegrity` (length, impedance) and
  `CheckPowerDistribution` (min pins for power nets, current capacity,
  voltage drop). Depends on M5.5.
- **M6 (3D viewer)** *(separate agent)*. M6.D mating refinements,
  M6.E real-geometry collision, M6.F STEP assembly export, M6.G
  persistence + polish.
- **M7.1 — Container library architecture** *(separate agent)*.
  Physical replication model (see locked-in decision #8). Pieces:
  - **Foundation** ✓ landed. `PROJECT::GetContainerProjectPath()` walk-up
    resolver + `PROJECT::GetContainerProject()` peek into SETTINGS_MANAGER.
    `include/project.h`, `common/project.cpp`. No auto-load — pure lookup.
  - **M7.1.A — Replication core**. Add `bool shared` and `bool conflict`
    flags to `LIBRARY_TABLE_ROW` (with serialization + grammar tokens).
    No new `LIBRARY_TABLE_SCOPE` value — that enum is a filter scope
    over physical files; "container scope" is purely a save-target
    choice in the UI layer (M7.1.C) that translates to
    `LIBRARY_MANAGER::AddSharedLibrary`. New manager methods:
    `AddSharedLibrary` / `RemoveSharedLibrary` / `UnshareLibraryRow`
    that fan out to container + every sub-project's lib-table on
    disk. Reconciliation hook in `LoadProjectTables`: parse
    `<container>/sym-lib-table` raw (no PROJECT load), sync `shared`
    rows in/out, mark `conflict=true` on nickname collisions. Files:
    `include/libraries/library_table.h`, `library_table_grammar.h`,
    `library_table_parser.h`, `library_manager.h`,
    `common/libraries/library_table.cpp`, `library_table_parser.cpp`,
    `library_manager.cpp`.
  - **M7.1.C — Save-to-library scope selector**. Third radio
    (Project / Container / Global) in symbol/footprint save dialogs.
    Default = Container in MBSCH, Project in sub-project editors.
    Hide CONTAINER option when `GetContainerProjectPath().IsEmpty()`.
    Info banner in lib-management dialog when in a multi-board
    context: *"Libraries added with scope 'Container' are replicated
    to all sibling boards."*
  - **Collision rendering**. When replication would create a row whose
    nickname clashes with an existing local (`shared=false`) row in a
    sub-project, write the shared row with a `conflict=true` marker
    and surface it in the lib-table UI like an unresolvable library
    (warning icon + clickable error explaining the rename). User
    resolves by renaming either side. The `conflict=true` row never
    serves lookups — the local row wins by being non-conflict.
  - **Deletion semantics.** Removing a row at container scope cascades
    to all sub-projects. A user editing a sub-project's lib-table
    directly and removing a `shared` row only **unshares** for that
    board (clears `shared=true`, leaves the row local). To actually
    delete from all peers, the user goes through the container.
- **M7.2 — Container settings propagation** *(separate agent)*.
  Investigation landed below; concrete implementation pending user
  pick on scope + model.

  **Investigation findings (2026-04-28).** KiCad's settings are split
  across three tiers with the following call-out:

  - **USER tier** (`~/.config/kicad/`): `kicad_common.json`,
    `eeschema.json`, `pcbnew.json`, `colors/*.json`, `hotkeys.json`,
    `toolbars/*.json`. *Already global per user* — applies to every
    project the user opens. The plan-goal items "color theme, grid
    defaults, hotkeys, eeschema/pcbnew prefs" all live here, so they
    need no container-level work.
  - **PROJECT tier** (`.kicad_pro`): per-project. Fields vary in
    sharing value:
    - **Genuine candidates for container sharing:**
      - `net_settings` (nested) — net classes, default widths, custom
        DRC rules. **Highest value** for multi-board: today, each
        sub-project has its own net classes, which means cross-board
        nets can carry inconsistent rules across the boundary.
      - `pcbnew.page_layout_descr_file` — title-block template path.
        Useful for consistent branding/border across all sub-boards.
      - `board.layer_presets`, `board.layer_pairs` — saved layer view
        configurations. Optional convenience.
    - **Not worth sharing:** `libraries.pinned_*` (per-user UI state),
      `pcbnew.last_paths.*` (recent dirs), `text_variables` (often
      per-board), `board.ipc2581.*` (export metadata, board-specific),
      `schematic.bus_aliases` (per-schematic).
  - **PROJECT_LOCAL tier** (`.kicad_prl`): per-project, per-machine —
    file histories, last-used UI states. *Not* shared.

  **Recommended scope for v1.** Just `net_settings`. It's the only
  field where the cross-board correctness story actively needs
  consistency (cross-board nets connect pads on different boards;
  divergent net classes silently produce different DRC outcomes on
  each side). Page-layout and layer presets are nice-to-haves and
  can land later behind the same mechanism.

  **Two viable models** (decision needed before implementing):

  - **Model A — Physical replication** (mirrors M7.1.A). Container's
    `net_settings` is the source of truth. On container save,
    `net_settings` is copied into each sub-project's `.kicad_pro`,
    flagged similarly to `shared=true` on lib-table rows. On
    sub-project load, reconcile against the container.
    *Pro:* sub-projects stay individually openable with full design
    rules.
    *Con:* `net_settings` is a large structured object — drift risk
    is higher than for lib-table rows; merge semantics with local
    sub-project classes are non-trivial.

  - **Model B — Runtime overlay** (no replication). Sub-project's
    `.kicad_pro` carries an opt-in flag `multi_board.inherit_net_settings:
    true`. When set, `PROJECT_FILE::NetSettings()` returns the
    container's `net_settings` instead of its own. Standalone open
    (no container present) falls back to the sub-project's own
    `net_settings`.
    *Pro:* zero drift, simple semantics, smaller code surface.
    *Con:* sub-project distributed without the container loses the
    inherited rules — opens with defaults until reconnected to a
    container with matching nicknames.

  Recommendation: **Model B** (overlay). Net classes are referenced
  by *name* on each board, so distributing a sub-project standalone
  is unlikely to break — the symbols still carry their net-class
  assignments by name; the user just sees default rule values until
  they reconnect to a container or copy the rules locally. This
  matches the plan's locked-in #8 spirit (sub-projects individually
  openable) without paying the replication tax for a structurally
  large field.

  **Files (Model B sketch):**
  - `include/project/project_file.h` — add
    `bool m_inheritNetSettingsFromContainer`; `multi_board.inherit_net_settings`
    param.
  - `common/project/project_file.cpp` — `NetSettings()` accessor
    consults `PROJECT::GetContainerProject()`'s net_settings when the
    flag is set + container is loaded; falls back otherwise.
  - UI: checkbox in net classes / DRC settings dialog when project
    has a container, default off, label "Inherit from multi-board
    container."

  **Pending user decision before code lands:** confirm Model B (vs
  A) and confirm scope (`net_settings` only vs broader).

### P2 (mechanical / polish)

- **M5.2 — Replace directory-walk lookup with explicit parent ref**
  (~1 session). Single token in `.kicad_mbs` + a small refactor of
  the four callers listed above. Eliminates the 6-level guess.
- **M5.3 — Replace text-level PCB sync with BOARD-level edits**
  (~1-2 sessions). `PCB_IO_KICAD_SEXPR::LoadBoard` round-trip; reuses
  `ApplyCrossBoardNetsToBoard` (M8.3) but operating on a transient
  in-memory load.
- **M5.4 — Comment sweep** (~30 min). Remove stale `.kicad_multi` /
  `MULTI_BOARD_PROJECT` mentions in comments only; code paths
  already migrated.
- **M8.0 — Shared iteration helper for module-block traversal**
  (~half-session). New `RECURSE_INTO_MODULE_BLOCKS` flag (or a
  `RunOnModuleBlocks` helper analogous to `RunOnChildren`) on the
  iteration utilities used by the surfaces in M8.1/M8.2. Most surface
  fixes become 1-line callsite swaps once this lands.
- **M8.1 — Schematic editor surfaces with confirmed gaps** (~3-4
  sessions, opportunistic):
  - ~~`widgets/net_navigator.cpp` (no `SCH_MODULE_PIN_T` handler)~~ ✓
  - `tools/sch_find_replace_tool.cpp` (no descent into module blocks)
  - `annotate.cpp` (collision check doesn't span module blocks)
  - `widgets/hierarchy_pane.cpp` (sheet-only tree)
  - `dialog_symbol_fields_table.cpp` (excludes module-block contents)
  - `netlist_exporters/*` (Orcad/SPICE/etc. omit cross-board nets)
  - `bom_plugins.cpp` (no cross-board connector mapping)
  - `sch_plotter.cpp` (module blocks not rendered in plot)
  - `dialogs/dialog_symbol_properties.cpp` (no cross-board indicator)
- **M8.2 — PCB editor surfaces with confirmed gaps** (~2-3 sessions):
  - ~~`widgets/pcb_net_inspector_panel.cpp` — Cross-Board column~~ ✓
    (showing aggregation with sibling-board names is a v2 follow-up)
  - `drc/drc_test_provider_matched_length.cpp` + router (length calc
    doesn't account for cross-board connector segments)
  - Differential-pair finder (same boundary issue)
  - Footprint editor cross-reference (no "this connector mates with X"
    info)
  - `exporters/place_file_exporter.cpp` (no cross-board de-dup)
  - `dialogs/dialog_pad_properties_base*` (no cross-board indicator)
- **M8.5 (icons) — Visual polish** (~1 session). New MBS file-format
  icon, dedicated icons for the MBS toolbar's Refresh / Manage
  Boards / Sync to PCB actions.
- **Properties dialog wxFormBuilder rewrite** (~30 min). Currently
  hand-coded; standard pattern uses a `.fbp` + matching `_base`
  class.
- **M9 — Agent integration** *(separate agent)*. Kipy bindings for
  container + module-block; tool schemas in `agent/tools/`;
  IPC handlers in `eeschema/api/` and `pcbnew/api/`.
- **Headless schematic sync** (~1-2 sessions). Today
  `PCB_EDIT_FRAME::TestStandalone` opens / spawns a project-matched
  SCH frame before fetching a netlist. A frame-less path would load
  the `.kicad_sch` headlessly via `SCH_IO_KICAD_SEXPR`, build a
  transient `SCHEMATIC`, and run `NETLIST_EXPORTER_KICAD` directly
  — eliminating the schematic-frame side effect of every PCB sync.
  Requires moving the netlist exporter behind a kiface entry point
  callable from pcbnew, or duplicating the loader path in pcbnew.
- **M3 — Launcher redesign**. wxAuiManager center+sidebar layout,
  multi-board-aware actions visible only when
  `IsMultiBoardContainer()`. Most launcher work landed; remaining
  scope unclear — verify against current launcher state before
  scoping further.

---

## Open risks

### R1 — Untested cross-board DRC engine
The unwired `DRC_ENGINE_CROSS_BOARD` may have rotted since M1. Port
in M5.5 will need to fix any stale signatures from the data-model
migration.

### R2 — 3D renderer divergence
Extending `RENDER_3D_BASE` for assembly mode could regress
single-board rendering in pcbnew. Mitigation: parallel code path
gated by mode, not a refactor of the single-adapter API. Test the
single-board 3D viewer against demo projects after every M6.C
follow-up.

### R3 — STEP export performance
Multi-board STEP export composes N per-board exports. Each board
takes seconds. Mitigation: progress reporter from the start;
parallelise per-board export if possible.

### R4 — Sub-project board loader cache invalidation
Loaders return owner-managed `std::unique_ptr<BOARD>` with no
shared cache; a sub-board edited in a peer PCB editor must not
appear stale to the 3D viewer or DRC engine. Mitigation: each
caller caches its own copy; peer save broadcasts a
`MAIL_RELOAD_SUB_PROJECT` to invalidate.

### R5 — Container project lifetime under M7.1 ✓ resolved
Per locked-in decision #8, M7.1 does *not* auto-load the container
PROJECT into `SETTINGS_MANAGER`. Container lib-table files are read
directly during sub-project reconciliation; the container `PROJECT*`
is only loaded when the user explicitly opens the container
manager. No refcount needed. M7.2 (settings overlay) will follow the
same pattern — load the container's settings as a JSON overlay, not
as a managed PROJECT.

---

## Phase reference

Phase IDs survive in commit messages and across this doc. Quick
mapping:

| Phase | Topic |
|---|---|
| M0–M2 | File unification, `.kicad_mbs`, MBSCH frame |
| M3 | Launcher (mostly landed) |
| M4 | Concurrent peer editors |
| M5 | Cleanup + cross-board DRC engine port |
| M6 | 3D assembly viewer |
| M7 | Project-level integration (libraries, settings) |
| M8 | Editor UI surface integration |
| M9 | Agent integration |
