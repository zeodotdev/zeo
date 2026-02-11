#
# CMakeLists.txt for pcbnew/autoroute (maze-based autorouter)
#
# This is a native C++ autorouter implementing an A* maze search algorithm
# based on FreeRouting's expansion room approach.
#

include_directories( BEFORE ${INC_BEFORE} )

include_directories(
    ./
    ../
    ../../include
    ../../pcbnew
    ../../common
    ../../libs/kimath/include
    ../../libs/core/include
    ../../thirdparty/dynamic_bitset
    ${INC_AFTER}
)

set( AUTOROUTE_SRCS
    # Main entry point and orchestration
    autoroute.cpp
    autoroute_engine.cpp

    # Geometry primitives
    geometry/tile_shape.cpp

    # Expansion room model
    expansion/expansion_room.cpp
    expansion/expansion_door.cpp
    expansion/expansion_drill.cpp

    # A* maze search algorithm
    search/maze_search.cpp
    search/destination_distance.cpp
    search/shape_search_tree.cpp
    search/room_completion.cpp
    search/ripup_checker.cpp
    search/congestion_map.cpp

    # Path reconstruction and trace creation
    locate/locate_connection.cpp
    insert/insert_connection.cpp

    # Path optimization
    optimize/pull_tight.cpp
    optimize/via_optimizer.cpp

    # Fanout routing for BGA/QFP
    fanout/fanout_router.cpp

    # Differential pair routing
    differential/differential_pair.cpp

    # Length matching
    length_match/length_matcher.cpp
)

add_library( autoroute STATIC ${AUTOROUTE_SRCS} )

target_link_libraries( autoroute PRIVATE
    common
)
