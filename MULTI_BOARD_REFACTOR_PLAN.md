# Multi-Board Architecture Refactor — Phase M

Living plan for upgrading Zeo (KiCad fork) to support Altium-style
multi-board development: container projects, dedicated MBS editor,
cross-board nets, concurrent multi-editor work, and 3D assembly viewer.

**Status (2026-04-26): ~70% done.** Foundation, MBS editor, cross-probe,
launcher, peer editors, and the OpenGL 3D assembly compositor are
landed. The finish-line section below lists the remaining ~30%
in priority order.

---

## Finish line — remaining 30%

Organized by area then priority. **P0** = blocks user workflows,
**P1** = important for parity, **P2** = polish / nice-to-have.

### MBS editor — broken edit operations (P0, do first)

User-confirmed regressions / gaps when interacting with module blocks
in the MBS editor. Each is a missing `case SCH_MODULE_BLOCK_T:` in a
tool handler that already handles `SCH_SHEET_T`. Detail in M8.6.

| Item | File | Notes |
|---|---|---|
| Delete on a module block does nothing | `eeschema/sch_collectors.cpp` | Add `SCH_MODULE_BLOCK_T` to `DeletableItems` |
| Rotate / Mirror are no-ops | `eeschema/tools/sch_edit_tool.cpp` | Mirror SCH_SHEET case; block has Rotate / MirrorH/V overrides ready |
| No drag-corner resize | `eeschema/tools/sch_point_editor.cpp` | Add `MODULE_BLOCK_POINT_EDIT_BEHAVIOR` mirroring `SHEET_POINT_EDIT_BEHAVIOR` |
| Moving block breaks wire on the *other* end | `eeschema/tools/sch_move_tool.cpp` | Extend `m_specialCaseSheetPins` to include module pins |
| Wire requires double-click to terminate on module pin | trace through `SCH_LINE::CanConnect` + autostart detection | Earlier fix may have regressed; reproduce + verify |

Estimate: ~1 session, all five together (same pattern repeated). Lands
visible-impact fixes for the MBS editor.

### Sub-project ERC / DRC noise suppression (P0)

Without these, sub-project ERC and DRC are unusable on multi-board
projects — every cross-board connector pin / pad lights up red. Detail
in M8.9.1 / M8.9.2.

| Item | File | Notes |
|---|---|---|
| ERC suppress no-driver / single-driver / floating-input on cross-board pins | `eeschema/erc/erc.cpp` + test providers | Consult `Prj().GetContainerProject()->GetCrossBoardNets()` |
| DRC suppress single-pad-net / unconnected on cross-board pads | `pcbnew/drc/drc_test_provider_*.cpp` | Wire up the existing `CONNECTIVITY_DATA::IsCrossBoardConnectorPad` (currently dead code at `connectivity_data.cpp:1255`) |

Estimate: ~1-2 sessions. The DRC half is mechanical (just wire up an
existing helper). The ERC half needs a small new check provider.

### Cross-board verification (P1)

The "real" multi-board ERC and the one new DRC check.

| Item | Where | Notes |
|---|---|---|
| Cross-board ERC rules (M5.8) | new check provider in `eeschema/erc/` | Validates boundary pins: unconnected, driver conflicts, type mismatch, pin-count match. Runs from MBS editor. |
| Binding DRC check (M8.4) | new check provider in `pcbnew/drc/` | Verifies each connector pad's net matches the MBS-declared net for that block pin. Only fires when project has a container. |
| DRC port to container model (M5.5) | `pcbnew/drc/drc_engine_cross_board.cpp` | Engine still iterates legacy `BOARD_INFO`; port to `GetSubProjects()` + `GetCrossBoardNets()` so M5.1 loader fallback becomes useful and `CheckConnectorMatching` actually runs. |
| Flesh out DRC stubs (M5.6) | `drc_engine_cross_board.cpp` | `CheckSignalIntegrity` and `CheckPowerDistribution` are TODOs. |

Estimate: ~3-4 sessions total.

### 3D viewer — finish what's started (P1)

OpenGL composition for translation-only is landed (M6.C-phase-1).
What remains:

| Item | What | Notes |
|---|---|---|
| Rotation / flip in pose matrix (M6.C-phase-2 #1) | `3d_viewer_assembly.cpp::RedrawAll` | Currently translation-only; ignored rotation in `BOARD_3D_INSTANCE`. Two-line change + lighting verify. |
| Connector mating refinements (M6.D) | `MateConnectors` / `CalculateMatingOffset` | Real pad-normal alignment, board-flip respect, component-height-aware Z gap. Highest visible-feature ROI. |
| Real-geometry collision (M6.E) | new — broad / narrow phase per-component | Today is AABB board-outline only. Use `S3D_CACHE` meshes with per-component AABB then OBB / GJK. |
| STEP assembly export (M6.F) | finish stub `ExportAssemblySTEP` | Compose per-board STEP shapes into a STEP compound with `TopLoc_Location`. |
| Persist assembly state (M6.G) | `multi_board.assembly_3d` in container `.kicad_pro` | Per-instance transform + visibility, key by sub-project UUID. |
| Raytracer multi-instance (M6.C-phase-2 #2) | `create_scene.cpp` | Today the raytracer falls through to single-board. Scene-graph composition. Lowest-priority of these. |
| Camera auto-frame on open | wire `GetAssemblyBoundingBox()` into initial camera fit | Polish. |

Estimate: M6.D is ~1 session, M6.E is ~1-2, M6.F is ~1-2, M6.G is ~1.
~4-6 sessions total for the visible features (D/E/F/G); raytracer +
model-cache dedup + parallel load are deferrable.

### Project-level integration (P1)

Cross-cutting work that gives the container project-level *substance*.
Detail in M7.

| Item | What | Notes |
|---|---|---|
| Container library (M7.1) | New container-tier in `LIBRARY_MANAGER`; lookup precedence GLOBAL → CONTAINER → PROJECT; new `GetContainerProject()` on PROJECT | Lib added in MBSCH visible to every sub-project |
| Settings propagation (M7.2) | Parallel to M7.1: container `settings.json` overlay between global and project | Reuses `GetContainerProject()` from M7.1 |

Estimate: M7.1 ~3 sessions (incl. dialog UI for scope selector). M7.2
~2 sessions after pre-impl investigation.

### Editor UI integration (P2 — broad but mechanical)

Catalog of surfaces with confirmed gaps. Pattern in each: extend the
iterator that walks `SCH_SHEET` hierarchies / iterates per-screen
items to also recognize module blocks and cross-board nets. Detail in
M8.0–M8.2.

- **Pre-work (M8.0):** add a `RECURSE_INTO_MODULE_BLOCKS` flag /
  `RunOnModuleBlocks` helper to iteration utilities. Most items below
  become 1-line callsite swaps once this lands.
- **SCH (M8.1):** Net Navigator, Find/Replace, Annotation, Hierarchy
  pane, Symbol Fields Table, Netlist exporters, BOM, Plotter, Symbol
  Properties dialog (10 surfaces total).
- **PCB (M8.2):** Net Inspector, Length tuner, Diff pair finder,
  Footprint editor xref, BOM/PnP, Pad properties (6 surfaces).

Estimate: ~3 sessions for M8.0; surfaces tackled opportunistically in
~1 session per pair.

### UX polish (P2)

| Item | Notes |
|---|---|
| Refresh dialog redesign (M8.5) | Settings panel + `WX_HTML_REPORT_PANEL` console, modeled on `dialogs/dialog_update_from_pcb.*`. Replaces flat checklist. |
| Fold MBS push-sync into SCH→PCB sync (M8.3) | Removes the standalone "Sync to PCB" toolbar button; adds detection in `dialog_update_pcb.cpp` |
| MBS file-format icon + Refresh / Manage Boards toolbar icons | Bundle of small visual items |
| MBS editor drag/move feel | Snap behavior, label reflow when pins added |

Estimate: ~2 sessions total for everything in this bucket.

### Cleanup (P2)

| Item | What |
|---|---|
| M5.2 Replace directory-walk MBS lookup | Store explicit parent-project ref in `.kicad_mbs` instead of walking up 6 levels |
| M5.3 Replace text-level PCB sync | BOARD-level edits via `PCB_IO_KICAD_SEXPR::LoadBoard()` instead of regex |
| M5.4 Comment sweep | Remove remaining `.kicad_multi` / `MULTI_BOARD_PROJECT` mentions in comments |
| M4.2 Global-`Prj()` sweep | ~50-100 shallow call sites in dialogs/tools still hit the global; cross-talks under multi-project |

Estimate: ~2 sessions, opportunistic.

### Agent integration (P2 — separate workstream)

Goal: agent can drive multi-board workflows end-to-end. Detail in M9.

- New Kipy bindings for container, `SCH_MODULE_BLOCK`, refresh, push-sync
- Tool schemas + Python handlers in `agent/tools/`
- IPC handlers in `eeschema/api/` and `pcbnew/api/`

Estimate: ~3-4 sessions; deferable until everything else stabilizes.

### Confirmed *not* on the finish line

User confirmed 2026-04-26 these are already addressed and don't need
further work:
- M8.9.3 (net classes for cross-board nets)
- M8.9.4 (visual annotation on connector pins/pads)
- M8.9.5 (net naming reconciliation)
- M8.9.6 (highlight-net cross-editor propagation)
- M8.9.7 (save/load round-trip — verified working)

### Suggested order

1. **Week 1:** P0 items (MBS edit ops + ERC/DRC suppression). Restores
   user trust in the MBS editor and clears noise from sub-project
   verification.
2. **Week 2:** Cross-board verification (M5.8 ERC + M8.4 binding DRC +
   M5.5/M5.6 DRC port + stubs). Closes the verification story.
3. **Week 3:** 3D viewer M6.D + M6.E (mating + collision). Highest
   Altium-parity visible feature.
4. **Week 4:** 3D viewer M6.F + M6.G (STEP export + persistence).
5. **Week 5:** M7.1 container library.
6. **Week 6:** M7.2 settings + M8.3 fold sync + UX polish bundle.
7. **Week 7:** M8.0/M8.1/M8.2 editor UI integration.
8. **Week 8+:** M9 agent integration + cleanup.

~6-8 weeks to feature-complete. Compressible if 3D viewer and library
work parallelize across two contributors.

---

## Current state (2026-04-26)

### Landed + runtime-verified
- Container topology unified under `PROJECT_FILE::multi_board`:
  `multi_board.container` flag, `sub_projects[]` (SUB_PROJECT_INFO),
  `cross_board_nets[]` (MB_CROSS_BOARD_NET), `mbs_file`. Legacy
  `BOARD_INFO` / `CROSS_BOARD_CONNECTION` / `COMPONENT_BOARD_ASSIGNMENT`
  arrays preserved for the tested single-project multi-PCB modules.
- `MULTI_BOARD_PROJECT` class + `.kicad_multi` extension **removed**.
- `.kicad_mbs` extension registered (`FILEEXT::MbsFileExtension`).
- `FRAME_MBSCH` + `MBSCH_EDIT_FRAME` (subclass of `SCH_EDIT_FRAME`,
  lives in `eeschema/`). Dispatches through `FACE_SCH`. Trimmed menubar,
  custom cross-probe handling, saves trigger MBS net extraction.
- Sub-project scanning (`multi_board_scan.h`) for connectors + pads.
- MBS rendering via `SCH_MODULE_BLOCK` / `SCH_MODULE_PIN`.
- Wire-based union-find cross-board net extraction (eeschema).
- Text-level PCB net sync with reverse propagation + conflict detection.
- MBS save hook triggering extraction + sync.
- Multi-board netlist updater (exercised via normal workflow).
- Component assignment manager (90% functional).
- **M4 peer editors**: `KIWAY_HOLDER::SetPrjOverride`,
  `RegisterPeerPlayer`, `SpawnPeerSchematicEditor`, File → Open
  Sub-Board Schematic in New Window, peer lifecycle + ExpressMail
  broadcast to all peers of a given FRAME_T.

### Landed since 2026-04-23

- **MBS-scoped annotation** (B1/B2/…) — auto-assigned during refresh,
  serialized via `(mbs_reference "B1")` token in `.kicad_mbs`. Painter
  shows `B1 — fc/J2` header format; falls back to legacy `Module: name`
  when ref unset. `nextMbsReference(screen)` helper in
  `multi_board_mbs_refresh.cpp` finds first free slot.
- **Cross-probe sender-scoped highlight** — `KIWAY_MAIL_EVENT.GetEventObject()`
  cast to `EDA_BASE_FRAME` in MBSCH `KiwayMailIn` to identify the sender
  project; explicit `$PROJECT:` token wins over sender path so
  ambiguous refs (two boards both have J2) resolve correctly.
- **Refresh-time view sync** — `ApplyMbsRefreshChanges` now takes a
  `KIGFX::VIEW*` and calls `view->Add/Remove/Update` for every
  add/remove/rename/add-pin path. Fixes the post-refresh "blocks
  disappear until reopen" symptom (RTree mutated without view
  notification).
- **Empty-block auto-sweep** — at the end of every
  `ApplyMbsRefreshChanges`, any block with `GetPins().empty()` is
  removed from screen + view. Cleans up orphans from prior sessions
  and from refreshes where pin removals leave a block bare.
- **Selection (lasso + click)**:
  - `SCH_MODULE_BLOCK::Visit` override surfaces child pins to the
    inspector (mirrors `SCH_SHEET::Visit`); pins now reach the
    selection collector at all.
  - `SCH_MODULE_BLOCK::HitTest(SHAPE_LINE_CHAIN, bool)` override added
    via `KIGEOM::BoxHitTest`; lasso (poly) selection now picks up
    blocks alongside their pins. Box (rect) selection already worked
    via the existing `HitTest(BOX2I, bool)` override.
  - `SCH_SELECTION_TOOL::SelectMultiple` lasso branch added for
    `SCH_MODULE_BLOCK` mirroring the `SCH_SHEET` branch.
  - `itemPassesFilter`: `SCH_MODULE_BLOCK_T` routes through the
    "symbols" filter, `SCH_MODULE_PIN_T` routes through the "pins"
    filter. Was falling to `default → otherItems`.
  - `GuessSelectionCandidates`: `SCH_MODULE_PIN_T` treated as
    exact-priority hit alongside `SCH_PIN_T` / `SCH_SHEET_PIN_T` so
    pin-on-block-edge clicks select the pin, not its parent block.
  - `SCH_LINE::CanConnect`: `SCH_MODULE_PIN_T` added to the wire
    endpoint whitelist; wires now terminate on a module pin in one
    click instead of needing a double-click.
- **Cross-probe self-loop fix** — two related bugs that together
  collapsed multi-item MBS selections to a single item:
  - `MBSCH_EDIT_FRAME::crossProbeHighlightPart` was calling
    `selectionClear` on every invocation, defeating the multi-spec
    batching in `KiwayMailIn`. Removed; the loop's `cleared` flag is
    the sole clear point now.
  - `SCH_EDIT_FRAME::SendSelectItemsToPcb` was fanning `$SELECT` to
    `FRAME_MBSCH` even when the sender already was MBSCH, so the
    packet round-tripped through the MBSCH's own KiwayMailIn. Gated
    on `GetFrameType() != FRAME_MBSCH`.
  - Net effect before fix: lasso showed pins highlight one-by-one and
    only the last survived; shift+click failed identically. Both work
    now.
- **`SCH_SCREEN::Append/clear` filters** — `SCH_MODULE_PIN_T` excluded
  from the rtree (parent block owns lifetime) alongside
  `SCH_SHEET_PIN_T` / `SCH_FIELD_T`. Defensive.

### Landed but code-read only (never exercised at runtime)
- **3D assembly viewer skeleton** — `ASSEMBLY_3D_MANAGER`
  (`3d-viewer/3d_viewer/3d_viewer_assembly.{h,cpp}`) and
  `PANEL_3D_ASSEMBLY` (`3d-viewer/dialogs/panel_3d_assembly.{h,cpp}`).
  Compiled into the 3d-viewer library but **never instantiated or
  referenced from `EDA_3D_VIEWER_FRAME`**. Uses the legacy `BOARD_INFO` /
  `CROSS_BOARD_CONNECTION` data model — has not been ported to the
  container topology. STEP export is a stub. See Phase M6.
- **Cross-board DRC** (`pcbnew/drc/drc_engine_cross_board.{h,cpp}`) —
  `CheckConnectorMatching` partially implemented; `CheckSignalIntegrity`
  and `CheckPowerDistribution` are TODOs. Blocker:
  `DRC_ENGINE_CROSS_BOARD::GetBoardByUuid()` returns `nullptr`.

### Incomplete or fragile
- **Stale `.kicad_multi` / `MULTI_BOARD_PROJECT` references in comments
  only** — files need a comment-only sweep:
  `include/project/cross_board_pcb_sync.h:56`,
  `kicad/kicad_manager_frame.h:168,177`,
  `kicad/dialogs/dialog_multi_board_setup.h:45,47,61`,
  `kicad/tools/kicad_manager_control.cpp:338-341` (legacy-stub cleanup,
  already code-wise correct but mentions the old extension),
  `eeschema/multi_board_net_extractor.cpp:37`,
  `eeschema/multi_board_mbs_refresh.cpp:39`,
  `eeschema/tools/sch_editor_control.cpp:621`,
  `pcbnew/netlist_reader/board_netlist_updater.{h:150,160; cpp:1176}`.
- **Directory-walking MBS lookup** (`sch_editor_control.cpp:~621`,
  `files-io.cpp::syncCrossBoardNetsIfMbs`) walks up 6 levels for the
  enclosing container `.kicad_pro`; picks first found, breaks on deeper
  nesting. Replace with an explicit parent-project reference stored in
  the `.kicad_mbs` file.
- **Text-level PCB sync** is regex-based; fragile on escaped chars /
  non-standard whitespace. Replace with BOARD-level edits via
  `PCB_IO_KICAD_SEXPR::LoadBoard()`.
- **Shared sub-project board loader missing**: `GetBoardByUuid()` is
  stubbed to nullptr in `DRC_ENGINE_CROSS_BOARD` and to active-only in
  `MULTI_BOARD_NETLIST_UPDATER`. The 3D viewer will need the same
  loader — this is the Phase M6 precursor that unblocks M5 DRC
  validation too.
- **M4 sweep incomplete**: ~50-100 shallow `Prj()` call sites in dialogs
  and tools still resolve via the global. Works for single-project use,
  cross-talks when multiple projects are loaded.

### Kiway constraint (unchanged)
`KIWAY::Prj()` still delegates to `SETTINGS_MANAGER::Prj()`, which
returns `*m_projects_list.begin()->get()`. Per-frame resolution is
handled by `KIWAY_HOLDER::SetPrjOverride` (M4.2). The sweep to remove
remaining global lookups is M4.2's optional cleanup phase.

---

## Phase M0 — Pre-work ✓ done
Branch snapshot tag + decision to discard test projects (no migration).

## Phase M1 — File unification ✓ done
Full port of the container model to `PROJECT_FILE::multi_board`. Class
`MULTI_BOARD_PROJECT` and extension `.kicad_multi` are retired. Caller
sweep completed (all active code uses `Prj().GetProjectFile()` +
`IsMultiBoardContainer()`). Comment-level cleanup still pending
(see Current state → Incomplete).

## Phase M2 — `.kicad_mbs` format + MBS editor ✓ done (as subclass)

**Chose subclass over separate kiface.** `MBSCH_EDIT_FRAME` is a
`SCH_EDIT_FRAME` subclass in `eeschema/` (not a dedicated
`mbschema/` kiface DSO). `FRAME_MBSCH` dispatches through `FACE_SCH`.

Rationale: `SCH_MODULE_BLOCK`, `SCH_MODULE_PIN`, painter, parser,
connectivity, and the MBS net extractor already live in the eeschema
kiface. A separate DSO would have forced a three-way split of that
code without isolating any runtime state. The subclass pattern keeps
the MBS feature surface trimmed (menubar, cross-probe, save hook) while
sharing implementation with full eeschema.

**Landed:**
- `FILEEXT::MbsFileExtension = "kicad_mbs"`.
- `FRAME_MBSCH` in `frame_type.h`; `KifaceType(FRAME_MBSCH) → FACE_SCH`.
- `eeschema/mbsch_edit_frame.{h,cpp}` — overrides `onSchematicSaved`,
  `doReCreateMenuBar`, `KiwayMailIn`.
- `eeschema/eeschema.cpp::CreateKiWindow` handles `FRAME_MBSCH`.

**Deferred** (may never be needed, revisit only if runtime state
isolation becomes necessary):
- Split into `mbschema/` kiface DSO.

## Phase M3 — Launcher redesign (~2 days, low risk) — pending

Unchanged from prior plan. `wxAuiManager` in `KICAD_MANAGER_FRAME`,
project tree center pane, sidebar left pane with action buttons,
multi-board-aware actions (Edit MBS / Manage Sub-Boards / Sync / 3D
Assembly) shown only when `IsMultiBoardContainer()`, sub-project nodes
rendered under the multi-board root.

Tree-pane rendering of sub-projects is the one active usability bug
blocking hands-on M1 validation; everything else is polish.

## Phase M4 — Concurrent editor instances — MVP done

**Landed** (M4.0 spike + M4.1–M4.5 MVP, see 2026-04-20 status block
below for detail):
- Peer-player registration, broadcast ExpressMail, File → Open
  Sub-Board Schematic in New Window, duplicate protection, per-frame
  title from overridden `Prj()`.

**Still deferred** (M4.2 full cleanup, optional):
- Sweep remaining global `Prj()` call sites to frame-local.
- Per-frame library table caches.
- ExpressMail routed on (project, frame-type) tuple, not just frame type.

### Phase M4 status (2026-04-20) — preserved

**M4.0 spike succeeded**: opened a multi-board container with two
sub-boards, launched the "Open First Sub-Board Schematic (peer)"
action. Result:
- Fresh `SCH_EDIT_FRAME` alongside the project manager.
- Title / hierarchy / library lookups resolve to the sub-project via
  `SetPrjOverride`.
- Two concurrent `SCH_EDIT_FRAME`s viable in one process.

Root fix that unblocked the spike: C-style cast instead of
`dynamic_cast` when casting `CreateKiWindow`'s return value. The
eeschema kiface DSO has hidden typeinfo visibility; `dynamic_cast` from
the manager binary fails even though the hierarchy is correct.
Consistent with `Kiway::Player()`'s own pattern.

**M4.3 — Kiway peer-player support** ✓:
- `KIWAY::RegisterPeerPlayer`, `UnregisterPeerPlayer`,
  `GetAllPlayerFrames`.
- Mutex-protected `m_peerPlayerFrames`.
- `ExpressMail` broadcasts to primary + every peer.
- Single-instance editors see unchanged behavior.

**M4.4 — Sub-project open UX** ✓:
- Menu entry, duplicate protection, self-unregister on close.

**M4.5 — frame identity**: achieved via `Prj()` basename in title.

### Phase M1 status (2026-04-20) — superseded
All M1 items (M1.1–M1.5) landed between 2026-04-20 and 2026-04-23.

---

## Phase M5 — Non-3D cleanup + follow-ons (~2-3 days) — partially landed

### M5.1 ✓ landed (2026-04-23)

**Shared sub-project board loader** at
`pcbnew/multi_board/sub_project_board_loader.{h,cpp}`. Two free
functions: `LoadSubProjectBoard(PROJECT&, SUB_PROJECT_INFO)` and
`LoadSubProjectBoard(PROJECT&, KIID)`. Resolves via
`PROJECT_FILE::ResolveSubProjectPath` + `MultiBoardMainPcb`, loads via
`PCB_IO_KICAD_SEXPR::LoadBoard`, returns `std::unique_ptr<BOARD>`.
No shared cache (R9). Failures swallowed via `wxLogTrace MULTI_BOARD`.

Wired into both legacy stubs as a fallback path:
- `MULTI_BOARD_NETLIST_UPDATER::GetBoardByUuid` — first the cache,
  then the frame's active board (legacy single-project multi-PCB
  path), then the loader. Owns loaded boards in
  `m_loadedSubBoards`.
- `DRC_ENGINE_CROSS_BOARD::GetBoardByUuid` — first the cache, then
  the loader. Owns loaded boards in `m_loadedSubBoards`.

**Important caveat**: both engines still iterate
`PROJECT_FILE::GetBoardInfos()` (legacy single-project multi-PCB
model) at their call sites, so the loader fallback is currently
unreachable from existing callers — it's there for the 3D viewer
(M6.A onward) and for when those engines themselves are ported to
the container topology (separate task, not yet scoped).

### M5.2+ pending

- **M5.2 Replace directory-walk MBS lookup** with an explicit
  parent-project reference stored in the `.kicad_mbs` file
  (`(parent_project "...")` s-expr or in the `(title_block)` metadata).
  Updates `sch_editor_control.cpp:~621`,
  `files-io.cpp::syncCrossBoardNetsIfMbs`, and the netlist-updater
  container probe.
- **M5.3 Replace text-level PCB sync** with BOARD-level edits via
  `PCB_IO_KICAD_SEXPR::LoadBoard()`. Robust handling of quoted strings,
  whitespace, escape sequences.
- **M5.4 Comment sweep** — eliminate remaining `.kicad_multi` /
  `MULTI_BOARD_PROJECT` mentions in the files listed in Current state.
- **M5.5 Port DRC + netlist updater to container model** — both
  engines currently iterate the legacy `BOARD_INFO` array. Container
  projects leave it empty, so the engines silently no-op. Port to
  `GetSubProjects()` + `GetCrossBoardNets()` so the M5.1 loader
  fallback becomes useful and `CheckConnectorMatching` actually runs.
- **M5.6 DRC stubs investment** — flesh out `CheckSignalIntegrity` and
  `CheckPowerDistribution` once M5.5 is in place.
- **M5.7 Evaluate CONNECTION_GRAPH integration** — register
  `SCH_MODULE_BLOCK_T` / `SCH_MODULE_PIN_T`, retire the custom
  union-find extractor if the graph integration is cheap. ~4 hours if
  we go for it; skip if the graph API makes it harder.
- **M5.8 Cross-board ERC rules** — the real "multi-board DRC". Walk
  `multi_board.cross_board_nets` and emit ERC violations for:
  - Unconnected boundary pin (block pin not wired to anything)
  - Driver conflict on a cross-board net (two outputs facing each other)
  - Pin-type mismatch at the boundary (output ↔ output, power ↔ output)
  - Pin count mismatch on mating connector pairs
  - Voltage rail mismatch (block A drives 3.3V into B's 5V rail)
  - Pad-count match between paired connectors

  Mechanism: new ERC check provider that consumes
  `multi_board_net_extractor` output and emits violations through the
  existing ERC violation framework. Reuses the same dialog/report
  machinery as single-board ERC. Runs from the MBS editor.

  **Note on terminology.** What people loosely call "multi-board DRC"
  decomposes into three things, none of which is a new top-level DRC
  dialog:
  1. **MBS ERC** (this section, M5.8) — logical/electrical boundary
     checks. Lives in MBS editor.
  2. **Mechanical / collision** (M6.E) — geometric checks. Lives in
     3D viewer.
  3. **Cross-board net binding check** (M8.4) — single new check
     provider in pcbnew's existing DRC, only fires when project is
     multi-board. Verifies each connector pad on this board carries
     the net the MBS declares for the corresponding block pin.

  Per-board electrical/clearance DRC stays per-board, unchanged.

---

## Phase M6 — 3D Multi-Board Assembly Viewer

**Goal:** reach feature parity with Altium's MultiBoard Assembly
view — load every sub-board into a single 3D scene with per-board
transforms, snap mated connectors, detect mechanical collisions,
and export the whole assembly as a STEP compound.

### M6 current state

Two unwired classes exist:

- `ASSEMBLY_3D_MANAGER` (`3d-viewer/3d_viewer/3d_viewer_assembly.{h,cpp}`)
  — vector of `BOARD_3D_INSTANCE` (UUID + transform + visibility), FLAT /
  STACKED / CUSTOM layout modes, AABB-based collision check, stubbed
  STEP export (`ExportAssemblySTEP` returns false).
- `PANEL_3D_ASSEMBLY` (`3d-viewer/dialogs/panel_3d_assembly.{h,cpp}`)
  — wxPanel with board list, XYZ position / rotation controls,
  mate / collision / transparency / export buttons.

Both are in `CMakeLists.txt` but neither is referenced by
`EDA_3D_VIEWER_FRAME` or anything else in the tree. `ASSEMBLY_3D_MANAGER`
calls `PROJECT_FILE::GetBoardInfos()` and `GetCrossBoardConnections()`
— the legacy single-project multi-PCB model, not the container
topology.

### M6 rendering architecture (decision)

**Multiple `BOARD_ADAPTER`s, one per sub-board, composited in the
renderer with per-instance transforms.** (Approach "a" from the 3D
rendering audit.)

Why not the alternatives:
- **One super-adapter with an instance list**: violates the existing
  per-adapter geometry build; forces `InitSettings` to know about
  multiple boards.
- **Merged super-BOARD with offset footprints**: loses board isolation,
  breaks per-board edge outlines, and the design-settings / layer-stack
  divergence between sub-boards can't be represented.

Key facts from the audit that make approach "a" the right fit:
- `RENDER_3D_BASE` already takes `BOARD_ADAPTER&` — the plumbing is
  reference-based.
- OpenGL renderer iterates footprints at render time with per-footprint
  transforms (`render_3d_opengl.cpp:1050-1118`) — wrap the outer loop
  over instances, left-multiply the footprint matrix by the instance
  transform.
- Raytracer consumes prebuilt BVHs (`create_scene.cpp:347`) — instance
  loop composes per-board BVHs with transforms before stitching into
  the scene.
- 3D model cache (`S3D_CACHE`) is already shared across adapters.

### M6.A — Skeleton refactor to new MBS data model (~1 session)

Make `ASSEMBLY_3D_MANAGER` compile and function against the container
topology. No rendering changes yet.

1. `BOARD_3D_INSTANCE` (`3d_viewer_assembly.h:41`):
   - rename `boardUuid` → `subProjectUuid`;
   - add `wxString pcbFilePath` (absolute `.kicad_pcb` path, resolved
     via `PROJECT_FILE::ResolveSubProjectPath`);
   - `board` pointer becomes `std::unique_ptr<BOARD>` so the manager
     owns each loaded board's lifetime.
2. `ASSEMBLY_3D_MANAGER::LoadProjectBoards` (`.cpp:73`):
   - guard `Prj().GetProjectFile().IsMultiBoardContainer()`;
   - iterate `GetSubProjects()` instead of `GetBoardInfos()`;
   - for each sub-project, resolve its `.kicad_pro`, load it into a
     transient PROJECT_FILE probe to read the board filename, then
     load the sibling `.kicad_pcb` via the M5.1 helper.
3. `MateConnectors` (`.cpp:279`): switch from `CROSS_BOARD_CONNECTION`
   to `MB_CROSS_BOARD_NET`. For each net with ≥2 endpoints, resolve
   each `MB_CROSS_BOARD_NET_ENDPOINT` (subProjectUuid + componentRef +
   pinNumber) to a `PAD*` on the corresponding loaded `BOARD`.
4. `CalculateMatingOffset` (`.cpp:459`): replace pad-KIID lookup with
   `(componentRef, pinNumber)` lookup.
5. `PANEL_3D_ASSEMBLY`: no structural change; handle the async
   "boards still loading" state cleanly (the manager's board-load is
   sync for now but will become async in M6.E).

Deliverable: compiles; given a container project, `LoadProjectBoards`
loads all sub-project BOARDs and `MateConnectors` resolves endpoints
via the new model. No rendering yet. Depends on M5.1 shared loader.

### M6.B — Launch + single-board rendering on the assembly frame (~1 session)

Prove the manager / panel / frame wiring end-to-end before touching
the renderer.

1. `EDA_3D_VIEWER_FRAME` gains an "assembly mode" construction path:
   constructor variant taking a container `PROJECT*` (no parent
   `PCB_BASE_FRAME`). In this mode the frame owns an
   `ASSEMBLY_3D_MANAGER` and the existing `m_boardAdapter` points at
   the currently-selected instance's BOARD.
2. Wire `PANEL_3D_ASSEMBLY` into the frame's AUI layout (right pane
   alongside `APPEARANCE_CONTROLS_3D`). Panel's board-list selection
   calls `SetBoard()` on the adapter + `NewDisplay()`.
3. Menu entry in `KICAD_MANAGER_FRAME`: "Open 3D Assembly", visible
   only when `Prj().GetProjectFile().IsMultiBoardContainer()`. Launches
   via `Kiway().Player(FRAME_PCB_DISPLAY3D, …)` using the M4 peer-
   player machinery so it lives alongside any existing per-sub-board
   3D viewers.
4. Window identity: title includes container basename; peer-register
   so multiple 3D viewers can coexist in one process.

Deliverable: "Open 3D Assembly" from the manager opens a 3D viewer
that renders one sub-board at a time; the assembly panel switches
which board is active. Single-board render pipeline unchanged.

### M6.C — Multi-board composition in the renderer

#### M6.C-phase-1 ✓ landed (2026-04-23)

OpenGL-path compositing with translation-only per-instance transforms.

**What landed:**
- `ASSEMBLY_3D_MANAGER` gained per-instance ownership:
  `std::vector<std::unique_ptr<BOARD_ADAPTER>> m_instanceAdapters` +
  `std::vector<std::unique_ptr<RENDER_3D_OPENGL>> m_instanceRenderers`,
  aligned with `m_boardInstances`. New methods:
  `InitRenderers(canvas, camera, s3dCache)` — lazy per-instance build;
  `RedrawAll(isMoving, reporters)` — orchestrates the composite pass;
  `RequestReload()` — invalidates every per-instance renderer's caches.
- `RENDER_3D_OPENGL` gained two setters (default-off,
  single-board mode untouched):
  `SetAssemblyPose(glm::mat4)` — multiplied into the MODELVIEW after
  the camera view is loaded; `SetSkipBufferClear(bool)` — skips glClear
  of color/depth and the background gradient so subsequent instances
  composite onto the framebuffer.
- `EDA_3D_CANVAS::DoRePaint` routes through
  `m_assemblyManager->RedrawAll(...)` when a manager is set and the
  engine is OpenGL. Legacy single-board path unchanged when no manager.
  New setter: `SetAssemblyManager(ASSEMBLY_3D_MANAGER*)`.
- Assembly-mode frame constructor calls
  `m_canvas->SetAssemblyManager(m_assemblyManager.get())` after
  `setupFrame()`.
- macOS `gl.h`-ordering fix: `3d_viewer_assembly.cpp` pre-includes
  `<kicad_gl/kiglad.h>` to pre-empt system GL from being dragged in by
  later headers.

**Deliverable:** user opens a container in the manager → File → 3D
Assembly Viewer → every sub-board renders at its flat-layout
translation in one OpenGL scene. Visibility toggle skips a board's
render pass. Panel position edits update instance translation and the
next repaint shows the new position. 3D models load per-renderer
(duplicated cache — acceptable for phase 1; deduped in phase 2).

#### M6.C-phase-2 — deferred

1. **Rotation / flip** — translation-only in phase 1. `BOARD_3D_INSTANCE.rotation`
   is read but ignored by `RedrawAll`. Adding a rotation composition
   to the pose matrix is a two-line change, but verifying it behaves
   with the lighting/normal paths is its own small investment — land
   together with M6.D connector-mating.
2. **Raytracer multi-instance** — the raytracer path falls through to
   rendering only the active instance (single-board via
   `m_3d_render`). Full integration requires
   `3d-viewer/3d_rendering/raytracing/create_scene.cpp` to compose
   multiple BOARD_ADAPTERs' BVH containers with per-instance transform
   attach. Scene-graph-level change, separate session.
3. **3D model cache dedup** — each per-instance `RENDER_3D_OPENGL`
   loads its own `m_3dModelMap`. For assemblies with many sub-boards
   sharing 3D models (connectors, ICs), this duplicates the GPU
   mesh cache. Share via a common cache keyed by model path.
4. **Per-board reload throttling** — opening a 5-board container
   currently blocks for ~5 × `BOARD_ADAPTER::InitSettings` (hundreds of
   ms each). Load them in parallel or with a progress reporter.
5. **Camera auto-frame on open** — wire `GetAssemblyBoundingBox()` into
   the initial camera fit so the user sees the whole assembly instead
   of whatever the default camera shows.
6. **Visibility invalidation correctness** — when all instances are
   hidden, the framebuffer is uncleared (no first pass runs). Add a
   "nothing to render → still clear+background" branch.

### M6.D — Connector mating refinements (~1 session)

The basic mating in M6.A aligns centres and stacks in Z by board
thickness + 5 mm. This stage makes it plausible for real hardware.

1. Mate along pad normals: for a connector pair, compute each pad's
   surface normal (accounting for board flip) and rotate the second
   board so the normals are anti-parallel.
2. Respect board flip: if one sub-board has its connector on the
   bottom layer, flip the second board 180° about X/Y before mating.
3. Component-height-aware Z gap: read the connector's 3D model bounds
   (when available) instead of the 5 mm fallback, so board-to-board
   headers vs. FPC cables both mate correctly.
4. Multi-endpoint nets: for nets with >2 endpoints (e.g. a GND plane
   bridging three boards), mate the first pair and leave the third
   as-is (user can custom-position, or we pick a heuristic later).

### M6.E — Collision detection: real geometry (~1-2 sessions)

Upgrade from axis-aligned board-outline bbox to per-component 3D
collision. Reuses existing 3D model cache.

1. For each footprint on each instance: collect its 3D model mesh
   (from `S3D_CACHE`) transformed into world space.
2. Broad phase: AABB overlap between instances, then AABB overlap
   between components across overlapping board pairs.
3. Narrow phase: OBB-vs-OBB first; if the user wants mesh-accurate
   results, fall back to GJK/EPA on the convex hulls (libraries already
   in thirdparty, verify).
4. Collision results carry footprint refs so the panel can show
   "U5 on MAIN vs J1 on IO ext" instead of just "MAIN vs IO".
5. Highlight: render colliding components with an outline or tint in
   the existing render passes (add a post-build highlight set).

### M6.F — STEP assembly export (~1-2 sessions)

Finish the stubbed `ExportAssemblySTEP`. KiCad already exports per-board
STEP via OpenCASCADE (`pcbnew/exporters/step/`); the assembly export
composes N of those into a single STEP compound.

1. For each visible instance: drive the existing per-board STEP export
   to an in-memory `TopoDS_Shape` (refactor the existing file-emitting
   path to also support shape return).
2. Build a STEP compound, add each shape with its assembly transform
   applied as a `TopLoc_Location`.
3. Write the compound with the existing STEP writer. Preserve
   sub-board names in STEP product names so CAD tools show a tree.
4. Progress reporter for multi-board exports (they're slow).

### M6.G — Persistence + polish (~1 session)

1. Persist assembly state (per-instance transform + visibility +
   transparency) in the container `.kicad_pro` under
   `multi_board.assembly_3d` — key by sub-project UUID so rename-safe.
2. Restore on frame open.
3. Default initial layout: FLAT with 20 mm gaps (matches current
   `LoadProjectBoards` behaviour), overridden by persisted state.
4. Explode view (animate layout from MATED to FLAT over ~0.5 s) —
   inexpensive polish using existing camera-animation hooks.

### M6 dependencies + order

```
M5.1 (sub-project board loader) ──► M6.A ──► M6.B ──► M6.C ──┬─► M6.D
                                                              ├─► M6.E
                                                              ├─► M6.F
                                                              └─► M6.G
```

M6.A is blocked on M5.1 (the shared board loader). M6.D/E/F/G are
independent after M6.C and can be scheduled by priority. M6.D is
highest signal per unit effort — mates board-to-board headers, which
is the single most visible Altium-parity feature.

### M6 testable outcomes

- Open a container project in the manager, click "Open 3D Assembly".
- The 3D viewer opens showing every sub-board laid out flat, each
  with its own 3D models / silkscreen / copper. (M6.C)
- Toggle "Mate connectors": boards snap at connector pairs with
  correct flip handling. (M6.D)
- Run collision check: components overlapping show highlighted;
  status panel lists the overlapping refs. (M6.E)
- Export STEP: the resulting file opens in FreeCAD / SolidWorks with
  sub-board assemblies as named children. (M6.F)
- Close and re-open the project: assembly positions persist. (M6.G)

---

## Phase M7 — Project-level integration

Cross-cutting work that gives the multi-board container *project-level
substance*: today the container is a thin pass-through to its sub-
projects. M7 makes the container an actual scope for libraries and
settings.

### M7.1 — Container library architecture

**Goal:** symbol/footprint added in MBSCH (or designated as "container
library") is visible to every sub-project without manual lib-table
edits.

**Lookup precedence (new):** `GLOBAL → CONTAINER → PROJECT` — project
wins on nickname collision; container shadows global; global fills in
the rest.

**Changes:**
- Container has its own tables on disk: `<container_dir>/sym-lib-table`,
  `<container_dir>/fp-lib-table`. Same format as project tables.
  Loaded lazily on container open.
- `LIBRARY_MANAGER` gains a third tier `m_containerTables`
  (container-project-keyed by type) alongside global + project.
  Reference-counted across sub-project openings.
- `PROJECT::GetContainerProject()` walks up from project dir; if a
  sibling `.kicad_pro` with `multi_board.container = true` references
  this project's relative path under `sub_projects[]`, that container
  is its parent. Cached on first lookup. Returns nullptr for
  standalone projects.
- New `LIBRARY_TABLE_SCOPE::CONTAINER` enum value;
  `LIBRARY_MANAGER_ADAPTER::Rows()` extended to interleave container
  rows at the right precedence; `HasLibrary` / `GetLibraryNames` /
  `Row` consult container tables when adapter's PROJECT has a non-null
  container.
- Save-to-library dialogs gain a scope selector: Project / Container /
  Global. Default = Container when invoked from MBSCH; Project when
  invoked from a sub-project editor; Global unchanged.

**Files:**
- `include/project.h` + `common/project.cpp` — `GetContainerProject()`,
  resolution helper
- `include/libraries/library_manager.h` + `.cpp` — `m_containerTables`,
  `LoadContainerTables(PROJECT*)`, scope plumbing
- `include/libraries/library_table.h` — `LIBRARY_TABLE_SCOPE::CONTAINER`
- `common/libraries/symbol_library_adapter.cpp`,
  `footprint_library_adapter.cpp` — surface container rows
- `eeschema/dialogs/dialog_select_lib_table.*` (or wherever the save-
  target dialog lives) — third radio option
- `pcbnew/footprint_libraries_utils.cpp` — same scope selector
- `eeschema/mbsch_edit_frame.cpp` — load container tables on open;
  default save scope = CONTAINER

**Edge cases settled:**
| Question | Answer |
|---|---|
| Sub-project opened standalone (not via container manager): still see container libs? | Yes. `GetContainerProject()` runs unconditionally; opening `boards/fc/fc.kicad_pro` directly walks up and finds the container. |
| Two sub-projects open simultaneously, one writes a new lib row to container | Last-writer-wins on disk; in-memory adapter invalidates and reloads on next lookup. No file locking — same as global today. |
| Container has same nickname as a sub-project (`my_caps` in both) | Project wins. Documented in dialog. |
| User deletes container row while sub-project has cached it | Lookup misses → "library not found" warning on next reload. Same behavior as deleting a global row. |
| Migration of existing multi-board projects | None. Container tables are created lazily on first write. Empty container = behaves like today (project + global only). |

**Out of scope v1:** per-board library overrides at container level,
import/migration tools (e.g. "promote this row from project to
container"), demote-from-global UX.

**Risks:**
- Container PROJECT must outlive every sub-project pointing to it.
  Refcount its lifetime into the LIBRARY_MANAGER.
- KiCad's lib-table tests assume single-project. Add: container-only
  row, container+project collision, container resolution from arbitrary
  subdir.
- `Rows(LIBRARY_TABLE_SCOPE)` is called widely; audit callers that
  switch on the enum.

### M7.2 — Application settings propagation

**Goal:** color theme, grid defaults, hotkeys, eeschema/pcbnew prefs
picked at container level apply to every sub-project that doesn't
explicitly override.

**Investigation (pre-implementation):**
- Today, where do app-level vs project-level settings live?
  (`COMMON_SETTINGS`, `EESCHEMA_SETTINGS`, `PROJECT_FILE`, …)
- Which settings are "global per user" vs "per project"?
- Which of those make sense to layer at the container level vs leave
  global vs leave per-board?

**Likely shape:** parallel to M7.1 — container `settings.json` overlay,
settings adapter consults container tier between global and project.
Concrete plan after the investigation lands. Reuses
`PROJECT::GetContainerProject()` from M7.1 (build the abstraction once,
use everywhere).

---

## Phase M8 — Editor UI integration with MBS nets

Per a 2026-04-26 audit, ~18 surfaces in the schematic and PCB editors
walk `SCH_SHEET` hierarchies / iterate per-screen items but bypass
`SCH_MODULE_BLOCK` and cross-board nets. Pattern in each is the same:
extend the iterator that walks per-sheet items to also recognize
module blocks and pins, and surface cross-board net context where the
UI displays nets.

### M8.0 — Shared iteration helper

Before fixing surfaces individually: add a `RECURSE_INTO_MODULE_BLOCKS`
flag (or a `RunOnModuleBlocks` helper analogous to `RunOnChildren`) to
the iteration utilities used by the M8.1 / M8.2 surfaces below. Most
of those items become 1-line callsite swaps once this lands.

Note: M8.0 does **not** fix M8.6 (edit-operation gaps) — those are
tool/handler `case` statements, not iterators. Distinct work.

### M8.1 — Schematic editor surfaces

| Surface | File | Gap |
|---|---|---|
| Net Navigator | `widgets/net_navigator.cpp` | Handles `SCH_PIN_T`, `SCH_SHEET_PIN_T`; missing `SCH_MODULE_PIN_T`. Tree doesn't distinguish module blocks. |
| Find/Replace | `tools/sch_find_replace_tool.cpp` | `visitAll()` per-sheet; no descent into `SCH_MODULE_BLOCK` children. |
| Annotation tool | `annotate.cpp` | Collision check doesn't span module blocks; risk of duplicate refs across boards. |
| Hierarchy pane | `widgets/hierarchy_pane.cpp` | Sheet-only tree; module blocks invisible. |
| Symbol Fields Table | `dialog_symbol_fields_table.cpp` | Bulk-edit excludes module-block contents. |
| Netlist exporters | `netlist_exporters/*` | Orcad / CadStar / SPICE / KiCad netlists omit cross-board net metadata. |
| BOM generator | `bom_plugins.cpp` | No per-board / cross-board connector mapping. |
| Schematic plot/print | `sch_plotter.cpp` | Module blocks not rendered in plotted output. |
| Symbol properties dialog | `dialogs/dialog_symbol_properties.cpp` | No indication that a symbol participates in a cross-board structure. |
| Highlight net | (multi-file) | Works in MBS frame but doesn't propagate cross-board nets to sub-project highlights consistently. |

### M8.2 — PCB editor surfaces

| Surface | File | Gap |
|---|---|---|
| Net Inspector panel | `widgets/pcb_net_inspector_panel.cpp` | Per-board only; no cross-board net aggregation, no connector context. |
| Length tuner / matched-length | `drc/drc_test_provider_matched_length.cpp` + router | Length calc doesn't account for cross-board connector segments; matched-length groups can't span the boundary. |
| Differential pair finder | (router) | Same boundary issue as length tuner. |
| Footprint editor cross-reference | `footprint_editor_*.cpp` | Connector footprint editing surfaces no "this connector mates with X" info. |
| BOM/PnP export | `exporters/place_file_exporter.cpp` | Per-board export, no de-dup of components shared across boards. |
| Pad/trace properties | `dialogs/dialog_pad_properties_base*` | No indicator that pad sits on a cross-board connector / which module block owns it. |

### M8.3 — Fold MBS push-sync into SCH→PCB sync

**Today:** the MBS editor has its own "Sync to PCB" toolbar button that
pushes cross-board nets to sub-project PCBs. The regular eeschema also
has its `Update PCB from Schematic` dialog. Two sync surfaces is
confusing.

**Change:** the standard `Update PCB from Schematic` dialog
(`dialogs/dialog_update_pcb.cpp`) detects when the project is part of a
multi-board container (via `GetContainerProject()` from M7.1) and
includes MBS net updates in the same change list. Removes the standalone
"Sync to PCB" toolbar button from the MBS editor.

**Plumbing:** the multi-board variant of the netlist updater already
exists (`pcbnew/netlist_reader/multi_board_netlist_updater.cpp`); the
dialog needs to (a) auto-detect multi-board mode, (b) layer MBS net
changes into the dialog's preview, (c) apply them through the existing
updater path.

### M8.4 — Cross-board net binding DRC check

**Goal:** when project is part of a multi-board container, verify each
connector pad on this board carries the net the MBS declares for the
corresponding block pin.

**Catches:** local mistakes like swapping pads on a connector that
disagree with the MBS contract. Catches what M5.8 (MBS ERC) cannot —
M5.8 validates the *boundary-level* contract; M8.4 validates that each
sub-project board actually implements the contract.

**Mechanism:** new DRC check provider, only fires when
`PROJECT::GetContainerProject()` returns non-null. Standalone projects
unaffected. **The only** new pcbnew DRC check needed for multi-board.

### M8.5 — UI polish (icons + interaction smoothing)

Listed for visibility; bundle of small items, not a single deliverable.

- New MBS file-format icon
- Toolbar icons for Refresh, Manage Boards (the standalone "Sync to PCB"
  button goes away with M8.3, no icon needed)
- Drag/move feel inside MBS editor (snap behaviour, label reflow when
  pins added)
- Refresh-dialog redesign — settings panel + console, modeled on the
  existing `Update Schematic from PCB` dialog
  (`dialogs/dialog_update_from_pcb.*`) which uses
  `WX_HTML_REPORT_PANEL` for streaming. Replaces the current flat
  checklist. Loses per-row granularity; gains scale + visible-error
  surface. Settings map directly to existing `MBS_CHANGE::KIND` enum.

### M8.6 — Edit operations on MBS items (high priority)

A 2026-04-26 deep audit found six places where edit-tool handlers
special-case `SCH_SHEET_T` / `SCH_SHEET_PIN_T` but have no equivalent
for `SCH_MODULE_BLOCK_T` / `SCH_MODULE_PIN_T`. These are not iteration
issues (M8.0 doesn't help); each is a `case` statement in a tool
handler. **Without these, basic edit operations on module blocks are
either broken or silently no-op.**

**These are higher priority than the M8.1 / M8.2 surface work** — they
gate user-facing functionality the user is likely to hit immediately
(delete a block, rotate it, copy/paste).

| Surface | File | Gap |
|---|---|---|
| Delete action collector | `sch_collectors.cpp` (`DeletableItems` list) | `SCH_SHEET_T` listed; `SCH_MODULE_BLOCK_T` missing — Delete shortcut silently doesn't fire on module blocks. Add `SCH_MODULE_BLOCK_T` (and verify pins are deleted by virtue of being parent-owned children, not as standalone collector entries). |
| Rotate / Mirror | `tools/sch_edit_tool.cpp` | Has `case SCH_SHEET_T:` blocks for rotate/mirror that call `sheet->Rotate(rotPoint)` etc. No `SCH_MODULE_BLOCK_T` cases. Module block rotates / mirrors are no-ops despite the block class having `Rotate()` / `MirrorH/V()` overrides. |
| Point editor (drag corners to resize) | `tools/sch_point_editor.cpp` | Constructs `SHEET_POINT_EDIT_BEHAVIOR` for `SCH_SHEET_T` — gives the sheet its draggable resize handles. No `MODULE_BLOCK_POINT_EDIT_BEHAVIOR` equivalent. Module blocks can't be resized by dragging. |
| Move tool — wire-to-pin connection preservation | `tools/sch_move_tool.cpp` | `m_specialCaseSheetPins` tracks wires connected to sheet pins so moves don't break the connection. Same machinery missing for module pins; moving a wire connected to a module pin can break the link. |
| Cut / Copy clipboard | `tools/sch_editor_control.cpp` `doCopy()` | Explicit `if( item->Type() == SCH_SHEET_T )` to stash sheet screen state in supplementary clipboard. No `SCH_MODULE_BLOCK_T` branch — copy may not preserve pins / cross-board net assignments correctly. Verify paste round-trip. |
| Connectivity classification | `sch_item.cpp` `IsSheetConnectedItem()` | Returns true for `SCH_SHEET_T` / `SCH_SHEET_PIN_T`. Module types missing. Anything that branches on this (connectivity rebuild, dangling-end-point computation) misclassifies module blocks/pins. |

**Mechanism for each:** mirror the sheet handler one-to-one. The
SCH_MODULE_BLOCK already has matching virtual methods (Rotate, Mirror*,
SetSize, etc.); these surfaces just need to know to call them.

### M8.7 — Verification items (lower priority — verify, then fix if broken)

| Surface | File | Gap (suspected) |
|---|---|---|
| Field text get/set on container items | `sch_field.cpp` `SetFieldText`/`GetFieldText` | `case SCH_SHEET_T:` exists; verify whether module blocks need a fields concept (probably not in v1 — block has its own `m_displayName` / `m_mbsReference`). |
| Symbol properties dialog context | `dialogs/dialog_symbol_properties.cpp` | Uses `GetCurrentSheet()`. May misbehave when MBSCH frame is active and a symbol inside a module block is selected. Verify before fixing. |
| Group operations | `tools/sch_group_tool.cpp` | Design decision: should groups be allowed to span MBS module-block boundaries? Probably no — but verify that grouping doesn't crash if user tries. |
| Undo / redo for module-block edits | `schematic_undo_redo.cpp` | Generic mechanism should work via `SCH_ITEM::Clone()` which the block already overrides. Verify that delete + undo restores all pins; that move + undo reverts position; that pin-add via refresh + undo removes the pin. |
| Board statistics dialog | `pcbnew/dialogs/dialog_board_statistics.cpp` | Per-board stats only; doesn't acknowledge the assembly. Low priority — could optionally show "this is one board of N in a multi-board project" line. |

### M8.9 — Cross-board context awareness in regular eeschema / pcbnew

User confirmed 2026-04-26 that several items in this section are already
addressed: net classes for cross-board nets (M8.9.3), visual annotation
on connector pins / pads (M8.9.4), net naming reconciliation (M8.9.5),
highlight-net cross-editor propagation (M8.9.6), and save/load
round-trip (M8.9.7) — all confirmed working. **Only M8.9.1 (sub-project
ERC suppression) and M8.9.2 (sub-project DRC suppression) remain open.**

Without these last two, regular ERC and DRC false-positive on every
cross-board connector pin/pad: ERC sees a "no driver" / "single driver",
DRC sees a "single-pad net". Both make sub-project verification noisy
on multi-board projects.

User confirmation 2026-04-26: copy/paste works on MBS items, but delete
/ rotate / resize / move-keeps-other-end-connected do not — confirms
the M8.6 audit. Wire one-click termination on module pin still appears
broken (user reports double-click required); regression check needed
on the earlier `SCH_LINE::CanConnect` fix.

#### M8.9.1 — Sub-project ERC suppresses cross-board false positives

**Problem:** sub-project schematic ERC has no concept of cross-board.
A connector pin (J1/3) wired to a single label on board A looks like a
"single driver" or "no driver" depending on direction. ERC flags it as
a violation. There are real violations buried in the noise.

**Mechanism:**
- New ERC check provider (or extension to existing checks) consults
  `Prj().GetContainerProject()->GetCrossBoardNets()`.
- For each ERC marker, if the underlying pin is a cross-board endpoint
  (its `(componentRef, pinNumber)` matches an `MB_CROSS_BOARD_NET_ENDPOINT`),
  suppress: no-driver / single-driver / floating-input warnings.
- Surface as "(cross-board net — driven from <board>/<connector>)" so
  the user sees why the warning is gone.

**Files:** `eeschema/erc/erc.cpp` + relevant test providers.

#### M8.9.2 — Sub-project DRC suppresses cross-board false positives

**Problem:** sub-project PCB DRC will flag a connector pad with no
copper trace as "single-pad net" / "track has no connection".
`CONNECTIVITY_DATA::IsCrossBoardConnectorPad` (`connectivity_data.cpp:1255`)
is implemented but **never called** — dead code awaiting use.

**Mechanism:**
- Existing DRC test providers that check pad connectivity (single-pad
  net, courtyard, unconnected items) consult `IsCrossBoardConnectorPad`
  before emitting violations.
- Suppression is informational: violation downgraded to "info" with the
  cross-board net name shown.

**Files:** `pcbnew/drc/drc_test_provider_*.cpp` (single-pad, connection,
copper). The helper already exists; just needs callers.

**Distinct from M8.4** (binding check): M8.4 *adds* a new check that
fires *because* the project is multi-board. M8.9.2 *suppresses* existing
checks that misfire because the project is multi-board.

#### M8.9.3 – M8.9.7 — done (confirmed 2026-04-26)

Net classes for cross-board nets, visual annotation on connector
pins/pads, net naming reconciliation, highlight-net cross-editor
propagation, and save/load round-trip — all confirmed working by user.
Reference detail removed from this doc; consult git history for the
implementation commits.

### M8.10 — Confirmed *not* needed

For closure, these surfaces were checked and don't need MBS work:
- Library browser (symbols / footprints) — operates on library content,
  not on MBS items.
- Layer manager — layers are per-board.
- Hotkey customization — MBS-specific actions register through the
  same `TOOL_ACTION` machinery; appear automatically.
- Title block / page setup on `.kicad_mbs` — already inherits from
  SCH_EDIT_FRAME; verified to render.
- About dialog, Preferences dialog tabs — no MBS-specific entries
  needed.

### M8.11 — Open regression to verify

User reported 2026-04-26: wire termination on a module pin still
appears to require a double-click despite the earlier
`SCH_LINE::CanConnect` fix that added `SCH_MODULE_PIN_T` to the
endpoint whitelist. Reproduce, identify whether the regression is
in CanConnect, in the autostart wire detection, or in the click-
disambiguation timer (`m_disambiguateTimer`). Possibly related to the
move-tool sheet-pin special-case gap in M8.6.

---

## Phase M9 — Agent integration

**Goal:** the in-process agent can drive multi-board workflows
end-to-end:
- Create a `.kicad_pro` container from a prompt
- Add / import sub-projects
- Edit MBS (place blocks, draw cross-board wires)
- Run all intermediary syncs (connector scan, refresh MBS, push to
  sub-project schematics, push to sub-project PCBs)

### M9.1 — Kipy bindings

New Python types in `zeo-python/kipy/` for:
- Multi-board container (open / save / list sub-projects / add /
  remove)
- `SCH_MODULE_BLOCK` / `SCH_MODULE_PIN` (read / create / wire to other
  blocks)
- Refresh-MBS orchestration (run diff, apply selected changes)
- Push-sync to sub-project SCH / PCB

### M9.2 — Agent tool schemas + Python handlers

- New tool definitions in `agent/tools/tool_schemas.cpp`:
  - `multi_board_create`, `multi_board_add_sub_project`,
    `multi_board_list`, `multi_board_remove_sub_project`
  - `mbs_add_block`, `mbs_remove_block`, `mbs_connect_pins`
  - `mbs_refresh`, `mbs_sync_to_pcb`
- Python handlers in `agent/tools/python/multi_board/` (new directory)
- Tool registration in `tool_registry.cpp`

### M9.3 — IPC handlers

- Extend `eeschema/api/api_handler_sch.cpp` with MBS-aware commands
  (recognize MBS items in selections, route refresh through the
  IPC API)
- Extend `pcbnew/api/api_handler_pcb.cpp` similarly for cross-board
  net push-sync
- Protobuf definitions in `api/proto/schematic/` and `api/proto/pcb/`
  for the new message types

---

## Dependencies + schedule

```
M0 ✓ ─► M1 ✓ ─┬─► M2 ✓ ──────────────┐
              └─► M3 (pending) ───────┤
                                      ├─► M5 (pending) ─► M6 (pending)
M4 ✓ MVP ─────────────────────────────┘                    │
                                                            │
M7.1 (container libs) ──► M7.2 (settings)                   │
       │                                                    │
       └──► M8.3 (fold sync), M8.4 (binding DRC) ───────────┤
                                                            │
M8.0 (helper) ──► M8.1, M8.2 (UI surfaces) ─────────────────┤
                                                            │
M9 (agent) ◄────────────────────────────────────────────────┘
```

For the actionable week-by-week schedule covering the remaining ~30%,
see the **Finish line — remaining 30%** section at the top of this
doc. Phase-by-phase detail below is reference / spec.

---

## Risks + open questions

### R1 — Pre-existing PROJECT_FILE `multi_board` fields ✓ resolved
Kept the existing `BOARD_INFO` / `CROSS_BOARD_CONNECTION` /
`COMPONENT_BOARD_ASSIGNMENT` structures alongside the new container
fields. Tested modules (netlist updater, component assignment) use the
legacy structures; the 3D viewer will migrate off them in M6.A.

### R2 — Container vs sub-project distinction ✓ resolved
`multi_board.container: bool` flag in PROJECT_FILE. Consumed by
`IsMultiBoardContainer()`.

### R3 — M4 effort revised ✓ resolved
MVP landed in ~3 days, not 1-2 months. The essential blocker was two
lines.

### R4 — mbschema kiface linkage ✓ resolved
Chose subclass over separate kiface; rationale in Phase M2.

### R5 — Test project migration ✓ resolved
Test projects discarded and recreated.

### R6 — Untested module validation
**Status:** the 3D viewer is still unwired and uses the legacy data
model — M6 is the full answer. Cross-board DRC is blocked only on the
sub-project board loader (M5.1); once landed, `CheckConnectorMatching`
should work and the stubbed `CheckSignalIntegrity` /
`CheckPowerDistribution` become a scoped investment.

### R7 — 3D renderer divergence risk (new)
**Risk:** extending `RENDER_3D_BASE` / `BOARD_ADAPTER` for multi-board
could regress single-board rendering in pcbnew.

**Mitigation:** prefer a parallel code path gated by mode ("assembly
mode" vs. "single-board mode") rather than refactoring the single-
adapter API. Keep the existing `ReloadRequest(BOARD*)` signature
intact; add new assembly-mode entry points alongside. Test pcbnew's
3D viewer against every demo project after each M6.C change.

### R8 — STEP export performance (new)
**Risk:** exporting N sub-boards as STEP can take tens of seconds each.

**Mitigation:** in M6.F wrap with progress reporter from the start;
parallelise per-board export if possible (the existing STEP exporter
is single-threaded per board).

### R9 — Shared board loader cache invalidation (new)
**Risk:** the M5.1 loader will be used from three different places
(DRC, netlist, 3D viewer) with different lifetime assumptions. A
sub-board edited in a peer PCB editor must not be seen as stale by the
3D viewer.

**Mitigation:** loader returns `std::unique_ptr<BOARD>`, owner-managed.
No shared cache in the loader; callers (3D viewer, DRC engine) cache
their own copy. Peer editors broadcast a new `MAIL_RELOAD_SUB_PROJECT`
ExpressMail on save that triggers the 3D viewer to reload the affected
instance.

---

## Key decisions locked in

1. **Multi-project topology.** Each sub-project is a standalone
   `.kicad_pro` + `.kicad_sch` + `.kicad_pcb`. The multi-board parent
   is a `.kicad_pro` with `multi_board.container = true` and an MBS
   schematic (`.kicad_mbs`) at the same level. Matches Altium's peer
   model.
2. **MBS editor as subclass of eeschema**, not a separate kiface.
   Shares everything; overrides the trimmed surface + save hook +
   cross-probe handling.
3. **In-process multi-project**, not separate processes.
4. **Tested modules preserved.** Multi-board netlist updater and
   component assignment keep their current data model.
5. **3D viewer skeleton preserved + ported**, not rewritten from
   scratch. `ASSEMBLY_3D_MANAGER` / `PANEL_3D_ASSEMBLY` are the
   starting point — M6.A refactors them to the container topology
   rather than introducing a new manager class.
6. **Multi-board rendering: multiple `BOARD_ADAPTER`s composited**,
   not a merged super-BOARD and not an extended single-adapter
   instance list. Rationale in Phase M6.
7. **Single shared sub-project board loader** (M5.1) used by the 3D
   viewer, cross-board DRC, and the multi-board netlist updater.
   Returns owner-managed `std::unique_ptr<BOARD>`, no shared cache.
