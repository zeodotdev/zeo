# FreeRouting vs Zener Autorouter Audit Report

**Date**: February 2026
**Status**: In Progress

This document provides a systematic comparison of FreeRouting's maze search implementation with Zener's current implementation, identifying gaps and integration issues.

---

## Executive Summary

After tracing through FreeRouting's complete maze search flow, we identified **several critical integration gaps** beyond Bug 7. While the architectural components exist in Zener, they are not fully wired together following FreeRouting's patterns.

### Issues Found

| # | Issue | Severity | Status |
|---|-------|----------|--------|
| 1 | Missing `target_doors` collection on `FREE_SPACE_ROOM` | Critical | TODO |
| 2 | Missing `expand_to_target_doors()` call in maze search | Critical | TODO |
| 3 | Init doesn't add target doors from start rooms to queue | Critical | TODO |
| 4 | No `is_start_info` tracking on items | Medium | Workaround via engine |
| 5 | TARGET_EXPANSION_DOOR creation in room completion | Fixed | ✅ Bug 7 |
| 6 | TARGET_EXPANSION_DOOR detection in occupy_next_element | Fixed | ✅ Bug 7 |
| 7 | `complete_neighbour_rooms()` not called | Medium | TODO |

---

## Detailed Flow Comparison

### 1. Initialization (`init()` vs `InitializeSearch()`)

#### FreeRouting (`MazeSearchAlgo.init()` lines 886-976)

```java
// 1. Process destination items
for (Item curr_item : p_destination_items) {
    ItemAutorouteInfo curr_info = curr_item.get_autoroute_info();
    curr_info.set_start_info(false);  // Mark as DESTINATION
    destination_distance.join(curr_tree_shape.bounding_box(), layer);
}

// 2. Process start items
for (Item curr_item : p_start_items) {
    ItemAutorouteInfo curr_info = curr_item.get_autoroute_info();
    curr_info.set_start_info(true);   // Mark as START
    // Create IncompleteFreeSpaceExpansionRoom for each start item shape
    IncompleteFreeSpaceExpansionRoom new_start_room =
        autoroute_engine.add_incomplete_expansion_room(null, layer, contained_shape);
}

// 3. Complete start rooms
for (IncompleteFreeSpaceExpansionRoom curr_room : start_rooms) {
    Collection<CompleteFreeSpaceExpansionRoom> curr_completed_rooms =
        autoroute_engine.complete_expansion_room(curr_room);
    completed_start_rooms.addAll(curr_completed_rooms);
}

// 4. Add TARGET DOORS (not regular doors) to queue
for (CompleteFreeSpaceExpansionRoom curr_room : completed_start_rooms) {
    for (TargetItemExpansionDoor curr_door : curr_room.get_target_doors()) {
        if (curr_door.is_destination_door())
            continue;  // Skip destination items
        // Add START item target doors to queue
        maze_expansion_list.add(new_element);
    }
}
```

#### Zener (`MAZE_SEARCH::InitializeSearch()`)

```cpp
// Creates pad rooms and adds regular doors to queue
for (EXPANSION_ROOM* sourceRoom : m_sourceRooms) {
    for (EXPANSION_DOOR* door : sourceRoom->GetDoors()) {  // Regular doors only!
        // ...
        AddToQueue(elem);
    }
}
```

**GAP**: Zener adds regular `EXPANSION_DOOR` from start rooms. FreeRouting adds `TargetItemExpansionDoor` for start items. This is fundamentally different!

---

### 2. Room Structure (`CompleteFreeSpaceExpansionRoom` vs `FREE_SPACE_ROOM`)

#### FreeRouting

```java
public class CompleteFreeSpaceExpansionRoom {
    private Collection<ExpansionDoor> doors;              // Regular doors
    private Collection<TargetItemExpansionDoor> target_doors;  // SEPARATE collection

    public void add_target_door(TargetItemExpansionDoor p_door) {
        this.target_doors.add(p_door);
    }

    public Collection<TargetItemExpansionDoor> get_target_doors() {
        return this.target_doors;
    }
}
```

#### Zener

```cpp
class FREE_SPACE_ROOM {
    std::vector<EXPANSION_DOOR*> m_doors;  // Only regular doors
    // NO target_doors collection!
};
```

**GAP**: Zener's `FREE_SPACE_ROOM` has no `target_doors` collection. Target doors would be mixed with regular doors, which breaks the FreeRouting pattern where target doors are handled separately.

---

### 3. Room Completion (`SortedRoomNeighbours.calculate()` vs `ROOM_COMPLETION::Complete()`)

#### FreeRouting

```java
public static CompleteExpansionRoom calculate(...) {
    // 1. Calculate neighbours
    SortedRoomNeighbours room_neighbours = calculate_neighbours(...);

    // 2. Try to remove edges (enlarge room)
    room_neighbours.try_remove_edge(...);

    // 3. Calculate new incomplete rooms between neighbours
    room_neighbours.calculate_new_incomplete_rooms(p_autoroute_engine);

    // 4. CRITICAL: Create target doors for own-net items
    if (result instanceof CompleteFreeSpaceExpansionRoom room) {
        calculate_target_doors(room, room_neighbours.own_net_objects, p_autoroute_engine);
    }
}

private static void calculate_target_doors(CompleteFreeSpaceExpansionRoom p_room,
                                           Collection<TreeEntry> p_own_net_objects, ...) {
    for (TreeEntry curr_entry : p_own_net_objects) {
        if (curr_entry.object instanceof Connectable curr_object) {
            if (curr_object.contains_net(net_no)) {
                TargetItemExpansionDoor new_target_door = new TargetItemExpansionDoor(...);
                p_room.add_target_door(new_target_door);  // Add to target_doors collection
            }
        }
    }
}
```

#### Zener (After Bug 7 Fix)

```cpp
void ROOM_COMPLETION::CalculateDoorsAndRooms(...) {
    // Create target doors for destination items in m_ownNetItems
    for (BOARD_ITEM* item : m_ownNetItems) {
        if (m_destItems.find(item) == m_destItems.end())
            continue;  // Only destinations
        // Create TARGET_EXPANSION_DOOR
        aResult.new_doors.push_back(std::move(targetDoor));  // Added to regular doors!
    }
}
```

**GAP**: Zener creates `TARGET_EXPANSION_DOOR` but adds it to regular `new_doors` collection, not a separate `target_doors` collection. Also, Zener only creates target doors for *destination* items, while FreeRouting creates them for ALL own-net items (start AND destination).

---

### 4. Maze Search Expansion (`expand_to_room_doors()` vs `ExpandToRoomDoors()`)

#### FreeRouting

```java
private boolean expand_to_room_doors(MazeListElement p_list_element) {
    // 1. Complete neighbour rooms
    this.autoroute_engine.complete_neighbour_rooms(p_list_element.next_room);

    // 2. CRITICAL: Expand to target doors FIRST
    boolean something_expanded = expand_to_target_doors(p_list_element, ...);

    // 3. Then expand to regular doors
    for (ExpansionDoor to_door : p_list_element.next_room.get_doors()) {
        expand_to_door(to_door, ...);
    }

    // 4. Expand to drill pages
    for (DrillPage to_drill_page : overlapping_drill_pages) {
        expand_to_drill_page(to_drill_page, ...);
    }
}

private boolean expand_to_target_doors(MazeListElement p_list_element, ...) {
    for (TargetItemExpansionDoor to_door : p_list_element.next_room.get_target_doors()) {
        // Calculate connection point to target item
        TileShape target_shape = ((Connectable) to_door.item)
            .get_trace_connection_shape(...);
        FloatPoint connection_point = target_shape.nearest_point_approx(shape_entry_middle);

        // Expand to target door
        expand_to_door_section(to_door, 0, new_shape_entry, ...);
    }
}
```

#### Zener

```cpp
void MAZE_SEARCH::ExpandToRoomDoors(const MAZE_LIST_ELEMENT& aElement) {
    // Complete incomplete rooms
    if (room->GetType() == ROOM_TYPE::FREE_SPACE_INCOMPLETE) {
        m_engine.CompleteExpansionRoom(incompleteRoom, m_netCode);
    }

    // Expand to regular doors
    for (EXPANSION_DOOR* door : room->GetDoors()) {
        ExpandToDoorSection(door, section, aElement);
    }

    // Expand to drill pages
    // ...

    // NO expand_to_target_doors() call!
}
```

**GAPS**:
1. No `complete_neighbour_rooms()` call - neighbors may not be ready
2. No `expand_to_target_doors()` call - target doors not explicitly expanded
3. Target doors mixed with regular doors (if they exist at all)

---

### 5. Destination Detection (`occupy_next_element()` vs `OccupyNextElement()`)

#### FreeRouting

```java
if (list_element.door instanceof TargetItemExpansionDoor curr_door) {
    if (curr_door.is_destination_door()) {  // Checks !is_start_info()
        this.destination_door = curr_door;
        return false;  // Found destination!
    }
}
```

#### Zener (After Bug 7 Fix)

```cpp
EXPANSION_DOOR* expDoor = dynamic_cast<EXPANSION_DOOR*>(element.door);
if (expDoor) {
    TARGET_EXPANSION_DOOR* targetDoor = dynamic_cast<TARGET_EXPANSION_DOOR*>(expDoor);
    if (targetDoor) {
        // Always treat as destination (no is_destination_door check)
        m_foundPath = true;
        return true;
    }
}
```

**GAP**: FreeRouting checks `is_destination_door()` which internally checks `!is_start_info()`. This distinguishes start items from destination items. Zener treats ALL target doors as destinations.

---

## Required Fixes

### Fix 1: Add `target_doors` Collection to `FREE_SPACE_ROOM`

```cpp
// expansion_room.h
class FREE_SPACE_ROOM : public EXPANSION_ROOM {
private:
    std::vector<TARGET_EXPANSION_DOOR*> m_targetDoors;

public:
    void AddTargetDoor(TARGET_EXPANSION_DOOR* aDoor) { m_targetDoors.push_back(aDoor); }
    const std::vector<TARGET_EXPANSION_DOOR*>& GetTargetDoors() const { return m_targetDoors; }
};
```

### Fix 2: Add `expand_to_target_doors()` to Maze Search

```cpp
// maze_search.cpp
void MAZE_SEARCH::ExpandToTargetDoors(const MAZE_LIST_ELEMENT& aElement) {
    FREE_SPACE_ROOM* room = dynamic_cast<FREE_SPACE_ROOM*>(aElement.next_room);
    if (!room) return;

    for (TARGET_EXPANSION_DOOR* targetDoor : room->GetTargetDoors()) {
        if (targetDoor == aElement.door) continue;
        ExpandToDoorSection(targetDoor, 0, aElement);
    }
}

void MAZE_SEARCH::ExpandToRoomDoors(const MAZE_LIST_ELEMENT& aElement) {
    // ... existing code ...

    // Add before regular door expansion:
    ExpandToTargetDoors(aElement);

    // ... rest of existing code ...
}
```

### Fix 3: Update Room Completion to Add to Target Doors Collection

```cpp
// room_completion.cpp
void ROOM_COMPLETION::CalculateDoorsAndRooms(...) {
    // Instead of:
    // aResult.new_doors.push_back(std::move(targetDoor));

    // Do this:
    FREE_SPACE_ROOM* completedRoom =
        dynamic_cast<FREE_SPACE_ROOM*>(aResult.completed_room.get());
    if (completedRoom) {
        completedRoom->AddTargetDoor(targetDoor.get());
    }
    aResult.new_target_doors.push_back(std::move(targetDoor));
}
```

### Fix 4: Add `is_start_info` Tracking to Items

```cpp
// item_autoroute_info.h
class ITEM_AUTOROUTE_INFO {
    bool m_isStartInfo = false;

public:
    void SetStartInfo(bool aIsStart) { m_isStartInfo = aIsStart; }
    bool IsStartInfo() const { return m_isStartInfo; }
};

// target_door.h
class TARGET_EXPANSION_DOOR {
    bool IsDestinationDoor() const {
        ITEM_AUTOROUTE_INFO* info = m_engine.GetItemAutorouteInfo(m_item);
        return info && !info->IsStartInfo();
    }
};
```

### Fix 5: Update Initialization to Add Target Doors from Start Rooms

```cpp
// maze_search.cpp
bool MAZE_SEARCH::InitializeSearch() {
    // Mark start items
    for (BOARD_ITEM* item : m_sourceItems) {
        ITEM_AUTOROUTE_INFO* info = m_engine.GetItemAutorouteInfo(item);
        if (info) info->SetStartInfo(true);
    }

    // After completing start rooms, add target doors to queue
    for (EXPANSION_ROOM* sourceRoom : m_sourceRooms) {
        FREE_SPACE_ROOM* freeRoom = dynamic_cast<FREE_SPACE_ROOM*>(sourceRoom);
        if (!freeRoom) continue;

        for (TARGET_EXPANSION_DOOR* targetDoor : freeRoom->GetTargetDoors()) {
            // Skip destination items - only add START item target doors
            if (targetDoor->IsDestinationDoor()) continue;

            MAZE_LIST_ELEMENT elem;
            elem.door = targetDoor;
            // ... fill in element ...
            AddToQueue(elem);
        }
    }
}
```

---

## Implementation Priority

1. **Fix 1**: Add `target_doors` to `FREE_SPACE_ROOM` (required for Fix 2, 3)
2. **Fix 3**: Update room completion to populate target doors
3. **Fix 2**: Add `expand_to_target_doors()` call
4. **Fix 4 & 5**: Add start/dest tracking and initialization changes

---

## Testing Strategy

1. **Unit Test**: Verify target doors are created during room completion
2. **Unit Test**: Verify `expand_to_target_doors()` is called and expands correctly
3. **Integration Test**: Route a simple 2-pad net, verify path found
4. **Integration Test**: Route the board that FreeRouting successfully routes

---

## References

- FreeRouting Source: `/Users/gmp/workspaces/zener-design/tools/freerouting/`
- Key files analyzed:
  - `MazeSearchAlgo.java` (lines 204-469, 886-976)
  - `SortedRoomNeighbours.java` (lines 52-123)
  - `CompleteFreeSpaceExpansionRoom.java` (lines 30, 108-117)
  - `TargetItemExpansionDoor.java` (lines 45-47)
