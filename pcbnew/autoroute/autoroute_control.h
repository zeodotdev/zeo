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
#include <utility>


/**
 * Via types supported by the autorouter.
 */
enum class AUTOROUTE_VIA_TYPE
{
    THROUGH,      ///< Through-hole via (all layers)
    BLIND_TOP,    ///< Blind via from top layer
    BLIND_BOTTOM, ///< Blind via from bottom layer
    BURIED,       ///< Buried via (internal layers only)
    MICROVIA      ///< Microvia (single layer transition)
};


/**
 * Configuration for a specific via type.
 */
struct VIA_TYPE_CONFIG
{
    AUTOROUTE_VIA_TYPE type = AUTOROUTE_VIA_TYPE::THROUGH;
    int diameter = 800000;       ///< Via pad diameter in nm
    int drill = 400000;          ///< Drill diameter in nm
    int first_layer = 0;         ///< First layer (0 = top)
    int last_layer = -1;         ///< Last layer (-1 = bottom)
    double cost_multiplier = 1.0; ///< Cost relative to through vias
    bool enabled = true;         ///< Whether this via type is allowed
};


/**
 * Configuration for a net class.
 * Net classes allow different routing parameters for different groups of nets.
 */
struct NET_CLASS_CONFIG
{
    std::string name;            ///< Net class name (e.g., "Power", "Signal", "Default")
    int trace_width = 250000;    ///< Trace width in nm (0.25mm default)
    int clearance = 200000;      ///< Clearance in nm (0.2mm default)
    int via_diameter = 800000;   ///< Via diameter in nm
    int via_drill = 400000;      ///< Via drill in nm
    double priority = 0.0;       ///< Routing priority (lower = route first)
    std::set<std::string> nets;  ///< Nets belonging to this class
};


/**
 * Clearance entry for net class pair.
 * Specifies clearance between two specific net classes.
 */
struct CLEARANCE_ENTRY
{
    std::string class1;          ///< First net class name
    std::string class2;          ///< Second net class name
    int clearance;               ///< Clearance between these classes in nm

    CLEARANCE_ENTRY() : clearance( 200000 ) {}
    CLEARANCE_ENTRY( const std::string& c1, const std::string& c2, int clr )
        : class1( c1 ), class2( c2 ), clearance( clr ) {}
};


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

    /// Via type configurations
    std::vector<VIA_TYPE_CONFIG> via_types;

    /// Whether blind vias are allowed
    bool blind_vias_allowed = false;

    /// Whether buried vias are allowed
    bool buried_vias_allowed = false;

    /// Whether microvias are allowed
    bool microvias_allowed = false;

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

    /// Multi-pass optimization settings
    bool multi_pass_enabled = true;   ///< Enable multi-pass optimization
    int  num_passes = 3;              ///< Number of optimization passes
    double pass_via_cost_multiplier = 0.8;  ///< Via cost reduction per pass (0.8 = 20% cheaper each pass)

    /// Net class configurations
    std::vector<NET_CLASS_CONFIG> net_classes;

    /// Clearance matrix: custom clearances between net class pairs
    std::vector<CLEARANCE_ENTRY> clearance_matrix;

    /**
     * Get the net class name for a net.
     * Returns "Default" if no class found.
     */
    std::string GetNetClassName( const std::string& aNetName ) const
    {
        const NET_CLASS_CONFIG* nc = GetNetClass( aNetName );
        return nc ? nc->name : "Default";
    }

    /**
     * Get the net class for a specific net.
     * Returns nullptr if no specific class is found (use defaults).
     */
    const NET_CLASS_CONFIG* GetNetClass( const std::string& aNetName ) const
    {
        for( const auto& nc : net_classes )
        {
            if( nc.nets.find( aNetName ) != nc.nets.end() )
                return &nc;
        }
        return nullptr;
    }

    /**
     * Get trace width for a specific net.
     * Falls back to default if no net class is found.
     */
    int GetNetTraceWidth( const std::string& aNetName, int aLayer ) const
    {
        const NET_CLASS_CONFIG* nc = GetNetClass( aNetName );
        if( nc )
            return nc->trace_width;
        return GetTraceWidth( aLayer );
    }

    /**
     * Get clearance for a specific net.
     * Falls back to default if no net class is found.
     */
    int GetNetClearance( const std::string& aNetName ) const
    {
        const NET_CLASS_CONFIG* nc = GetNetClass( aNetName );
        if( nc )
            return nc->clearance;
        return clearance;
    }

    /**
     * Get clearance between two nets using the clearance matrix.
     * Looks up custom clearance for the net class pair first,
     * then falls back to maximum of individual net clearances.
     */
    int GetClearanceBetweenNets( const std::string& aNet1, const std::string& aNet2 ) const
    {
        // Get net class names
        std::string class1 = GetNetClassName( aNet1 );
        std::string class2 = GetNetClassName( aNet2 );

        // Look up in clearance matrix (order-independent)
        for( const auto& entry : clearance_matrix )
        {
            if( ( entry.class1 == class1 && entry.class2 == class2 ) ||
                ( entry.class1 == class2 && entry.class2 == class1 ) )
            {
                return entry.clearance;
            }
        }

        // Fall back to maximum of individual net clearances
        int clr1 = GetNetClearance( aNet1 );
        int clr2 = GetNetClearance( aNet2 );
        return std::max( clr1, clr2 );
    }

    /**
     * Get via diameter for a specific net.
     */
    int GetNetViaDiameter( const std::string& aNetName ) const
    {
        const NET_CLASS_CONFIG* nc = GetNetClass( aNetName );
        if( nc )
            return nc->via_diameter;
        return via_diameter;
    }

    /**
     * Get via drill for a specific net.
     */
    int GetNetViaDrill( const std::string& aNetName ) const
    {
        const NET_CLASS_CONFIG* nc = GetNetClass( aNetName );
        if( nc )
            return nc->via_drill;
        return via_drill;
    }

    /**
     * Get via cost for a specific pass.
     * Later passes have lower via cost to encourage more solutions.
     */
    double GetPassViaCost( int aPass ) const
    {
        double multiplier = 1.0;
        for( int i = 0; i < aPass; ++i )
        {
            multiplier *= pass_via_cost_multiplier;
        }
        return via_cost * multiplier;
    }

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

    /**
     * Get the best via type for a layer transition.
     * Prefers shorter vias (blind/buried) over through vias when available.
     *
     * @param aFromLayer Starting layer
     * @param aToLayer Ending layer
     * @param aLayerCount Total number of copper layers
     * @return Best via type and layer pair, or nullopt if no via is possible
     */
    std::pair<AUTOROUTE_VIA_TYPE, std::pair<int, int>> GetBestViaType(
        int aFromLayer, int aToLayer, int aLayerCount ) const
    {
        int topLayer = 0;
        int bottomLayer = aLayerCount - 1;
        int minLayer = std::min( aFromLayer, aToLayer );
        int maxLayer = std::max( aFromLayer, aToLayer );

        // Check for microvia (single layer transition)
        if( microvias_allowed && std::abs( aFromLayer - aToLayer ) == 1 )
        {
            return { AUTOROUTE_VIA_TYPE::MICROVIA, { minLayer, maxLayer } };
        }

        // Check for blind via from top
        if( blind_vias_allowed && minLayer == topLayer && maxLayer < bottomLayer )
        {
            return { AUTOROUTE_VIA_TYPE::BLIND_TOP, { minLayer, maxLayer } };
        }

        // Check for blind via from bottom
        if( blind_vias_allowed && maxLayer == bottomLayer && minLayer > topLayer )
        {
            return { AUTOROUTE_VIA_TYPE::BLIND_BOTTOM, { minLayer, maxLayer } };
        }

        // Check for buried via
        if( buried_vias_allowed && minLayer > topLayer && maxLayer < bottomLayer )
        {
            return { AUTOROUTE_VIA_TYPE::BURIED, { minLayer, maxLayer } };
        }

        // Default to through via
        return { AUTOROUTE_VIA_TYPE::THROUGH, { topLayer, bottomLayer } };
    }

    /**
     * Get via diameter for a specific via type.
     */
    int GetViaDiameter( AUTOROUTE_VIA_TYPE aType ) const
    {
        for( const auto& config : via_types )
        {
            if( config.type == aType && config.enabled )
                return config.diameter;
        }
        return via_diameter;  // Default
    }

    /**
     * Get via drill for a specific via type.
     */
    int GetViaDrill( AUTOROUTE_VIA_TYPE aType ) const
    {
        for( const auto& config : via_types )
        {
            if( config.type == aType && config.enabled )
                return config.drill;
        }
        return via_drill;  // Default
    }
};


/**
 * Results from an autoroute operation.
 */
struct AUTOROUTE_RESULT
{
    bool success = false;           ///< True if all requested nets were routed
    bool cancelled = false;         ///< True if operation was cancelled
    int  nets_routed = 0;           ///< Number of nets successfully routed
    int  nets_failed = 0;           ///< Number of nets that failed to route
    int  tracks_added = 0;          ///< Total track segments created
    int  vias_added = 0;            ///< Total vias created
    int  passes_completed = 0;      ///< Number of optimization passes completed
    double time_seconds = 0;        ///< Time taken in seconds
    std::vector<std::string> failed_nets;  ///< Names of nets that failed
    std::string error_message;      ///< Error message if success is false
};


#endif // AUTOROUTE_CONTROL_H
