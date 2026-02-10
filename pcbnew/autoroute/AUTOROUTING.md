# PCB Autorouting Implementation Status

This document tracks the implementation status of the Zener PCB autorouter compared to FreeRouting.

## Overview

The Zener autorouter is based on FreeRouting's architecture, which uses:
- **Expansion Room Model**: Board decomposed into navigable regions
- **A\* Maze Search**: Priority queue-based pathfinding with heuristics
- **Doors & Drills**: Transitions between rooms and layers

Two implementations exist:
1. **Python Agent Code** (`pcb_crud_handler.cpp` → generates Python) - Simple grid-based A*
2. **C++ Library** (`pcbnew/autoroute/`) - Full expansion room model (in progress)

---

## Feature Comparison

### Core Algorithm

| Feature | FreeRouting | Python Agent | C++ Library | Status |
|---------|-------------|--------------|-------------|--------|
| A* maze search | ✅ | ✅ | ✅ | Working |
| Priority queue (min-heap) | ✅ | ✅ | ✅ | Working |
| Heuristic distance | ✅ Octile | ✅ Octile | ✅ Manhattan | Needs improvement |
| 8-connected grid (45°) | ✅ | ✅ | ❌ | C++ needs update |
| Expansion room model | ✅ | ❌ Grid only | ✅ | C++ in progress |
| Object pooling | ✅ ThreadLocal | ❌ | ❌ | Not started |

### Spatial Model

| Feature | FreeRouting | Python Agent | C++ Library | Status |
|---------|-------------|--------------|-------------|--------|
| Expansion rooms | ✅ | ❌ | ✅ | C++ headers done |
| Free space rooms | ✅ | ❌ | ✅ | Not populated |
| Obstacle rooms | ✅ | ✅ (ObstacleMap) | ✅ | Basic |
| Target rooms | ✅ | ❌ | ✅ | Headers only |
| Expansion doors | ✅ | ❌ | ✅ | Headers only |
| Expansion drills (vias) | ✅ | ❌ | ✅ | Headers only |
| ShapeSearchTree (R-tree) | ✅ | ❌ | ❌ | Not started |
| Clearance compensation | ✅ | ✅ Fixed | ❌ | Needs work |

### Multi-Layer Support

| Feature | FreeRouting | Python Agent | C++ Library | Status |
|---------|-------------|--------------|-------------|--------|
| 2-layer routing | ✅ | ✅ | ✅ | Working |
| N-layer routing | ✅ | ❌ | ✅ | C++ supports |
| Through vias | ✅ | ✅ | ✅ | Working |
| Blind vias | ✅ | ❌ | ❌ | Not started |
| Buried vias | ✅ | ❌ | ❌ | Not started |
| Layer direction preference | ✅ | ❌ | ✅ | Config only |
| Layer-specific trace width | ✅ | ❌ | ✅ | Config only |

### Cost Functions

| Feature | FreeRouting | Python Agent | C++ Library | Status |
|---------|-------------|--------------|-------------|--------|
| Trace length cost | ✅ | ✅ | ✅ | Working |
| Via insertion cost | ✅ | ✅ (fixed=5) | ✅ | Working |
| Direction change cost | ✅ | ❌ | ✅ | Config only |
| Layer preference cost | ✅ | ❌ | ❌ | Not started |
| Ripup cost | ✅ | ❌ | ❌ | Not started |
| Congestion cost | ✅ | ❌ | ❌ | Not started |

### Routing Strategies

| Feature | FreeRouting | Python Agent | C++ Library | Status |
|---------|-------------|--------------|-------------|--------|
| Single net routing | ✅ | ✅ | ✅ | Working |
| Batch routing (all nets) | ✅ | ✅ | ✅ | Working |
| Net ordering (MST) | ✅ | ✅ | ❌ | Python only |
| Ripup and retry | ✅ | ❌ | ❌ | Not started |
| Multi-pass optimization | ✅ | ❌ | ❌ | Not started |
| Fanout routing | ✅ | ❌ | ❌ | Not started |

### Path Optimization

| Feature | FreeRouting | Python Agent | C++ Library | Status |
|---------|-------------|--------------|-------------|--------|
| Path simplification | ✅ | ✅ | ❌ | Python only |
| PullTight 90° | ✅ | ❌ | ❌ | Not started |
| PullTight 45° | ✅ | ❌ | ❌ | Not started |
| PullTight any-angle | ✅ | ❌ | ❌ | Not started |
| Via optimization | ✅ | ❌ | ❌ | Not started |
| Shove trace | ✅ | ❌ | ❌ | Not started |

### Design Rules

| Feature | FreeRouting | Python Agent | C++ Library | Status |
|---------|-------------|--------------|-------------|--------|
| Fixed clearance | ✅ | ✅ 0.2mm | ✅ | Working |
| Clearance matrix | ✅ | ❌ | ❌ | Not started |
| Net class rules | ✅ | ❌ | ❌ | Not started |
| Via rules | ✅ | ❌ | ❌ | Not started |
| Keepout zones | ✅ | ❌ | ❌ | Not started |
| DRC integration | ✅ | ❌ | ❌ | Not started |

### Board Integration

| Feature | FreeRouting | Python Agent | C++ Library | Status |
|---------|-------------|--------------|-------------|--------|
| Read footprints | ✅ | ✅ | ❌ | Python via kipy |
| Read existing tracks | ✅ | ✅ | ❌ | Python via kipy |
| Read existing vias | ✅ | ✅ | ❌ | Python via kipy |
| Pad position transform | ✅ | ✅ | ❌ | Python handles |
| Insert tracks | ✅ | ✅ | ❌ | Python via kipy |
| Insert vias | ✅ | ✅ | ❌ | Python via kipy |
| Undo support | ✅ | ❌ | ❌ | Not started |

---

## Known Bugs (Python Agent Implementation)

### Critical

1. **[x] Pad position double-transformation (FIXED)**
   - Bug: pad.position already contains absolute board coordinates, but code was
     applying footprint rotation/translation transform again
   - Result: Tracks placed at wrong coordinates (e.g., Y=165mm off-board)
   - Fix: Removed unnecessary transformation - use pad.position directly

2. **[x] Layer transitions breaking connectivity (FIXED)**
   - Bug: When routing switched layers, via was placed at pad position but track
     was created on the NEW layer, leaving no copper connection on pad's layer
   - Result: KiCad connectivity engine didn't recognize pad-to-track connection
   - Fix: Create track on current layer TO via position, then place via, then
     continue on new layer. Via now placed at layer change point, not pad center.

3. **[x] Orphaned pads in MST routing (FIXED)**
   - Bug: When a route failed, the destination pad was removed from unconnected
     set but never added to connected set, leaving it unreachable
   - Result: Large nets (like GND with 53 pads) had many orphaned pads
   - Fix: Track failed source-destination pairs separately, keep trying other
     source pads. Only give up on a pad when ALL possible sources have failed.

4. **[x] Supporting functions had same 2x coordinate bug (FIXED)**
   - Bug: `pcb_get_pads`, `pcb_get_footprint`, `pcb_route`, and `pcb_place` all
     applied footprint rotation/translation to pad positions that were already absolute
   - Result: Pad positions reported at 2x the correct location
   - Fix: All functions now use `pad.position` directly without transformation

5. **[x] pcb_get_nets didn't check actual connectivity (FIXED)**
   - Bug: `unrouted_only` filter just counted pads (>= 2), didn't check if nets
     were actually routed according to KiCad's connectivity engine
   - Result: Showed all multi-pad nets as "unrouted" even when fully connected
   - Fix: Now uses `board.connectivity.get_unrouted_nets()` API to get actual
     routing status (routed_connections, unrouted_connections, is_complete)

6. **[ ] Pad layer detection incorrect**
   - SMD pads on back layer may not be detected correctly
   - PTH vs SMD distinction needs verification

7. **[ ] Obstacle marking incomplete**
   - Existing tracks marked but may have gaps
   - Footprint courtyards not considered

### High Priority

8. **[ ] Path simplification aggressive**
   - May remove necessary intermediate points
   - Direction changes at corners not preserved

9. **[ ] Grid alignment issues**
   - Pad centers may not align to routing grid
   - Track endpoints may be off-grid

### Medium Priority

10. **[ ] No diagonal obstacle checking**
    - Diagonal moves may clip corners of obstacles
    - Need proper line-rectangle intersection

11. **[ ] Fixed via size**
    - Uses hardcoded 0.8mm diameter, 0.4mm drill
    - Should read from board design rules

12. **[ ] No pad shape consideration**
    - Assumes point pads
    - Should consider actual pad geometry for entry point

### Low Priority

13. **[ ] No net class support**
    - All nets use same trace width/clearance
    - Power nets should use wider traces

14. **[ ] No differential pair support**
    - Routes each signal independently
    - No length matching

15. **[ ] Performance with large boards**
    - 100000 iteration limit may be insufficient for complex boards
    - No spatial indexing for obstacle lookup

---

## Architecture Comparison

### FreeRouting Class Hierarchy

```
MazeSearchAlgo
├── ExpansionRoom (abstract)
│   ├── IncompleteFreeSpaceExpansionRoom
│   ├── CompleteFreeSpaceExpansionRoom
│   └── ObstacleExpansionRoom
├── ExpansionDoor
│   └── TargetItemExpansionDoor
├── ExpansionDrill
├── MazeListElement (priority queue entry)
├── DestinationDistance (heuristic)
└── AutorouteEngine (coordination)

BatchAutorouter
├── BoardRanking (state tracking)
├── NetOrdering
└── RipupControl
```

### Zener C++ Class Hierarchy

```
AUTOROUTER (public API)
└── AUTOROUTE_ENGINE
    ├── EXPANSION_ROOM (abstract)
    │   ├── FREE_SPACE_ROOM
    │   ├── OBSTACLE_ROOM
    │   └── TARGET_ROOM
    ├── EXPANSION_DOOR
    ├── EXPANSION_DRILL
    └── MAZE_SEARCH
        ├── MAZE_LIST_ELEMENT
        └── DESTINATION_DISTANCE

AUTOROUTE_CONTROL (configuration)
AUTOROUTE_RESULT (statistics)
TILE_SHAPE (geometry)
```

---

## Implementation Roadmap

### Phase 1: Fix Python Agent (Current)
- [ ] Debug pad position calculation
- [ ] Fix layer detection for SMD vs PTH
- [ ] Verify track-to-pad connectivity
- [ ] Add diagonal obstacle checking
- [ ] Read design rules from board

### Phase 2: Complete C++ Library
- [ ] Implement `BuildRoomModel()` in engine
- [ ] Implement `BuildObstacleRooms()` from board items
- [ ] Implement `BuildFreeSpaceRooms()` using tile decomposition
- [ ] Implement `BuildDoors()` between adjacent rooms
- [ ] Implement `BuildDrills()` for via locations
- [ ] Connect MAZE_SEARCH to expansion rooms

### Phase 3: Path Optimization
- [ ] Port PullTightAlgo90 from FreeRouting
- [ ] Port PullTightAlgo45 from FreeRouting
- [ ] Implement via count optimization
- [ ] Add post-routing cleanup

### Phase 4: Advanced Features
- [ ] Ripup and retry
- [ ] Net class support
- [ ] Clearance matrix
- [ ] Multi-pass optimization
- [ ] Differential pair routing

### Phase 5: Performance
- [ ] Spatial indexing (R-tree)
- [ ] Object pooling for MazeListElement
- [ ] Multi-threaded batch routing
- [ ] Progress reporting / cancellation

---

## Testing

### Test Boards
- `qa/data/pcbnew/` - Various test PCBs
- Need simple 2-layer test with known good routes

### Test Cases Needed
1. Simple 2-pad net (straight line)
2. 2-pad net with obstacle (L-shape)
3. Multi-pad net (star topology)
4. Via required (pads on different layers)
5. Blocked route (no solution)
6. Large net (many pads)
7. Dense board (many obstacles)

---

## References

- [FreeRouting Source](https://github.com/freerouting/freerouting)
- [FreeRouting MazeSearchAlgo.java](src/main/java/app/freerouting/autoroute/MazeSearchAlgo.java)
- [FreeRouting BatchAutorouter.java](src/main/java/app/freerouting/autoroute/BatchAutorouter.java)
- [A* Algorithm](https://en.wikipedia.org/wiki/A*_search_algorithm)
- [Lee Algorithm](https://en.wikipedia.org/wiki/Lee_algorithm) (simpler maze router)
