/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef INSERT_CONNECTION_H
#define INSERT_CONNECTION_H

#include "../locate/locate_connection.h"
#include "../autoroute_control.h"
#include "../search/shape_search_tree.h"
#include <math/box2.h>
#include <string>

// Forward declarations
class BOARD;
class COMMIT;
class ZONE;
class CONGESTION_MAP;


/**
 * Result of inserting a connection.
 */
struct INSERT_RESULT
{
    bool success = false;
    int  tracks_added = 0;
    int  vias_added = 0;
    std::string error_message;
};


/**
 * Inserts routing paths into the PCB board.
 *
 * This class takes a ROUTING_PATH and creates actual PCB_TRACK and PCB_VIA
 * objects on the board. It uses the kipy IPC mechanism via generated Python code.
 */
class INSERT_CONNECTION
{
public:
    INSERT_CONNECTION();

    /**
     * Set the board to insert tracks/vias into.
     */
    void SetBoard( BOARD* aBoard ) { m_board = aBoard; }

    /**
     * Set the commit to use for adding items.
     * If not set, items will be added directly to the board (not recommended).
     */
    void SetCommit( COMMIT* aCommit ) { m_commit = aCommit; }

    /**
     * Set the search tree for collision detection.
     */
    void SetSearchTree( SHAPE_SEARCH_TREE* aSearchTree ) { m_searchTree = aSearchTree; }

    /**
     * Set the net code for collision filtering (same-net items don't block).
     */
    void SetNetCode( int aNetCode ) { m_netCode = aNetCode; }

    /**
     * Set routing parameters.
     */
    void SetControl( const AUTOROUTE_CONTROL& aControl ) { m_control = aControl; }

    /**
     * Set the net name for the connection.
     */
    void SetNetName( const std::string& aNetName ) { m_netName = aNetName; }

    /**
     * Set the congestion map for recording routed segments.
     */
    void SetCongestionMap( CONGESTION_MAP* aCongestionMap ) { m_congestionMap = aCongestionMap; }

    /**
     * Insert a routing path into the board.
     *
     * @param aPath The path to insert
     * @return Result of the insertion
     */
    INSERT_RESULT Insert( const ROUTING_PATH& aPath );

    /**
     * Generate Python/kipy code to insert the path.
     * Used for IPC-based insertion.
     *
     * @param aPath The path to insert
     * @return Python code string
     */
    std::string GenerateInsertCode( const ROUTING_PATH& aPath ) const;

private:
    /**
     * Insert a single track segment.
     */
    bool InsertTrackSegment( const PATH_SEGMENT& aSegment );

    /**
     * Insert a single via.
     */
    bool InsertVia( const PATH_POINT& aViaPoint );

    /**
     * Check if a track segment collides with obstacles.
     * @return true if segment is valid (no collision), false if blocked
     */
    bool ValidateSegment( const VECTOR2I& aStart, const VECTOR2I& aEnd, int aLayer );

    /**
     * Check if a point is inside any keepout zone.
     */
    bool IsInKeepoutZone( const VECTOR2I& aPoint, int aLayer );

    /**
     * Check if a segment crosses any keepout zone.
     */
    bool SegmentCrossesKeepout( const VECTOR2I& aStart, const VECTOR2I& aEnd, int aLayer );

    /**
     * Check if a segment crosses any pad from another net.
     */
    bool SegmentCrossesPad( const VECTOR2I& aStart, const VECTOR2I& aEnd, int aLayer );

    /**
     * Generate code for a track segment.
     */
    std::string GenerateTrackCode( const PATH_SEGMENT& aSegment ) const;

    /**
     * Generate code for a via.
     */
    std::string GenerateViaCode( const PATH_POINT& aViaPoint ) const;

    /**
     * Convert layer number to layer name.
     */
    std::string LayerToName( int aLayer ) const;

    /**
     * Calculate neckdown width for connection to a pad.
     * Returns the appropriate track width if neckdown is needed, or 0 if not.
     *
     * @param aPoint The point to check for pad proximity
     * @param aLayer The routing layer
     * @param aCurrentWidth Current track width
     * @return Neckdown width if needed, or aCurrentWidth if not
     */
    int CalculateNeckdownWidth( const VECTOR2I& aPoint, int aLayer, int aCurrentWidth );

    /**
     * Find a pad at the given position.
     */
    class PAD* FindPadAt( const VECTOR2I& aPoint, int aLayer );

    /**
     * Calculate the neckdown distance (how far from pad center to start neckdown).
     */
    int CalculateNeckdownDistance( class PAD* aPad, int aTrackWidth );

    BOARD*             m_board = nullptr;
    COMMIT*            m_commit = nullptr;
    SHAPE_SEARCH_TREE* m_searchTree = nullptr;
    CONGESTION_MAP*    m_congestionMap = nullptr;
    AUTOROUTE_CONTROL  m_control;
    std::string        m_netName;
    int                m_netCode = 0;
};


#endif // INSERT_CONNECTION_H
