# PCB Autorouting Implementation - FreeRouting Alignment

This document provides a comprehensive implementation plan to align Zener's autorouter with FreeRouting's proven architecture.

## Executive Summary

After extensive analysis of FreeRouting's source code and Zener's current implementation, we've identified **critical architectural differences** that cause the autorouter to fail. This document provides an implementation roadmap to fix these issues.

**Current Status**: Autorouter hangs or produces incorrect routes due to fundamental architectural mismatches with FreeRouting.

**Target State**: Full alignment with FreeRouuting's expansion room model for reliable routing.

---

## Architectural Comparison

### Overview of Key Differences

| Aspect | FreeRouting | Zener (Current) | Impact |
|--------|-------------|-----------------|--------|
| **Search Tree Contents** | Board Items (pads, traces, vias) | OBSTACLE_ROOM wrappers | Extra abstraction layer breaks queries |
| **Obstacle Rooms** | Created on-demand via ItemAutorouteInfo | Pre-created for all items | Memory overhead, wrong query results |
| **Room Completion** | Queries Items, creates obstacle rooms lazily | Queries obstacle rooms | Returns wrong neighbor set |
| **DrillPageArray** | 2D page grid for lazy drill expansion | Flat list (expansion disabled) | Queue explosion, no vias |
| **Target Doors** | TargetItemExpansionDoor to destination Items | TARGET_ROOM type (not integrated) | Cannot reach destinations |
| **Database Persistence** | Cached across connections | Rebuilt per net | Performance loss |
| **Shape Completion** | ShapeSearchTree.complete_shape() | GrowFromCenter() | Different algorithm |

---

## Implementation Plan

### Phase 1: Search Tree Architecture (Critical)

**Current Problem**: Zener stores `OBSTACLE_ROOM` objects in the search tree. FreeRouting stores board Items directly.

**Why It Matters**: When `SortedRoomNeighbours.calculate()` queries the search tree, it expects to receive Items (traces, pads, vias), not pre-wrapped obstacle rooms. The current approach breaks:
1. Neighbor detection (queries return obstacle rooms, not Items)
2. Net filtering (obstacle rooms have net codes, but filtering logic differs)
3. Ripup/shove (requires access to actual Items)

#### 1.1 Remove Pre-Created Obstacle Rooms

**Files**: `autoroute_engine.cpp`, `autoroute_engine.h`

**Current**:
```cpp
void AUTOROUTE_ENGINE::BuildObstacleRooms()
{
    // Creates OBSTACLE_ROOM for every pad, track, zone
    for( PAD* pad : fp->Pads() )
    {
        auto room = std::make_unique<OBSTACLE_ROOM>( shape, pad, layer );
        m_rooms.push_back( std::move( room ) );
    }
}
```

**FreeRouting Approach**:
- Items (pads, traces, vias) are in the search tree directly via `ShapeSearchTree.insert(Item)`
- No obstacle rooms are pre-created
- `ObstacleExpansionRoom` is created on-demand only when needed for ripup/shove

**Implementation**:
1. Remove `BuildObstacleRooms()` entirely
2. Insert board Items directly into search tree
3. Create `ItemAutorouteInfo` equivalent for lazy obstacle room creation

#### 1.2 Modify Shape Search Tree

**Files**: `search/shape_search_tree.h`, `search/shape_search_tree.cpp`

**Current**:
```cpp
struct TREE_ENTRY
{
    EXPANSION_ROOM* room;      // Currently stores obstacle rooms
    BOARD_ITEM*     item;
    BOX2I           bounds;
    int             layer;
};
```

**FreeRouting Approach**:
```java
// ShapeSearchTree stores SearchTreeObject, which includes:
// - Item (trace, via, pad, etc.)
// - CompleteFreeSpaceExpansionRoom (after completion)
// - ObstacleExpansionRoom is NOT stored - it wraps Items
```

**Implementation**:
1. Store `BOARD_ITEM*` as primary entry (not `EXPANSION_ROOM*`)
2. Add optional `EXPANSION_ROOM*` for completed free space rooms
3. Query methods return Items, not rooms
4. Add `is_trace_obstacle(net_no)` equivalent for net filtering

#### 1.3 Add ItemAutorouteInfo

**Files**: New file `expansion/item_autoroute_info.h/.cpp`

FreeRouting's `ItemAutorouteInfo` provides:
- Lazy creation of `ObstacleExpansionRoom` for an Item
- Caching of expansion rooms per shape index
- Door management for ripup/shove

```cpp
class ITEM_AUTOROUTE_INFO
{
public:
    ITEM_AUTOROUTE_INFO( BOARD_ITEM* aItem );

    // Get or create obstacle room for this item's shape
    OBSTACLE_ROOM* GetExpansionRoom( int aShapeIndex, SHAPE_SEARCH_TREE& aTree );

    // Reset for new connection
    void ResetDoors();

private:
    BOARD_ITEM* m_item;
    std::vector<std::unique_ptr<OBSTACLE_ROOM>> m_expansionRooms;
};
```

---

### Phase 2: Room Completion Algorithm (Critical)

**Current Problem**: `ROOM_COMPLETION::Complete()` queries for `OBSTACLE_ROOM`s and tries to create doors to them. FreeRouting queries for Items and handles them differently.

#### 2.1 Rewrite FindNeighbours

**File**: `search/room_completion.cpp`

**Current**:
```cpp
std::vector<ROOM_NEIGHBOUR> ROOM_COMPLETION::FindNeighbours( const INT_BOX& aShape,
                                                              int aLayer, int aNetCode )
{
    m_searchTree.QueryOverlapping( queryBounds, aLayer,
        [&]( const TREE_ENTRY& entry ) -> bool
        {
            // Gets obstacle rooms, not items
            if( entry.room && entry.room->GetNetCode() == aNetCode )
                return true;  // Skip same net
            // ...
        } );
}
```

**FreeRouting Approach** (from `SortedRoomNeighbours.calculate_neighbours()`):
```java
Collection<ShapeTree.TreeEntry> overlapping_objects = new LinkedList<>();
p_autoroute_search_tree.overlapping_tree_entries(room_shape, p_room.get_layer(), overlapping_objects);

for (ShapeTree.TreeEntry curr_entry : overlapping_objects) {
    SearchTreeObject curr_object = (SearchTreeObject) curr_entry.object;

    // Skip same room
    if (curr_object == p_room) continue;

    // For incomplete rooms, delay processing own-net items
    if (p_room instanceof IncompleteFreeSpaceExpansionRoom &&
        !curr_object.is_trace_obstacle(p_net_no)) {
        result.own_net_objects.add(curr_entry);  // Save for target doors later
        continue;
    }

    // Get shape and calculate intersection
    TileShape curr_shape = curr_object.get_tree_shape(p_autoroute_search_tree, curr_entry.shape_index_in_object);
    TileShape intersection = room_shape.intersection(curr_shape);
    int dimension = intersection.dimension();

    if (dimension > 1) {
        // 2D overlap - only for ObstacleExpansionRoom
        if (completed_room instanceof ObstacleExpansionRoom && curr_object instanceof Item) {
            // Create overlap door between obstacle rooms
            ObstacleExpansionRoom curr_overlap_room = item_info.get_expansion_room(...);
            room.create_overlap_door(curr_overlap_room);
        }
        continue;
    }

    if (dimension == 1) {
        // 1D touch - create door
        // Get or create obstacle room from Item
        ExpansionRoom neighbour_room = null;
        if (curr_object instanceof Item curr_item) {
            if (curr_item.is_routable()) {
                ItemAutorouteInfo item_info = curr_item.get_autoroute_info();
                neighbour_room = item_info.get_expansion_room(...);
            }
        }
        if (neighbour_room != null) {
            ExpansionDoor new_door = new ExpansionDoor(completed_room, neighbour_room, 1);
            // ...
        }
    }
}
```

**Implementation**:
1. Query returns Items, not obstacle rooms
2. Use `is_trace_obstacle(net_no)` for filtering
3. Save own-net Items for target door creation
4. Create `OBSTACLE_ROOM` on-demand when needed for door creation
5. Handle 1D (edge touch) vs 2D (area overlap) differently

#### 2.2 Add Target Door Mechanism

**Files**: `expansion/target_door.h/.cpp` (new), `expansion/expansion_room.h`

FreeRouting creates special "target doors" to destination Items:

```java
private static void calculate_target_doors(CompleteFreeSpaceExpansionRoom p_room,
                                            Collection<ShapeTree.TreeEntry> p_own_net_objects,
                                            AutorouteEngine p_autoroute_engine) {
    for (ShapeTree.TreeEntry curr_entry : p_own_net_objects) {
        if (curr_entry.object instanceof Connectable curr_object) {
            if (curr_object.contains_net(p_autoroute_engine.get_net_no())) {
                TileShape curr_connection_shape = curr_object.get_trace_connection_shape(...);
                if (p_room.get_shape().intersects(curr_connection_shape)) {
                    TargetItemExpansionDoor new_target_door = new TargetItemExpansionDoor(
                        curr_item, curr_entry.shape_index_in_object, p_room, ...);
                    p_room.add_target_door(new_target_door);
                }
            }
        }
    }
}
```

**Implementation**:
```cpp
class TARGET_EXPANSION_DOOR : public EXPANSION_DOOR
{
public:
    TARGET_EXPANSION_DOOR( BOARD_ITEM* aItem, int aShapeIndex,
                           FREE_SPACE_ROOM* aRoom, SHAPE_SEARCH_TREE& aTree );

    BOARD_ITEM* GetItem() const { return m_item; }
    bool IsDestination() const { return true; }

private:
    BOARD_ITEM* m_item;
    int m_shapeIndex;
};

// Add to FREE_SPACE_ROOM:
std::vector<TARGET_EXPANSION_DOOR*> m_targetDoors;
void AddTargetDoor( TARGET_EXPANSION_DOOR* aDoor );
const std::vector<TARGET_EXPANSION_DOOR*>& GetTargetDoors() const;
```

---

### Phase 3: DrillPage Implementation (Critical for Vias)

**Current Problem**: Drill expansion disabled because adding all drills to queue causes explosion. FreeRouting uses DrillPage abstraction.

#### 3.1 Implement DrillPage

**Files**: `expansion/drill_page.h/.cpp` (new)

```cpp
class DRILL_PAGE : public EXPANDABLE_OBJECT
{
public:
    DRILL_PAGE( const BOX2I& aBounds, int aFirstLayer, int aLastLayer );

    // From EXPANDABLE_OBJECT
    VECTOR2I GetCenter() const override;
    int GetLayer() const override { return -1; }  // Multi-layer
    int GetSectionCount() const override { return 1; }

    // Drill management
    void AddDrill( EXPANSION_DRILL* aDrill );
    const std::vector<EXPANSION_DRILL*>& GetDrills() const;

    // Lazy calculation
    bool IsCalculated() const { return m_calculated; }
    void Calculate( AUTOROUTE_ENGINE& aEngine );
    void Invalidate() { m_calculated = false; m_drills.clear(); }

private:
    BOX2I m_bounds;
    int m_firstLayer;
    int m_lastLayer;
    bool m_calculated = false;
    std::vector<EXPANSION_DRILL*> m_drills;
};
```

#### 3.2 Implement DrillPageArray

**Files**: `expansion/drill_page_array.h/.cpp` (new)

```cpp
class DRILL_PAGE_ARRAY
{
public:
    DRILL_PAGE_ARRAY( const BOX2I& aBoardBounds, int aMaxPageWidth );

    // Get pages overlapping a shape
    std::vector<DRILL_PAGE*> GetOverlappingPages( const BOX2I& aShape );

    // Invalidate pages after board change
    void Invalidate( const BOX2I& aShape );

    // Reset for new connection
    void Reset();

private:
    std::vector<std::vector<std::unique_ptr<DRILL_PAGE>>> m_pages;  // 2D grid
    int m_pageWidth;
    int m_pageHeight;
    int m_colCount;
    int m_rowCount;
};
```

#### 3.3 Modify Maze Search for DrillPage

**File**: `search/maze_search.cpp`

**Current** (disabled):
```cpp
void MAZE_SEARCH::ExpandToDrillsInRoom( EXPANSION_ROOM* aRoom, const MAZE_LIST_ELEMENT& aFromElement )
{
    // DISABLED - causes queue explosion
}
```

**FreeRouting Approach**:
```java
// In expand_to_room_doors():
Collection<DrillPage> overlapping_drill_pages =
    drill_page_array.overlapping_pages(room.get_shape());
for (DrillPage page : overlapping_drill_pages) {
    expand_to_drill_page(page, from_element);  // Expand to PAGE, not drills
}

// When processing a DrillPage element:
if (list_element.door instanceof DrillPage) {
    expand_to_drills_of_page(list_element);  // NOW expand to individual drills
    return true;
}
```

**Implementation**:
```cpp
void MAZE_SEARCH::ExpandToRoomDoors( EXPANSION_ROOM* aRoom, const MAZE_LIST_ELEMENT& aFromElement )
{
    // ... expand to doors ...

    // Expand to drill pages (not individual drills)
    std::vector<DRILL_PAGE*> pages =
        m_engine.GetDrillPageArray().GetOverlappingPages( aRoom->GetBoundingBox() );

    for( DRILL_PAGE* page : pages )
    {
        ExpandToDrillPage( page, aFromElement );
    }
}

void MAZE_SEARCH::ExpandToDrillPage( DRILL_PAGE* aPage, const MAZE_LIST_ELEMENT& aFromElement )
{
    // Add page to queue with cost based on distance to page center
    MAZE_LIST_ELEMENT element;
    element.expandable = aPage;
    element.expansion_value = aFromElement.expansion_value + PageCost( aPage, aFromElement );
    element.sorting_value = element.expansion_value + m_destDistance.Calculate( aPage->GetCenter() );
    m_queue.push( element );
}

bool MAZE_SEARCH::OccupyNextElement()
{
    // ...

    if( auto* page = dynamic_cast<DRILL_PAGE*>( element.expandable ) )
    {
        // Now expand to individual drills within the page
        ExpandToDrillsOfPage( page, element );
        return false;  // Not at destination
    }

    // ...
}
```

---

### Phase 4: Maze Search Alignment

#### 4.1 Queue Element Handling

**Current Issue**: Zener uses a `m_delayedDoors` retry mechanism. FreeRouting allows duplicates in queue and skips when occupied.

**FreeRouting Approach**:
```java
// In occupy_next_element():
MazeListElement curr_element = this.maze_expansion_list.poll();
if (curr_element == null) return false;

// Check if already occupied
ExpansionDoor curr_door = curr_element.door;
if (curr_door.get_maze_search_element(curr_element.section_no_of_door).is_occupied) {
    // Already processed, recycle and continue
    curr_element.reset();
    return false;
}

// Mark as occupied and process
curr_door.get_maze_search_element(curr_element.section_no_of_door).set_occupied();
```

**Implementation**:
Remove the delayed occupation retry mechanism and use FreeRouting's simpler approach:
1. Allow duplicates in queue
2. Check occupation when popping
3. Skip if already occupied

#### 4.2 Destination Detection

**FreeRouting Approach**:
```java
if (curr_door.is_destination_door()) {
    this.destination_door = (TargetItemExpansionDoor) curr_element.door;
    return true;  // Found path!
}
```

**Implementation**:
Add destination check in `OccupyNextElement()`:
```cpp
if( auto* targetDoor = dynamic_cast<TARGET_EXPANSION_DOOR*>( element.door ) )
{
    m_destinationDoor = targetDoor;
    return true;  // Found path
}
```

---

### Phase 5: Minor Alignments

#### 5.1 Database Persistence (Performance)

FreeRouting has `maintain_database` mode that caches completed rooms across connections. This is a performance optimization, not critical for correctness.

```cpp
// Optional: Add to AUTOROUTE_ENGINE
bool m_maintainDatabase = false;

// In RouteConnection():
if( !m_maintainDatabase )
{
    ClearRoomModel();  // Current behavior
}
else
{
    ResetAllDoors();  // Keep rooms, reset occupation
    InvalidateNetDependentRooms( aNetCode );
}
```

#### 5.2 Octile Heuristic

FreeRouting uses octile distance (8-connected grid), Zener uses Manhattan. This affects optimality but not correctness.

```cpp
double DESTINATION_DISTANCE::CalculateOctile( const VECTOR2I& aFrom ) const
{
    // Octile distance: max(|dx|, |dy|) + (√2 - 1) × min(|dx|, |dy|)
    int dx = std::abs( aFrom.x - m_target.x );
    int dy = std::abs( aFrom.y - m_target.y );
    static constexpr double SQRT2_MINUS_1 = 0.41421356;
    return std::max( dx, dy ) + SQRT2_MINUS_1 * std::min( dx, dy );
}
```

---

## Design Challenges (Zener/KiCad Limitations)

### 1. BOARD_ITEM Metadata

**Challenge**: FreeRouting's `Item` class has `get_autoroute_info()` that returns cached autoroute data. KiCad's `BOARD_ITEM` does not have this.

**Options**:
1. **External Map**: `std::unordered_map<BOARD_ITEM*, std::unique_ptr<ITEM_AUTOROUTE_INFO>>` stored in `AUTOROUTE_ENGINE`
2. **Temporary Member**: Add `void* m_autorouteInfo` to `BOARD_ITEM` (requires modifying KiCad base classes)

**Recommendation**: Use external map in `AUTOROUTE_ENGINE`. Cleaner separation.

### 2. Shape Index

**Challenge**: FreeRouting items can have multiple shapes (e.g., a trace with multiple segments). Each has a `shape_index_in_object`. KiCad items typically have one bounding box.

**Options**:
1. **Ignore**: Use single shape per item (current approach)
2. **Segment Decomposition**: Break traces into per-segment entries

**Recommendation**: Start with single shape, add segment decomposition if needed for ripup.

### 3. Clearance Compensation

**Challenge**: FreeRouting applies clearance compensation in the search tree itself (`ShapeSearchTree` has clearance knowledge). Zener applies clearance when creating obstacle shapes.

**Options**:
1. **Keep Current**: Apply clearance to shapes before insertion (simpler)
2. **Tree Compensation**: Add clearance-aware queries to `SHAPE_SEARCH_TREE`

**Recommendation**: Keep current approach for now.

### 4. Thread Safety

**Challenge**: KiCad's `BOARD` is not thread-safe. FreeRouting's `maintain_database` mode requires careful synchronization.

**Recommendation**: Disable database persistence for now. Can add later with proper locking.

---

## Implementation Priority

### Phase 1: Critical Fixes (Must Have)

1. **Remove obstacle room pre-creation** - Stop storing obstacle rooms
2. **Store Items in search tree** - Direct item storage
3. **Rewrite FindNeighbours** - Query returns Items, not rooms
4. **Add ItemAutorouteInfo** - Lazy obstacle room creation
5. **Implement DrillPageArray** - Fix via expansion

### Phase 2: Correctness (Should Have)

6. **Add Target Doors** - Proper destination detection
7. **Fix queue handling** - Allow duplicates, skip when occupied
8. **Fix destination detection** - Check for TargetItemExpansionDoor

### Phase 3: Performance (Nice to Have)

9. **Database persistence** - Cache rooms across connections
10. **Octile heuristic** - Better path optimality

---

## File Changes Summary

| File | Change Type | Description |
|------|-------------|-------------|
| `autoroute_engine.cpp/h` | Major Rewrite | Remove BuildObstacleRooms, add item insertion, ItemAutorouteInfo storage |
| `search/shape_search_tree.cpp/h` | Major Rewrite | Store Items not rooms, add is_trace_obstacle query |
| `search/room_completion.cpp/h` | Major Rewrite | Query Items, create obstacle rooms lazily, add target doors |
| `search/maze_search.cpp/h` | Significant | DrillPage handling, destination detection, queue handling |
| `expansion/expansion_room.h` | Minor | Add target door support to FREE_SPACE_ROOM |
| `expansion/item_autoroute_info.h/.cpp` | New File | Lazy obstacle room creation per Item |
| `expansion/target_door.h/.cpp` | New File | TargetItemExpansionDoor equivalent |
| `expansion/drill_page.h/.cpp` | New File | DrillPage abstraction |
| `expansion/drill_page_array.h/.cpp` | New File | 2D grid of drill pages |

---

## Testing Plan

### Unit Tests

1. **Search Tree**
   - Insert item, query returns item
   - is_trace_obstacle filtering works
   - Item removal works

2. **Room Completion**
   - Returns correct neighbors
   - Creates obstacle rooms on demand
   - Target doors created for own-net items

3. **DrillPageArray**
   - Pages calculated correctly
   - Overlapping pages query works
   - Invalidation clears correct pages

### Integration Tests

1. **Simple Route** - 2 pads, no obstacles
2. **Route with Obstacles** - Pads with obstructions
3. **Via Required** - Pads on different layers
4. **Multi-Pad Net** - Star topology
5. **Dense Board** - Many obstacles

---

## Known Bugs (Second Audit - Feb 2026)

The following bugs were identified and fixed during the second audit:

### Bug 1: CalculateDoorsAndRooms Never Creates Doors to Obstacles ✅ FIXED

**File**: `search/room_completion.cpp`

**Problem**: In `CalculateDoorsAndRooms()`, when a neighbor is an Item (not a room), the code tried to get `neighbour_room` from `entry.room` which was null for Items. This meant doors to obstacle rooms were never created.

**Fix**: When `neighbour.neighbour_item` exists, get or create the obstacle room via `m_engine.GetItemAutorouteInfo()->GetExpansionRoom()`. The code now properly creates obstacle rooms on-demand and adds doors to them.

### Bug 2: Cost Calculation Error in ExpandToDrillsOfPage ✅ FIXED

**File**: `search/maze_search.cpp`

**Problem**: When expanding to drills of a page, the trace cost was calculated from the room's entry point to the drill, but the page element's expansion_value already included the cost to reach the page center. This caused double-counting of costs.

**Fix**: Changed to calculate trace cost from page center to drill, since the page element's expansion_value already includes the cost from room entry to page center.

### Bug 3: Gap Handling Between Neighbors Incomplete ✅ FIXED

**File**: `search/room_completion.cpp`

**Problem**: When there were gaps between neighbors on a room edge (non-contiguous obstacles), incomplete rooms were not created for those gaps, preventing expansion through free space.

**Fix**: Added gap detection and handling in `CalculateDoorsAndRooms()`. The code now sorts neighbors along each edge, finds gaps between them, and creates incomplete rooms with doors for each gap.

### Bug 4: OBSTACLE Room Same-Net Fall-Through (Deferred)

**File**: `search/room_completion.cpp`

**Status**: Not a current issue. `FindNeighbours` is only called from `Complete()` for FREE_SPACE rooms. `CompleteObstacle()` creates incomplete rooms for each edge without querying neighbors, so same-net filtering doesn't apply there. If future changes require neighbor queries for obstacle rooms, this should be revisited.

### Bug 5: Search Area Negative Size Edge Case ✅ FIXED

**File**: `search/room_completion.cpp`

**Problem**: In `FindClosestObstacleDistance()`, the search area calculation could produce zero or negative dimensions when the search point was at the board edge.

**Fix**: Added bounds checks before creating each search area. If the dimension would be <= 0, return 0 immediately (already at edge).

---

## Known Bugs (Third Audit - Feb 2026)

### Bug 6: ExpandToDrillPage Missing next_room ✅ FIXED

**File**: `search/maze_search.cpp`

**Problem**: When creating page elements in `ExpandToDrillPage()`, the `next_room` field was never set. This uninitialized value was then propagated to drill elements in `ExpandToDrillsOfPage()`, causing drills to have incorrect or null `next_room` which could break room expansion and destination detection.

**Fix**: Added `element.next_room = aFromElement.next_room;` in `ExpandToDrillPage()` to properly inherit the room context for drill expansion.

### Bug 7: Missing TARGET_EXPANSION_DOOR Creation ✅ FIXED

**Files**: `search/room_completion.cpp`, `search/maze_search.cpp`, `autoroute_engine.cpp`

**Problem**: The autorouter was timing out because destination pads were never connected to the expanding room graph. The issue manifested as:
1. `FindNeighbours()` found same-net items (destination pads) and stored them in `m_ownNetItems`
2. But `CalculateDoorsAndRooms()` never created `TARGET_EXPANSION_DOOR` entries for these items
3. Maze search only checked `m_destDistance.IsDestination(next_room)` which never matched
4. Result: Infinite room expansion with no path to destination

**Evidence from logs**: `FindNeighbours: done, queryCount=13 neighbours=0 skipSameNet=1` - same-net items were collected but ignored.

**Fix** (multi-part):
1. **room_completion.h/cpp**: Added `SetDestinations()` method and `m_destItems` storage. Modified `CalculateDoorsAndRooms()` to create `TARGET_EXPANSION_DOOR` for destination items found in `m_ownNetItems`
2. **autoroute_engine.h/cpp**: Added `SetCurrentDestinations()` and `m_currentDestItems` to pass destinations to room completion. Updated `CompleteExpansionRoom()` to call `completion.SetDestinations()`
3. **maze_search.cpp**: Updated `SetDestinations()` to call `m_engine.SetCurrentDestinations()`. Added check for `TARGET_EXPANSION_DOOR` in `OccupyNextElement()` to detect reaching destination via target door

**FreeRouting equivalent**: `calculate_target_doors()` in `SortedRoomNeighbours.java` creates `TargetItemExpansionDoor` entries when same-net items that are destinations are encountered during room completion.

---

## References

- **FreeRouting Source**: `/Users/gmp/workspaces/zener-design/tools/freerouting/`
- Key files:
  - `src/main/java/app/freerouting/autoroute/AutorouteEngine.java`
  - `src/main/java/app/freerouting/autoroute/MazeSearchAlgo.java`
  - `src/main/java/app/freerouting/autoroute/SortedRoomNeighbours.java`
  - `src/main/java/app/freerouting/autoroute/DrillPage.java`
  - `src/main/java/app/freerouting/autoroute/DrillPageArray.java`
  - `src/main/java/app/freerouting/autoroute/ItemAutorouteInfo.java`
  - `src/main/java/app/freerouting/autoroute/TargetItemExpansionDoor.java`
  - `src/main/java/app/freerouting/board/ShapeSearchTree.java`
