# Multi-Board Architecture Refactor — Phase M

Living plan for consolidating multi-board infrastructure around KiCad-native
primitives, introducing a dedicated MBS editor, redesigning the project
manager launcher, enabling concurrent multi-editor work, and building an
Altium-grade multi-board 3D assembly viewer.

## Current state (2026-04-23)

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

## Phase M5 — Non-3D cleanup + follow-ons (~2-3 days) — pending

1. **Shared sub-project board loader** — new helper (likely
   `common/project/sub_project_board_loader.{h,cpp}` or
   `pcbnew/board_utils/`) that takes a container `PROJECT&` and a
   `SUB_PROJECT_INFO` or its UUID, resolves the sub-project's
   `.kicad_pro`, reads its board filename, and loads the `.kicad_pcb`
   via `PCB_IO_KICAD_SEXPR::LoadBoard`. Returns a `std::unique_ptr<BOARD>`
   with owner-managed lifetime.
   - Replaces the stub in `DRC_ENGINE_CROSS_BOARD::GetBoardByUuid`.
   - Replaces the active-only path in
     `MULTI_BOARD_NETLIST_UPDATER::GetBoardByUuid`.
   - Consumed by Phase M6 (3D assembly viewer).
2. **Replace directory-walk MBS lookup** with an explicit parent-project
   reference stored in the `.kicad_mbs` file (`(parent_project "...")`
   s-expr or in the `(title_block)` metadata). Updates
   `sch_editor_control.cpp:~621`, `files-io.cpp::syncCrossBoardNetsIfMbs`,
   and the netlist-updater container probe.
3. **Replace text-level PCB sync** with BOARD-level edits via
   `PCB_IO_KICAD_SEXPR::LoadBoard()`. Robust handling of quoted strings,
   whitespace, escape sequences.
4. **Comment sweep** — eliminate remaining `.kicad_multi` /
   `MULTI_BOARD_PROJECT` mentions in the files listed in Current state.
5. **Validate cross-board DRC** on a migrated multi-board project once
   the sub-project board loader is in place. The stubs
   `CheckSignalIntegrity` and `CheckPowerDistribution` are a separate
   investment decision, not blocked by this phase.
6. **Evaluate CONNECTION_GRAPH integration** — register
   `SCH_MODULE_BLOCK_T` / `SCH_MODULE_PIN_T`, retire the custom
   union-find extractor if the graph integration is cheap. ~4 hours if
   we go for it; skip if the graph API makes it harder.
7. **Cross-board ERC rules** — walk `multi_board.cross_board_nets`,
   validate endpoint counts + pin directions + pad-count match between
   paired connectors.

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

### M6.C — Multi-board composition in the renderer (~2 sessions)

The core technical work — actual Altium-like view.

1. `ASSEMBLY_3D_MANAGER` owns one `BOARD_ADAPTER` per
   `BOARD_3D_INSTANCE`. Each adapter builds its own layer containers /
   BVH in its local frame via `InitSettings`. Shared `S3D_CACHE`.
2. Extend `RENDER_3D_BASE` (`render_3d_base.h:102`) to optionally hold
   a list of `{BOARD_ADAPTER*, glm::mat4}` instead of a single adapter.
   Prefer adding a second path rather than breaking the existing single-
   adapter API — pcbnew's single-board 3D viewer must stay pristine.
3. **OpenGL path** (`render_3d_opengl.cpp:1050-1118`): wrap footprint-
   iteration and layer-draw blocks in an outer loop over instances;
   left-multiply the footprint matrix by the instance transform.
   Existing per-footprint matrix at line 1098-1118 is unchanged in form.
4. **Raytracer path** (`create_scene.cpp:347`): apply per-instance
   transform to the layer's BVH containers before compositing into the
   raytracer scene. The BVH supports transform attach.
5. Camera auto-frame uses `ASSEMBLY_3D_MANAGER::GetAssemblyBoundingBox()`
   (already implemented; just needs to be consumed).
6. Live transforms: panel position / rotation edits update the instance
   transform and trigger `Redraw()` without rebuilding per-board
   geometry (transforms are render-time).
7. Visibility: per-instance `visible` flag skips its render pass; no
   adapter teardown.

Deliverable: boards arranged flat / stacked / mated in one scene, with
live transformation, existing AABB collision highlighting, and
transparency per board.

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

## Dependencies + schedule

```
M0 ✓ ─► M1 ✓ ─┬─► M2 ✓ ──────────────┐
              └─► M3 (pending) ───────┤
                                      ├─► M5 (pending) ─► M6 (pending)
M4 ✓ MVP ─────────────────────────────┘
```

| Week | Focus |
|---|---|
| 0 ✓ | M0, M1, M2 |
| 1 ✓ | M4 MVP |
| 2 (now) | M3 launcher + M5 (start with M5.1 board loader) |
| 3 | M5 complete + M6.A/B |
| 4-5 | M6.C (multi-board rendering) |
| 6 | M6.D (mating) + M6.E (real collision) |
| 7 | M6.F (STEP export) + M6.G (persistence) |
| 8+ | M4.2 sweep, cross-board ERC, DRC stubs |

Total to Altium-parity 3D assembly viewer: ~8 weeks from 2026-04-23.

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
