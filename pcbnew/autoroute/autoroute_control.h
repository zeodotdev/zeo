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

#ifndef AUTOROUTE_CONTROL_H
#define AUTOROUTE_CONTROL_H

#include <vector>
#include <string>
#include <set>


/**
 * Configuration and control parameters for the autorouter.
 */
struct AUTOROUTE_CONTROL
{
    /// Maximum number of routing passes to attempt
    int max_passes = 100;

    /// Whether to allow vias for layer transitions
    bool vias_allowed = true;

    /// Preferred routing direction per layer (true = horizontal, false = vertical)
    /// Empty means no preference
    std::vector<bool> layer_direction;

    /// Default trace width in nanometers (per layer, or single value for all)
    std::vector<int> trace_width;

    /// Default via diameter in nanometers
    int via_diameter = 800000;  // 0.8mm

    /// Default via drill diameter in nanometers
    int via_drill = 400000;  // 0.4mm

    /// Clearance in nanometers
    int clearance = 200000;  // 0.2mm

    /// Cost multiplier for vias (higher = avoid vias)
    double via_cost = 50.0;

    /// Cost multiplier for traces (per nm)
    double trace_cost = 1.0;

    /// Cost multiplier for changing direction
    double direction_change_cost = 5.0;

    /// Specific nets to route (empty = all unrouted nets)
    std::set<std::string> nets_to_route;

    /// Maximum time in seconds before giving up (0 = no limit)
    double max_time_seconds = 0;

    /// Whether to ripup and retry blocked routes
    bool allow_ripup = false;

    /// Number of ripup attempts per net
    int ripup_passes = 3;

    /**
     * Get the trace width for a given layer.
     * Falls back to first width or default if layer not specified.
     */
    int GetTraceWidth( int aLayer ) const
    {
        if( trace_width.empty() )
            return 250000;  // 0.25mm default

        if( aLayer < static_cast<int>( trace_width.size() ) )
            return trace_width[aLayer];

        return trace_width[0];
    }

    /**
     * Calculate the cost for adding a via between two layers.
     */
    double GetViaCost( int aFromLayer, int aToLayer ) const
    {
        // Cost is proportional to number of layers traversed
        int layers = std::abs( aToLayer - aFromLayer );
        return via_cost * layers;
    }
};


/**
 * Results from an autoroute operation.
 */
struct AUTOROUTE_RESULT
{
    bool success = false;           ///< True if all requested nets were routed
    int  nets_routed = 0;           ///< Number of nets successfully routed
    int  nets_failed = 0;           ///< Number of nets that failed to route
    int  tracks_added = 0;          ///< Total track segments created
    int  vias_added = 0;            ///< Total vias created
    double time_seconds = 0;        ///< Time taken in seconds
    std::vector<std::string> failed_nets;  ///< Names of nets that failed
    std::string error_message;      ///< Error message if success is false
};


#endif // AUTOROUTE_CONTROL_H
