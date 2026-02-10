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

#ifndef LOCATE_CONNECTION_H
#define LOCATE_CONNECTION_H

#include <math/vector2d.h>
#include <vector>
#include <utility>

// Forward declarations
class EXPANDABLE_OBJECT;
class EXPANSION_DOOR;
class EXPANSION_DRILL;
class MAZE_SEARCH;


/**
 * Represents a point on a routing path.
 */
struct PATH_POINT
{
    VECTOR2I position;
    int      layer;
    bool     is_via;      ///< True if a via should be placed here
    bool     is_start;    ///< True if this is the path start
    bool     is_end;      ///< True if this is the path end

    PATH_POINT() : layer( 0 ), is_via( false ), is_start( false ), is_end( false ) {}
    PATH_POINT( const VECTOR2I& aPos, int aLayer, bool aVia = false )
        : position( aPos ), layer( aLayer ), is_via( aVia ), is_start( false ), is_end( false ) {}
};


/**
 * A segment of a routing path on a single layer.
 */
struct PATH_SEGMENT
{
    std::vector<VECTOR2I> points;  ///< Polyline points
    int                   layer;
    int                   width;   ///< Track width in nanometers

    PATH_SEGMENT() : layer( 0 ), width( 250000 ) {}
};


/**
 * Complete routing path from source to destination.
 */
struct ROUTING_PATH
{
    std::vector<PATH_SEGMENT> segments;
    std::vector<PATH_POINT>   via_locations;
    VECTOR2I                  start_point;
    VECTOR2I                  end_point;
    int                       start_layer;
    int                       end_layer;

    /**
     * Get total path length in nanometers.
     */
    int64_t GetTotalLength() const;

    /**
     * Get number of vias in this path.
     */
    int GetViaCount() const { return via_locations.size(); }

    /**
     * Check if path is valid (has at least start and end).
     */
    bool IsValid() const { return !segments.empty(); }
};


/**
 * Reconstructs a routing path from the maze search backtrack information.
 *
 * After MAZE_SEARCH finds a path, this class processes the backtrack
 * information to create actual routing geometry (polylines and vias).
 */
class LOCATE_CONNECTION
{
public:
    LOCATE_CONNECTION();

    /**
     * Set the maze search to extract path from.
     */
    void SetMazeSearch( const MAZE_SEARCH* aSearch ) { m_search = aSearch; }

    /**
     * Set the default track width.
     */
    void SetTrackWidth( int aWidth ) { m_trackWidth = aWidth; }

    /**
     * Set the clearance for path optimization.
     */
    void SetClearance( int aClearance ) { m_clearance = aClearance; }

    /**
     * Reconstruct the path from backtrack information.
     *
     * @return Routing path, or empty path if reconstruction failed
     */
    ROUTING_PATH LocatePath();

    /**
     * Simplify a path by removing unnecessary waypoints.
     * Keeps points where direction changes or layer changes.
     */
    static void SimplifyPath( ROUTING_PATH& aPath );

    /**
     * Optimize path corners for smoother routing.
     * (45-degree or curved segments)
     */
    static void OptimizePath( ROUTING_PATH& aPath, int aMinSegmentLength );

private:
    /**
     * Process a single backtrack element into path points.
     */
    void ProcessBacktrackElement( EXPANDABLE_OBJECT* aDoor, int aSection,
                                  std::vector<PATH_POINT>& aPoints );

    /**
     * Connect path points into segments.
     */
    void BuildSegments( const std::vector<PATH_POINT>& aPoints, ROUTING_PATH& aPath );

    /**
     * Extract via locations from path points.
     */
    void ExtractVias( const std::vector<PATH_POINT>& aPoints, ROUTING_PATH& aPath );

    const MAZE_SEARCH* m_search = nullptr;
    int                m_trackWidth = 250000;  // 0.25mm default
    int                m_clearance = 200000;   // 0.2mm default
};


#endif // LOCATE_CONNECTION_H
