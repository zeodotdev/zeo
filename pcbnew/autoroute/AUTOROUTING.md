# PCB Autorouting Implementation Status

This document tracks the implementation status of the Zener PCB autorouter compared to FreeRouting.

## Overview

The Zener autorouter is based on FreeRouting's architecture, which uses:
- **Expansion Room Model**: Board decomposed into navigable regions
- **A\* Maze Search**: Priority queue-based pathfinding with heuristics
- **Doors & Drills**: Transitions between rooms and layers
- **Dynamic Room Expansion**: Rooms completed on-demand during search

Two implementations exist:
1. **Python Agent Code** (`pcb_crud_handler.cpp` → generates Python) - Simple grid-based A*
2. **C++ Library** (`pcbnew/autoroute/`) - Full expansion room model (FreeRouting-style)

---

## Recent Changes (February 2026)

The C++ autorouter has been significantly rebuilt to match FreeRouting's architecture:

### Completed Features

| Feature | Description | Files Modified |
|---------|-------------|----------------|
| **Dynamic Room Expansion** | Rooms completed lazily during maze search instead of pre-computed grid | `autoroute_engine.cpp/h` |
| **45-Degree Corner Optimization** | Chamfered corners instead of 90-degree turns | `locate_connection.cpp` |
| **Delayed Occupation Strategy** | Retry mechanism with cost penalties for blocked paths | `maze_search.cpp/h` |
| **Pin Neckdown** | Automatic trace width reduction when approaching smaller pads | `insert_connection.cpp/h` |
| **Push-and-Shove** | Move blocking traces instead of removing them | `ripup_checker.cpp/h`, `maze_search.cpp` |
| **Collision Validation** | Check tracks against obstacles, keepout zones, and other-net pads before insertion | `insert_connection.cpp` |
| **Net-Aware Filtering** | Same-net items don't block routing | `shape_search_tree.cpp/h` |
| **Half-Plane/Simplex Geometry** | FreeRouting-style convex shape representation | `tile_shape.cpp/h` |
| **PullTight Optimization** | Path optimization with 90°/45°/any-angle vertex moving | `optimize/pull_tight.cpp/h` |
| **Net Ordering** | Route simpler nets first, retry failed nets | `autoroute_engine.cpp` |
| **Multi-Pass Optimization** | Multiple passes with increasing effort and ripup | `autoroute_engine.cpp`, `autoroute_control.h` |
| **Congestion-Aware Routing** | Track congestion and penalize crowded areas | `search/congestion_map.cpp/h` |

---

## Feature Comparison

### Core Algorithm

| Feature | FreeRouting | Python Agent | C++ Library | Status |
|---------|-------------|--------------|-------------|--------|
| A* maze search | ✅ | ✅ | ✅ | Working |
| Priority queue (min-heap) | ✅ | ✅ | ✅ | Working |
| Heuristic distance | ✅ Octile | ✅ Octile | ✅ Manhattan | Working |
| 8-connected grid (45°) | ✅ | ✅ | ✅ | **Now implemented** |
| Expansion room model | ✅ | ❌ Grid only | ✅ | **Working** |
| Dynamic room expansion | ✅ | ❌ | ✅ | **Now implemented** |
| Delayed occupation retry | ✅ | ❌ | ✅ | **Now implemented** |
| Object pooling | ✅ ThreadLocal | ❌ | ❌ | Not started |

### Spatial Model

| Feature | FreeRouting | Python Agent | C++ Library | Status |
|---------|-------------|--------------|-------------|--------|
| Expansion rooms | ✅ | ❌ | ✅ | **Working** |
| Free space rooms | ✅ | ❌ | ✅ | **Dynamic completion** |
| Incomplete rooms | ✅ | ❌ | ✅ | **Now implemented** |
| Obstacle rooms | ✅ | ✅ (ObstacleMap) | ✅ | **Working** |
| Target rooms | ✅ | ❌ | ✅ | Working |
| Expansion doors | ✅ | ❌ | ✅ | **Working** |
| Expansion drills (vias) | ✅ | ❌ | ✅ | **Working** |
| ShapeSearchTree (R-tree) | ✅ | ❌ | ✅ | **Now implemented** |
| Half-plane geometry | ✅ | ❌ | ✅ | **Now implemented** |
| Simplex shapes | ✅ | ❌ | ✅ | **Now implemented** |
| Clearance compensation | ✅ | ✅ Fixed | ✅ | **Working** |

### Multi-Layer Support

| Feature | FreeRouting | Python Agent | C++ Library | Status |
|---------|-------------|--------------|-------------|--------|
| 2-layer routing | ✅ | ✅ | ✅ | Working |
| N-layer routing | ✅ | ❌ | ✅ | Working |
| Through vias | ✅ | ✅ | ✅ | Working |
| Blind vias | ✅ | ❌ | ✅ | **Now implemented** |
| Buried vias | ✅ | ❌ | ✅ | **Now implemented** |
| Microvias | ✅ | ❌ | ✅ | **Now implemented** |
| Layer direction preference | ✅ | ❌ | ✅ | Config only |
| Layer-specific trace width | ✅ | ❌ | ✅ | Config only |

### Cost Functions

| Feature | FreeRouting | Python Agent | C++ Library | Status |
|---------|-------------|--------------|-------------|--------|
| Trace length cost | ✅ | ✅ | ✅ | Working |
| Via insertion cost | ✅ | ✅ (fixed=5) | ✅ | Working |
| Direction change cost | ✅ | ❌ | ✅ | Config only |
| Layer preference cost | ✅ | ❌ | ❌ | Not started |
| Ripup cost | ✅ | ❌ | ✅ | **Now implemented** |
| Shove cost | ✅ | ❌ | ✅ | **Now implemented** |
| Congestion cost | ✅ | ❌ | ✅ | **Now implemented** |

### Routing Strategies

| Feature | FreeRouting | Python Agent | C++ Library | Status |
|---------|-------------|--------------|-------------|--------|
| Single net routing | ✅ | ✅ | ✅ | Working |
| Batch routing (all nets) | ✅ | ✅ | ✅ | Working |
| Net ordering (MST) | ✅ | ✅ | ❌ | Python only |
| Ripup and retry | ✅ | ❌ | ✅ | **Now implemented** |
| Push-and-shove | ✅ | ❌ | ✅ | **Now implemented** |
| Multi-pass optimization | ✅ | ❌ | ✅ | **Now implemented** |
| Fanout routing | ✅ | ❌ | ✅ | **Now implemented** |

### Path Optimization

| Feature | FreeRouting | Python Agent | C++ Library | Status |
|---------|-------------|--------------|-------------|--------|
| Path simplification | ✅ | ✅ | ✅ | **Working** |
| 45-degree corners | ✅ | ❌ | ✅ | **Now implemented** |
| PullTight 90° | ✅ | ❌ | ✅ | **Now implemented** |
| PullTight 45° | ✅ | ❌ | ✅ | **Now implemented** |
| PullTight any-angle | ✅ | ❌ | ✅ | **Now implemented** |
| Via optimization | ✅ | ❌ | ✅ | **Now implemented** |
| Pin neckdown | ✅ | ❌ | ✅ | **Now implemented** |

### Design Rules

| Feature | FreeRouting | Python Agent | C++ Library | Status |
|---------|-------------|--------------|-------------|--------|
| Fixed clearance | ✅ | ✅ 0.2mm | ✅ | Working |
| Clearance matrix | ✅ | ❌ | ✅ | **Now implemented** |
| Net class rules | ✅ | ❌ | ✅ | **Now implemented** |
| Via rules | ✅ | ❌ | ✅ | **Now implemented** |
| Keepout zones | ✅ | ❌ | ✅ | **Now implemented** |
| DRC integration | ✅ | ❌ | ❌ | Not started |

### Board Integration

| Feature | FreeRouting | Python Agent | C++ Library | Status |
|---------|-------------|--------------|-------------|--------|
| Read footprints | ✅ | ✅ | ✅ | **Working** |
| Read existing tracks | ✅ | ✅ | ✅ | **Working** |
| Read existing vias | ✅ | ✅ | ✅ | **Working** |
| Pad position transform | ✅ | ✅ | ✅ | **Working** |
| Insert tracks | ✅ | ✅ | ✅ | **Direct board insertion** |
| Insert vias | ✅ | ✅ | ✅ | **Direct board insertion** |
| Commit support | ✅ | ❌ | ✅ | **Working** |
| Undo support | ✅ | ❌ | ✅ | Via commit |

---

## Remaining Work to Match FreeRouting

### High Priority (Completed)

1. ✅ **PullTight Optimization** - `optimize/pull_tight.cpp/h`
   - Vertex moving to reduce path length
   - Three modes: 90°, 45°, any-angle
   - Collision detection during optimization
   - Integrated into path reconstruction

2. ✅ **Net Ordering Optimization** - `autoroute_engine.cpp`
   - Route simpler nets first (fewer pads, shorter distances)
   - Power/ground nets routed last
   - Failed nets retried after successful routes complete
   - Up to 3 retry passes

3. ✅ **Multi-Pass Optimization** - `autoroute_engine.cpp`, `autoroute_control.h`
   - Configurable number of passes (default 3)
   - Pass-specific via cost (decreases each pass)
   - Ripup enabled in later passes
   - Retry failed nets with progressively aggressive settings

4. ✅ **Congestion-Aware Routing** - `search/congestion_map.cpp/h`
   - Grid-based congestion tracking
   - Records routed segments and vias
   - Penalizes routes through congested areas
   - Spreads traces more evenly across board

### Medium Priority (Completed)

1. ✅ **Blind/Buried Via Support** - `autoroute_control.h`, `insert_connection.cpp`, `locate_connection.h`
   - AUTOROUTE_VIA_TYPE enum (THROUGH, BLIND_TOP, BLIND_BOTTOM, BURIED, MICROVIA)
   - VIA_TYPE_CONFIG with layer ranges and cost multipliers
   - GetBestViaType() selects optimal via for layer transition
   - Insert vias with proper layer spans

2. ✅ **Net Class Support** - `autoroute_control.h`, `autoroute_engine.cpp`
   - NET_CLASS_CONFIG with trace width, clearance, via sizes
   - Per-net lookup methods (GetNetTraceWidth, GetNetClearance, etc.)
   - Priority-based routing order
   - Power/ground nets with wider traces

3. ✅ **Clearance Matrix** - `autoroute_control.h`
   - CLEARANCE_ENTRY for net class pair clearances
   - GetClearanceBetweenNets() for pair lookups
   - Falls back to max of individual clearances

4. ✅ **Via Optimization** - `optimize/via_optimizer.cpp/h`
   - RemoveUnnecessaryVias: removes vias where layer doesn't change
   - OptimizeViaPositions: moves vias to reduce path length
   - MergeShortSegments: removes vias with short segments on both sides
   - Integrated into locate_connection path reconstruction

5. ✅ **Fanout Routing** - `fanout/fanout_router.cpp/h`
   - Multiple patterns: DIRECT, STAGGERED, DOG_BONE, CHANNEL
   - Escape direction calculation based on pad position
   - Collision-checked via and trace placement
   - Support for BGA and QFP packages

### Lower Priority (Completed)

1. ✅ **Differential Pair Routing** - `differential/differential_pair.cpp/h`
   - Route paired signals together with constant spacing
   - Automatic net pairing by _P/_N suffix convention
   - Coupled trace generation with configurable gap
   - Length matching between positive and negative signals
   - Serpentine generation for length equalization

2. ✅ **Length Matching** - `length_match/length_matcher.cpp/h`
   - Match trace lengths within configurable tolerance
   - Multiple meander styles: trapezoidal, rectangular, rounded
   - Automatic amplitude and spacing calculation
   - Collision-checked serpentine placement
   - Best segment selection for meander insertion

3. ✅ **Progress Reporting** - `autoroute_engine.cpp/h`
   - AUTOROUTE_PROGRESS struct with detailed status
   - SetProgressCallback for real-time updates
   - Cancel() method for thread-safe cancellation
   - Per-net and per-pass progress tracking
   - Elapsed time and completion percentage

### Remaining Work

1. **Object Pooling**
   - Reuse `MAZE_LIST_ELEMENT` objects
   - Reduce memory allocation overhead
   - Files: `maze_search.cpp/h`

2. **Multi-Threaded Routing**
   - Route independent nets in parallel
   - Lock shared data structures
   - Files: `autoroute_engine.cpp`

---

## Architecture

### Zener C++ Class Hierarchy

```
AUTOROUTER (public API)
└── AUTOROUTE_ENGINE
    ├── EXPANSION_ROOM (abstract)
    │   ├── FREE_SPACE_ROOM (complete)
    │   ├── INCOMPLETE_FREE_SPACE_ROOM (dynamic)
    │   ├── OBSTACLE_ROOM
    │   └── TARGET_ROOM
    ├── EXPANSION_DOOR
    ├── EXPANSION_DRILL
    ├── SHAPE_SEARCH_TREE (spatial index)
    ├── ROOM_COMPLETION (dynamic room builder)
    └── MAZE_SEARCH
        ├── MAZE_LIST_ELEMENT
        ├── DESTINATION_DISTANCE
        └── RIPUP_CHECKER (ripup + shove)

TILE_SHAPE (geometry)
├── INT_BOX (axis-aligned box)
├── CONVEX_POLY_SHAPE
├── HALF_PLANE
└── SIMPLEX (half-plane intersection)

LOCATE_CONNECTION (path reconstruction)
├── PATH_POINT
├── PATH_SEGMENT
└── ROUTING_PATH

INSERT_CONNECTION (board insertion)
└── INSERT_RESULT

AUTOROUTE_CONTROL (configuration)
AUTOROUTE_RESULT (statistics)
AUTOROUTE_PROGRESS (progress reporting)

VIA_OPTIMIZER (via count reduction)
├── RemoveUnnecessaryVias
├── OptimizeViaPositions
└── MergeShortSegments

FANOUT_ROUTER (BGA/QFP escape routing)
├── FANOUT_CONNECTION
└── FANOUT_PATTERN (DIRECT, STAGGERED, DOG_BONE, CHANNEL)

DIFF_PAIR_ROUTER (differential pair routing)
├── DIFF_PAIR_CONNECTION
├── DIFF_PAIR_PATH
└── DIFF_PAIR_CONFIG

LENGTH_MATCHER (length matching)
├── LENGTH_MATCH_TARGET
├── LENGTH_MATCH_RESULT
└── MEANDER_STYLE (TRAPEZOIDAL, ROUNDED, RECTANGULAR)
```

### Key Algorithms

1. **Dynamic Room Expansion** (`room_completion.cpp`)
   - Start with incomplete rooms adjacent to obstacles
   - Complete rooms on-demand during maze search
   - Sort neighbors counterclockwise around room border
   - Create doors to adjacent rooms and obstacles
   - Extend rooms to board bounds when no neighbors

2. **45-Degree Corner Optimization** (`locate_connection.cpp`)
   - Detect 90-degree turns in path
   - Calculate chamfer distance (40% of segment length)
   - Insert two 45-degree segments instead of one 90-degree turn
   - Filter out too-short segments

3. **Delayed Occupation** (`maze_search.cpp`)
   - Track doors that are occupied during search
   - On failure, retry with occupied doors cleared
   - Apply cost penalty to discourage using blocked paths
   - Up to 3 retry attempts

4. **Push-and-Shove** (`ripup_checker.cpp`)
   - Calculate perpendicular shove direction
   - Try moving blocking trace by clearance + trace width
   - Prefer shove over ripup (preserves routing)
   - Apply shoves via commit

5. **Pin Neckdown** (`insert_connection.cpp`)
   - Detect when trace endpoint is at a pad
   - Calculate max width that fits pad minus clearance
   - Create tapered transition segment

---

## File Structure

```
pcbnew/autoroute/
├── autoroute_engine.cpp/h      # Main engine, room model, net ordering, multi-pass
├── autoroute_control.h         # Configuration: net classes, via types, clearance matrix
├── expansion/
│   ├── expansion_room.cpp/h    # Room classes (free space, obstacle, target)
│   ├── expansion_door.cpp/h    # Door between rooms
│   └── expansion_drill.cpp/h   # Via/drill locations
├── geometry/
│   └── tile_shape.cpp/h        # INT_BOX, HALF_PLANE, SIMPLEX
├── search/
│   ├── maze_search.cpp/h       # A* algorithm, congestion-aware costs
│   ├── maze_list_element.h     # Priority queue entry
│   ├── destination_distance.cpp/h # Heuristic calculator
│   ├── shape_search_tree.cpp/h # Spatial index (grid-based)
│   ├── room_completion.cpp/h   # Dynamic room completion
│   ├── ripup_checker.cpp/h     # Ripup and shove logic
│   └── congestion_map.cpp/h    # Congestion tracking for spread routing
├── locate/
│   └── locate_connection.cpp/h # Path reconstruction, 45° optimization, via optimization
├── insert/
│   └── insert_connection.cpp/h # Board insertion, neckdown, blind/buried vias
├── optimize/
│   ├── pull_tight.cpp/h        # PullTight path optimization (90°/45°/any)
│   └── via_optimizer.cpp/h     # Via count reduction and position optimization
├── fanout/
│   └── fanout_router.cpp/h     # BGA/QFP fanout routing with escape patterns
├── differential/
│   └── differential_pair.cpp/h # Differential pair routing with length matching
├── length_match/
│   └── length_matcher.cpp/h    # Length matching with serpentine patterns
└── AUTOROUTING.md              # This file
```

---

## Testing

### Test Cases Needed
1. Simple 2-pad net (straight line)
2. 2-pad net with obstacle (L-shape)
3. Multi-pad net (star topology)
4. Via required (pads on different layers)
5. Blocked route (no solution, triggers retry)
6. Blocked route (triggers push-and-shove)
7. Large net (many pads)
8. Dense board (many obstacles)
9. Pin neckdown (large trace to small pad)
10. Keepout zone avoidance

---

## FreeRouting Algorithm Deep Dive

This section documents the low-level technical details of FreeRouting's algorithms to ensure
Zener's implementation matches the original behavior.

### A* Maze Search Algorithm

FreeRouting's `MazeSearchAlgo` implements a modified A* algorithm:

**Priority Queue**
- Uses a `PriorityQueue<MazeListElement>` ordered by `sorting_value` (ascending)
- Elements extracted via `poll()` in O(log n) time
- Already-occupied sections are skipped and recycled

**MazeListElement Fields**
```java
door                        // Current EXPANDABLE_OBJECT (door or drill)
section_no_of_door          // Section within the door (for multi-section doors)
backtrack_door              // Previous door in path (for reconstruction)
section_no_of_backtrack_door
expansion_value             // g(n): Actual cost from start to this node
sorting_value               // f(n) = g(n) + h(n): Used for priority ordering
next_room                   // Room we're expanding into
shape_entry                 // Entry geometry (TileShape)
room_ripped                 // Whether this path requires ripup
adjustment                  // Shove adjustment info
already_checked             // Prevents re-expansion
```

**Cost Calculation**
```
expansion_value = previous_expansion_value + trace_cost + via_cost + ripup_penalty
sorting_value = expansion_value + heuristic_distance_to_destination
```

Where:
- `trace_cost` = weighted Manhattan distance (layer-specific weights)
- `via_cost` = `add_via_costs` parameter (configurable per via type)
- `ripup_penalty` = base_ripup_cost × detour_factor × fanout_protection_multiplier
- `heuristic` = `destination_distance.calculate()` (octile distance)

**Search Termination**
FreeRouting terminates when:
1. Destination door is reached: `curr_door.is_destination_door()`
2. Queue becomes empty (no path exists)
3. User requests stop: `autoroute_engine.is_stop_requested()`
4. Fanout-specific: First drill expansion when both doors are drills

**Key Difference from Zener**: FreeRouting does NOT have a hard node expansion limit.
It relies on user cancellation and natural queue exhaustion. Zener adds safety limits
(MAX_NODES_EXPANDED = 50000) to prevent hangs in edge cases.

### Expansion Room Model

**Room Types**
```
ExpansionRoom (abstract)
├── FreeSpaceExpansionRoom
│   ├── IncompleteFreeSpaceExpansionRoom  // Unbounded, completed on-demand
│   └── CompleteFreeSpaceExpansionRoom    // Bounded, doors calculated
├── ObstacleExpansionRoom                  // Pads, tracks, zones
└── TargetExpansionRoom                    // Destination pads
```

**Incomplete Room Model**
- Shape can be null (meaning "the whole plane")
- Has a "contained shape" that must be preserved in the completed room
- Completed lazily when first accessed during maze search

**Room Completion Algorithm** (`complete_expansion_room`)
1. Get the incomplete room's unbounded shape
2. Call `search_tree.complete_shape()` to find obstacle boundaries
3. Calculate final shape that avoids all obstacles while staying as large as possible
4. Create doors to neighboring rooms via `calculate_doors()`
5. Door algorithm varies by routing angle (90°, 45°, arbitrary)

**Door Calculation Complexity**
- FreeRouting uses door "snapshots" before iteration
- Improves from O(N²) to O(N) by avoiding restarts

**Database Persistence**
FreeRouting's `maintain_database` mode:
- Completed rooms persist across connections
- Net-dependent rooms invalidated selectively
- Avoids full recalculation after each route

**Key Difference from Zener**: Zener rebuilds the room model for each net. FreeRouting
caches completed rooms across nets, which is faster but more complex.

### Drill/Via Management

**DrillPageArray**
- 2D array of rectangular pages containing ExpansionDrill objects
- Pages invalidated when expansion room shapes change
- Recalculated on next access (lazy evaluation)
- Prevents recalculating entire drill database after each room completion

**Zener Simplification**: Uses a single list of drills with a spatial query.
May be slower for very large boards but simpler to implement.

### Batch Autorouting Strategy

**Net Ordering**
- FreeRouting v2.3+ uses **natural board order** (disabled earlier optimizations)
- Comment: "Disabled because it negatively impacts convergence compared to v1.9"
- Earlier versions sorted by airline distance

**Multi-Pass Approach**
```
for pass = 1 to maxPasses:
    ripup_cost = start_ripup_costs × pass_number
    time_limit = 100000 × 2^(pass_number - 1) ms  // Exponential increase

    for each unrouted_connection:
        if failure_count > threshold:
            skip_with_log
        else:
            attempt_route(ripup_cost, time_limit)
```

**Failure Handling**
- Failed items tracked in failure log
- Items exceeding failure threshold are skipped
- Board history maintained for state restoration
- `MAXIMUM_TRIES_ON_THE_SAME_BOARD = 3`

**Stagnation Detection**
- After pass 8, check every 4 passes (`STOP_AT_PASS_MODULO = 4`)
- If no improvement and board rank > 50, terminate
- Prevents infinite loops on difficult boards

**Key Difference from Zener**: Zener uses fixed pass counts and uniform time limits.
FreeRouting uses exponentially increasing time limits and stagnation detection.

### Safeguards and Limits

**FreeRouting's Approach**
```java
MAX_RIPUP_COSTS = Integer.MAX_VALUE / 100  // Prevents integer overflow
max_shove_trace_recursion_depth = 5        // Limits nested shove operations
max_spring_over_recursion_depth = 5        // Limits nested spring operations
is_stop_requested()                         // User cancellation check
```

**Zener's Additional Safeguards** (Not in FreeRouting)
```cpp
MAX_NODES_EXPANDED = 50000      // Prevents infinite search
MAX_ROOM_COMPLETIONS = 1000     // Limits dynamic room expansion
MAX_INCOMPLETE_ROOMS = 10000    // Limits initial room creation
MAX_DRILLS = 5000               // Limits via location count
m_cancelled flag                 // Thread-safe cancellation
```

These limits are practical safeguards for edge cases but may cause early termination
on complex boards. Consider raising limits or making them configurable if routes fail.

### Cost Function Details

**Trace Cost**
```
trace_cost = manhattan_distance × layer_weight × direction_penalty
```
Where `layer_weight` allows preferring certain layers.

**Via Cost**
```
via_cost = base_via_cost × layer_transition_multiplier
```
Configurable per via type (through, blind, buried, microvia).

**Ripup Cost**
```
ripup_cost = base_cost × detour_factor × fanout_protection
```
Where:
- `detour_factor` = estimated additional distance if routed around
- `fanout_protection` = multiplier to protect recently placed fanout

**Congestion Cost** (FreeRouting's spread routing)
```
congestion_cost = segment_length × congestion_factor[cell]
```
Where `congestion_factor` increases as more traces pass through a grid cell.

### Heuristic Function

FreeRouting uses **octile distance** (8-connected):
```
h(n) = max(|dx|, |dy|) + (√2 - 1) × min(|dx|, |dy|)
```

This accounts for 45-degree routing and provides an admissible heuristic.

Zener currently uses **Manhattan distance**:
```
h(n) = |dx| + |dy|
```

This is also admissible but less tight for 45-degree paths.

### Performance Characteristics

**FreeRouting Optimizations**
1. Room database persistence across nets
2. Page-based drill management
3. Door snapshots before iteration
4. Natural net ordering (avoids sorting overhead)
5. Lazy room completion

**Zener Trade-offs**
1. Simpler room model (rebuilt per net)
2. Single drill list with grid queries
3. Standard iteration
4. Priority-based net ordering
5. Safety limits may cause early termination

### Known Deviations from FreeRouting

| Aspect | FreeRouting | Zener | Impact |
|--------|-------------|-------|--------|
| Node limit | None (queue exhaustion) | 50,000 | May miss complex routes |
| Room persistence | Cached across nets | Rebuilt per net | Slower on large boards |
| Drill pages | 2D page array | Single list | O(n) vs O(1) lookup |
| Net ordering | Natural order | Priority sorted | Different routing order |
| Time limit | Exponential per pass | Fixed | Less adaptive |
| Stagnation | Detected at pass 8+ | Not implemented | May run longer |
| Heuristic | Octile distance | Manhattan | Slightly less optimal |

---

## Current Implementation Issues (February 2026)

This section documents active debugging work and issues for agents continuing this work.

### Critical Issue: Queue Overflow

The A* maze search queue grows to 100,000+ elements and overflows. Root causes identified:

1. **Null next_room elements** - Elements with `next_room=nullptr` were being added to queue
   - **Fixed**: Added null checks in `InitializeSearch`, `ExpandToDoorSection`, `ExpandToOtherLayers`

2. **ExpandToDrillsInRoom causing explosion** - Every room expansion was adding ALL drills
   on the board to the queue (hundreds per room)
   - **Temporarily disabled**: FreeRouting uses `DrillPage` abstraction to solve this
   - See "Missing DrillPage Implementation" below

3. **Rooms with 0 doors** - Completed rooms sometimes have no doors, creating dead ends
   - Need investigation: Why are doorless rooms being created?

### Missing: DrillPage Implementation

FreeRouting prevents drill explosion with a critical abstraction we don't have:

**FreeRouting's Approach** (see `/tools/freerouting/src/main/java/app/freerouting/autoroute/DrillPage.java`):
```java
// DrillPageArray divides board into rectangular pages
// Each page contains drills within its bounds
public class DrillPageArray {
    DrillPage[][] arr;  // 2D grid of pages

    Collection<DrillPage> overlapping_pages(TileShape room_shape);
}

// In expand_to_room_doors:
Collection<DrillPage> overlapping_drill_pages =
    drill_page_array.overlapping_pages(room.get_shape());
for (DrillPage page : overlapping_drill_pages) {
    expand_to_drill_page(page, from_element);  // Add PAGE, not individual drills
}

// Only when processing a DrillPage element:
if (list_element.door instanceof DrillPage) {
    expand_to_drills_of_page(list_element);  // NOW expand to individual drills
    return true;
}
```

**Why This Matters**:
- Instead of N drills per room → M drill pages (where M << N)
- Only expands to individual drills when we actually REACH that page
- Drastically reduces queue size

**To Implement**:
1. Create `DrillPage` class as `EXPANDABLE_OBJECT` subclass
2. Create `DrillPageArray` grid structure
3. Modify `ExpandToRoomDoors` to expand to pages, not drills
4. Add `DrillPage` handling in `OccupyNextElement`

### Key Differences from FreeRouting Found During Debugging

| Issue | FreeRouting | Zener (Current) | Impact |
|-------|-------------|-----------------|--------|
| Queue deduplication | Allows duplicates, skips if occupied when popped | Was using `m_inQueue` set (removed) | Fixed - now matches FR |
| Occupation timing | Set AFTER expansion | Was setting BEFORE (fixed) | Fixed - now matches FR |
| Drill expansion | Via DrillPage abstraction | Direct per-room (disabled) | **Needs DrillPage impl** |
| Node limits | None | MAX_QUEUE_SIZE=100000 | May need raising |

### Debug Macros

The codebase has extensive debug logging (can be noisy):

```cpp
// In maze_search.cpp
#define MAZE_DEBUG( msg ) std::cerr << "[MAZE] " << msg << std::endl

// In autoroute_engine.cpp
#define AUTOROUTE_DEBUG( msg ) std::cerr << "[AUTOROUTE] " << msg << std::endl

// In room_completion.cpp
#define COMPLETION_DEBUG( msg ) std::cerr << "[COMPLETION] " << msg << std::endl
```

To reduce noise, comment out the `#define` lines or make them conditional.

### FreeRouting Source Reference

FreeRouting code is available locally at:
```
/Users/gmp/workspaces/zener-design/tools/freerouting/
```

Key files for maze search:
- `src/main/java/app/freerouting/autoroute/MazeSearchAlgo.java` - Main A* algorithm
- `src/main/java/app/freerouting/autoroute/MazeListElement.java` - Queue element
- `src/main/java/app/freerouting/autoroute/MazeSearchElement.java` - Per-door occupation tracking
- `src/main/java/app/freerouting/autoroute/DrillPage.java` - Drill page abstraction
- `src/main/java/app/freerouting/autoroute/DrillPageArray.java` - Page grid
- `src/main/java/app/freerouting/autoroute/ExpansionDoor.java` - Door with sections
- `src/main/java/app/freerouting/autoroute/ExpandableObject.java` - Base interface

### February 2026 Progress Update

**2D Overlap Fix (FindNeighbours)**:
- Problem: `FindNeighbours` was skipping all items with 2D overlap (`skipAreaOverlap=131`)
- The incomplete rooms extended to board bounds and overlapped obstacles in 2D
- Fix: Treat 2D overlaps as neighbors too - create doors at obstacle boundaries
- Result: Neighbors now found (e.g., `neighbours=521`), paths are found

**Pad Collision Detection Fix (InsertTrackSegment)**:
- Problem: Traces were going through pads from other nets
- `ValidateSegment()` only checked tracks and keepout zones, not pads
- Fix: Added `SegmentCrossesPad()` to check pad collisions before insertion
- File: `insert/insert_connection.cpp`

**Current Status**:
- ✅ A* maze search finds paths (FOUND PATH, nodes=56)
- ✅ Neighbors being detected (neighbours=521+)
- ✅ Doors being created between rooms
- ✅ Traces being inserted to board
- ✅ Pad collision detection added
- ⚠️ No vias/layer changes (drill expansion disabled - needs DrillPage)
- ⚠️ Variable width traces from neckdown feature

### Next Steps for Agents

1. **Implement DrillPage** - Critical for via/layer-transition routing
2. **Test pad collision fix** - Verify traces no longer cross other-net pads
3. **Consider raising MAX_QUEUE_SIZE** - 100K may be too low for complex boards
4. **Add occupancy reset between nets** - Ensure doors are cleared before each net
5. **Tune neckdown logic** - Currently may create unnecessary width transitions

### Build Commands

```bash
# Fast incremental build (agent module only)
./mac_build_fast.sh

# Full build with install
./mac_build_hard.sh

# With debug tracing
WXTRACE=KICAD_AGENT "/path/to/Zener.app/Contents/MacOS/Zener"
```

---

## References

- [FreeRouting Source](https://github.com/freerouting/freerouting)
- **Local copy**: `/Users/gmp/workspaces/zener-design/tools/freerouting/`
- [FreeRouting MazeSearchAlgo.java](src/main/java/app/freerouting/autoroute/MazeSearchAlgo.java)
- [FreeRouting CompleteFreeSpaceExpansionRoom.java](src/main/java/app/freerouting/autoroute/CompleteFreeSpaceExpansionRoom.java)
- [FreeRouting LocateFoundConnectionAlgo45Degree.java](src/main/java/app/freerouting/autoroute/LocateFoundConnectionAlgo45Degree.java)
- [FreeRouting MazeShoveTraceAlgo.java](src/main/java/app/freerouting/autoroute/MazeShoveTraceAlgo.java)
- [FreeRouting BatchAutorouter.java](src/main/java/app/freerouting/autoroute/BatchAutorouter.java)
- [FreeRouting AutorouteEngine.java](src/main/java/app/freerouting/autoroute/AutorouteEngine.java)
- [A* Algorithm](https://en.wikipedia.org/wiki/A*_search_algorithm)
