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
class PCB_TRACK;


/// Transmission-line model selected for a given track.
enum class IMPEDANCE_MODEL
{
    NONE,
    MICROSTRIP,         ///< single-ended, outer layer over a plane
    STRIPLINE,          ///< single-ended, inner layer between planes
    COPLANAR,           ///< coplanar waveguide (side grounds, no back plane)
    GROUNDED_COPLANAR,  ///< grounded coplanar waveguide (side grounds + back plane)
    COUPLED_MICROSTRIP, ///< differential pair, outer layer
    COUPLED_STRIPLINE,  ///< differential pair, inner layer
};


/// Outcome of a track impedance calculation: the model chosen plus the relevant impedance(s).
struct IMPEDANCE_RESULT
{
    int             singleEndedOhms  = 0;  ///< Z₀ (single line); odd-mode line Z for coupled models
    int             differentialOhms = 0;  ///< Z_diff; 0 unless a coupled (differential) model was used
    IMPEDANCE_MODEL model            = IMPEDANCE_MODEL::NONE;

    double conductorLossDbPerInch  = 0.0;  ///< conductor (skin + roughness) insertion loss
    double dielectricLossDbPerInch = 0.0;  ///< dielectric (tan δ) insertion loss

    /// Total insertion loss in dB per inch at the analysis frequency.
    double totalLossDbPerInch() const { return conductorLossDbPerInch + dielectricLossDbPerInch; }

    bool isDifferential() const
    {
        return model == IMPEDANCE_MODEL::COUPLED_MICROSTRIP
               || model == IMPEDANCE_MODEL::COUPLED_STRIPLINE;
    }

    bool valid() const { return model != IMPEDANCE_MODEL::NONE; }
};


/**
 * Signal-integrity analysis inputs that are NOT part of the physical stackup geometry.
 *
 * Defaults reproduce the legacy hardcoded behaviour (1 GHz, copper, smooth foil), so an
 * unconfigured board computes exactly as before.  Populated from BOARD_DESIGN_SETTINGS'
 * m_SI_* fields by the BOARD overload of ComputeOhms().
 */
struct IMPEDANCE_PARAMS
{
    double frequencyHz         = 1.0e9;    ///< analysis frequency (modal dispersion + skin depth)
    double dkMeasurementFreqHz = 1.0e9;    ///< frequency the stackup Dk/Df are specified at
    double conductorRho        = 1.72e-8;  ///< conductor resistivity, ohm-metre (copper @20°C)
    double roughnessM          = 0.0;      ///< conductor RMS surface roughness, metres
};


/**
 * Characteristic impedance calculation for tracks on a copper layer.
 *
 * Two entry points:
 *  - ComputeOhms(): single-ended Z₀ from (layer, width) alone.  Microstrip for outer
 *    copper (F.Cu / B.Cu), stripline for inner copper.  Cheap and cached; used where only
 *    the geometry is known.
 *  - ComputeForTrack(): board-and-track aware.  Detects whether the track is one half of a
 *    differential pair (→ coupled microstrip / stripline, reporting Z_diff) or is coplanar-
 *    referenced by adjacent ground copper (→ coplanar / grounded-coplanar), and otherwise
 *    falls back to the single-ended microstrip / stripline model.
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
     * tracks don't pay the per-call GetStackupOrDefault() cost.  Uses the SI params most
     * recently set via SetParams() (default: legacy 1 GHz / copper / smooth).
     */
    int ComputeOhms( const BOARD_STACKUP& aStackup, PCB_LAYER_ID aLayer, int aWidthIU );

    /**
     * Board-and-track aware impedance.  Reads the board's SI parameters, then:
     *  - if @p aTrack is half of a differential pair (its net has a coupled complement) and
     *    the coupled track is found nearby, models the pair (coupled microstrip / stripline)
     *    and reports both the per-line and differential impedance;
     *  - else if the track is flanked by adjacent ground copper on the same layer (coplanar
     *    geometry), models it as (grounded) coplanar waveguide;
     *  - else falls back to single-ended microstrip / stripline.
     *
     * Returns a NONE-model result if the stackup can't yield an impedance.
     */
    IMPEDANCE_RESULT ComputeForTrack( BOARD* aBoard, const PCB_TRACK* aTrack );

    /// Set the signal-integrity analysis parameters (frequency, conductor properties).
    /// Clears the cache, since cached results depend on these.
    void SetParams( const IMPEDANCE_PARAMS& aParams )
    {
        m_params = aParams;
        m_cache.clear();
    }

    const IMPEDANCE_PARAMS& GetParams() const { return m_params; }

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

    IMPEDANCE_PARAMS m_params;
};


#endif  // IMPEDANCE_CALCULATOR_H
