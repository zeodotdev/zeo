/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2026, Zeo <team@zeo.dev>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 */

#ifndef IMPEDANCE_CALCULATOR_H
#define IMPEDANCE_CALCULATOR_H

#include <layer_ids.h>
#include <unordered_map>

class BOARD;
class BOARD_STACKUP;


/**
 * Single-ended characteristic impedance Z₀ calculation for tracks on a copper layer.
 *
 * Microstrip is used for outer copper layers (F.Cu / B.Cu); stripline for inner copper
 * layers (dielectric on both sides).  Differential pair impedance is intentionally out
 * of scope here — that uses COUPLED_MICROSTRIP / COUPLED_STRIPLINE and is handled
 * separately.
 *
 * The calculator is intentionally stateless apart from a width/layer → Z₀ cache, so it
 * can be embedded in the DRC test provider, the track properties dialog, the PNS router
 * status overlay, and the Net Inspector — all of which need the same number.
 */
class IMPEDANCE_CALCULATOR
{
public:
    IMPEDANCE_CALCULATOR() = default;

    /**
     * Compute Z₀ in ohms for a given (layer, track-width-in-IU) pair using the board's
     * active stackup.  Returns 0 if the stackup is incomplete or the layer/width pair
     * cannot be analysed.
     *
     * Results are cached per-instance; call ClearCache() when the stackup may have
     * changed.
     */
    int ComputeOhms( BOARD* aBoard, PCB_LAYER_ID aLayer, int aWidthIU );

    /**
     * Same as ComputeOhms but accepts a pre-fetched stackup so callers iterating many
     * tracks don't pay the per-call GetStackupOrDefault() cost.
     */
    int ComputeOhms( const BOARD_STACKUP& aStackup, PCB_LAYER_ID aLayer, int aWidthIU );

    void ClearCache() { m_cache.clear(); }

private:
    struct CACHE_KEY
    {
        PCB_LAYER_ID layer;
        int          width;

        bool operator==( const CACHE_KEY& o ) const { return layer == o.layer && width == o.width; }
    };

    struct CACHE_KEY_HASH
    {
        std::size_t operator()( const CACHE_KEY& k ) const noexcept
        {
            return std::hash<int>{}( static_cast<int>( k.layer ) )
                   ^ ( std::hash<int>{}( k.width ) << 1 );
        }
    };

    std::unordered_map<CACHE_KEY, int, CACHE_KEY_HASH> m_cache;
};


#endif  // IMPEDANCE_CALCULATOR_H
