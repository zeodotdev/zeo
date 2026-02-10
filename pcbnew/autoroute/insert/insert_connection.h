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
#include <string>

// Forward declarations
class BOARD;


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
     * Set routing parameters.
     */
    void SetControl( const AUTOROUTE_CONTROL& aControl ) { m_control = aControl; }

    /**
     * Set the net name for the connection.
     */
    void SetNetName( const std::string& aNetName ) { m_netName = aNetName; }

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

    BOARD*            m_board = nullptr;
    AUTOROUTE_CONTROL m_control;
    std::string       m_netName;
};


#endif // INSERT_CONNECTION_H
