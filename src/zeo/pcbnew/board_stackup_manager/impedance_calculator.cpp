/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2026, Zeo <team@zeo.dev>
 * Zeo modifications licensed AGPL-3.0-or-later (see /LICENSE.README).
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

#include "impedance_calculator.h"

#include <advanced_config.h>
#include <board.h>
#include <board_design_settings.h>
#include <board_stackup_manager/board_stackup.h>
#include <netinfo.h>
#include <pcb_track.h>
#include <pad.h>
#include <pin_type.h>
#include <zone.h>
#include <transline_calculations/microstrip.h>
#include <transline_calculations/stripline.h>
#include <transline_calculations/coupled_microstrip.h>
#include <transline_calculations/coupled_stripline.h>
#include <transline_calculations/coplanar.h>
#include <transline_calculations/units_scales.h>
#include <geometry/seg.h>
#include <geometry/shape_poly_set.h>
#include <geometry/shape_line_chain.h>
#include <trace_helpers.h>
#include <wx/log.h>

#include <algorithm>
#include <cmath>
#include <memory>

static const wxChar IMPEDANCE_TRACE[] = wxT( "IMPEDANCE" );

namespace
{
    constexpr double COPPER_RHO = 1.72e-8;  // Ω·m at 20 °C — matches tuning profile dialog
    constexpr double M_PER_INCH = 0.0254;   // convert per-metre loss → per-inch (dB/m × this)


    int findStackupCopperIndex( const std::vector<BOARD_STACKUP_ITEM*>& aLayers, PCB_LAYER_ID aLayer )
    {
        for( size_t i = 0; i < aLayers.size(); ++i )
        {
            BOARD_STACKUP_ITEM* item = aLayers[i];

            if( item && item->IsEnabled() && item->GetType() == BS_ITEM_TYPE_COPPER
                && item->GetBrdLayerId() == aLayer )
            {
                return static_cast<int>( i );
            }
        }

        return -1;
    }


    /// Result of walking the dielectric stack from a signal layer to its reference plane.
    struct DIELECTRIC_SPAN
    {
        double heightM = 0.0;   ///< total dielectric height, metres
        double dk      = 0.0;   ///< thickness-weighted average dielectric constant (εr)
        double df      = 0.0;   ///< thickness-weighted average loss tangent (tan δ)

        bool valid() const { return heightM > 0.0 && dk > 0.0; }
    };


    // Walk towards a side and accumulate dielectric height + thickness-weighted Dk/Df until
    // the next copper layer.  Iterates every dielectric SUBLAYER (a single dielectric region
    // may hold multiple sublayers with distinct Dk/Df in microwave stackups), so the average
    // reflects the true composite rather than only sublayer 0.
    DIELECTRIC_SPAN collectDielectricToReference(
            const std::vector<BOARD_STACKUP_ITEM*>& aLayers, int aSignalIdx, int aDirection )
    {
        double totalThicknessIU = 0.0;
        double weightedDk       = 0.0;
        double weightedDf       = 0.0;

        for( int i = aSignalIdx + aDirection; i >= 0 && i < (int) aLayers.size(); i += aDirection )
        {
            BOARD_STACKUP_ITEM* item = aLayers[i];

            if( !item || !item->IsEnabled() )
                continue;

            if( item->GetType() == BS_ITEM_TYPE_COPPER )
                break;

            if( item->GetType() != BS_ITEM_TYPE_DIELECTRIC )
                continue;

            for( int sub = 0; sub < item->GetSublayersCount(); ++sub )
            {
                const int t = item->GetThickness( sub );

                if( t <= 0 )
                    continue;

                totalThicknessIU += t;
                weightedDk += t * item->GetEpsilonR( sub );
                weightedDf += t * item->GetLossTangent( sub );
            }
        }

        if( totalThicknessIU <= 0 )
            return {};

        return { totalThicknessIU / 1e9, weightedDk / totalThicknessIU,
                 weightedDf / totalThicknessIU };
    }


    using TRANSLINE_RESULTS =
            std::unordered_map<TRANSLINE_PARAMETERS, std::pair<double, TRANSLINE_STATUS>>;


    /// Single transmission line outcome: Z₀ plus per-metre conductor / dielectric loss (dB/m).
    struct LINE_RESULT
    {
        int    z0Ohms        = 0;
        double condLossDbPerM = 0.0;
        double dielLossDbPerM = 0.0;
    };


    /// Read a finite, non-error result value (or 0.0 if missing / errored / NaN).
    double resultValue( const TRANSLINE_RESULTS& aResults, TRANSLINE_PARAMETERS aKey )
    {
        auto it = aResults.find( aKey );

        if( it != aResults.end() && it->second.second != TRANSLINE_STATUS::TS_ERROR
            && std::isfinite( it->second.first ) )
        {
            return it->second.first;
        }

        return 0.0;
    }


    /// Hammerstad-Jensen conductor-loss multiplier for surface roughness.  Δ = RMS roughness,
    /// δ = skin depth.  Returns 1.0 (no effect) for smooth foil or unknown skin depth.
    double roughnessLossFactor( double aRoughnessM, double aSkinDepthM )
    {
        if( aRoughnessM <= 0.0 || aSkinDepthM <= 0.0 )
            return 1.0;

        const double r = aRoughnessM / aSkinDepthM;
        return 1.0 + ( 2.0 / M_PI ) * std::atan( 1.4 * r * r );
    }


    LINE_RESULT analyseMicrostrip( double aWidthM, double aSignalThicknessM, double aHeightM,
                                   double aDk, double aDf, const IMPEDANCE_PARAMS& aParams )
    {
        const double rho = aParams.conductorRho > 0.0 ? aParams.conductorRho : 1.72e-8;

        MICROSTRIP ms;
        ms.SetParameter( TRANSLINE_PARAMETERS::PHYS_WIDTH, aWidthM );
        ms.SetParameter( TRANSLINE_PARAMETERS::T, aSignalThicknessM );
        ms.SetParameter( TRANSLINE_PARAMETERS::H, aHeightM );
        ms.SetParameter( TRANSLINE_PARAMETERS::H_T, 1e6 );  // free air above; effectively infinite
        ms.SetParameter( TRANSLINE_PARAMETERS::EPSILONR, aDk );
        ms.SetParameter( TRANSLINE_PARAMETERS::MUR, 1.0 );
        ms.SetParameter( TRANSLINE_PARAMETERS::MURC, 1.0 );
        ms.SetParameter( TRANSLINE_PARAMETERS::FREQUENCY, aParams.frequencyHz );
        ms.SetParameter( TRANSLINE_PARAMETERS::TAND, aDf );
        ms.SetParameter( TRANSLINE_PARAMETERS::ROUGH, aParams.roughnessM );  // roughness internal
        ms.SetParameter( TRANSLINE_PARAMETERS::SIGMA, 1.0 / rho );
        ms.SetParameter( TRANSLINE_PARAMETERS::PHYS_LEN, 1.0 );  // → loss results are dB per metre
        ms.SetParameter( TRANSLINE_PARAMETERS::ANG_L, 1.0 );

        ms.Analyse();

        const TRANSLINE_RESULTS& results = ms.GetAnalysisResults();
        const double             z0      = resultValue( results, TRANSLINE_PARAMETERS::Z0 );

        LINE_RESULT out;

        if( z0 <= 0.0 )
            return out;

        out.z0Ohms = static_cast<int>( z0 + 0.5 );
        // The microstrip model reports conductor / dielectric loss under ATTEN_COND /
        // ATTEN_DILECTRIC (it does NOT populate LOSS_CONDUCTOR / LOSS_DIELECTRIC), with the
        // surface-roughness correction already applied to R_s internally — so read those keys.
        out.condLossDbPerM = resultValue( results, TRANSLINE_PARAMETERS::ATTEN_COND );
        out.dielLossDbPerM = resultValue( results, TRANSLINE_PARAMETERS::ATTEN_DILECTRIC );
        return out;
    }


    LINE_RESULT analyseStripline( double aWidthM, double aSignalThicknessM, double aTopH,
                                  double aBotH, double aDk, double aDf,
                                  const IMPEDANCE_PARAMS& aParams )
    {
        const double rho = aParams.conductorRho > 0.0 ? aParams.conductorRho : 1.72e-8;

        // STRIPLINE's m_parameters map registers neither MUR nor ROUGH — SetParameter uses
        // unordered_map::at() and throws on unsupported keys.  Z₀ is essentially unaffected by
        // roughness for striplines; the roughness correction is applied to the conductor loss
        // below (Hammerstad-Jensen), so loss is modelled consistently with microstrip.
        STRIPLINE sl;
        sl.SetParameter( TRANSLINE_PARAMETERS::PHYS_WIDTH, aWidthM );
        sl.SetParameter( TRANSLINE_PARAMETERS::T, aSignalThicknessM );
        sl.SetParameter( TRANSLINE_PARAMETERS::H, aTopH + aBotH + aSignalThicknessM );
        sl.SetParameter( TRANSLINE_PARAMETERS::STRIPLINE_A, aTopH );
        sl.SetParameter( TRANSLINE_PARAMETERS::EPSILONR, aDk );
        sl.SetParameter( TRANSLINE_PARAMETERS::MURC, 1.0 );
        sl.SetParameter( TRANSLINE_PARAMETERS::FREQUENCY, aParams.frequencyHz );
        sl.SetParameter( TRANSLINE_PARAMETERS::TAND, aDf );
        sl.SetParameter( TRANSLINE_PARAMETERS::SIGMA, 1.0 / rho );
        sl.SetParameter( TRANSLINE_PARAMETERS::PHYS_LEN, 1.0 );  // → loss results are dB per metre
        sl.SetParameter( TRANSLINE_PARAMETERS::ANG_L, 1.0 );

        sl.Analyse();

        const TRANSLINE_RESULTS& results = sl.GetAnalysisResults();
        const double             z0      = resultValue( results, TRANSLINE_PARAMETERS::Z0 );

        LINE_RESULT out;

        if( z0 <= 0.0 )
            return out;

        out.z0Ohms = static_cast<int>( z0 + 0.5 );

        const double skin = resultValue( results, TRANSLINE_PARAMETERS::SKIN_DEPTH );
        out.condLossDbPerM = resultValue( results, TRANSLINE_PARAMETERS::LOSS_CONDUCTOR )
                             * roughnessLossFactor( aParams.roughnessM, skin );
        out.dielLossDbPerM = resultValue( results, TRANSLINE_PARAMETERS::LOSS_DIELECTRIC );
        return out;
    }


    // Djordjevic-Sarkar (wideband Debye) causal dielectric dispersion.  Given the datasheet
    // (dk0, df0) specified at fMeasHz, returns the causal (Dk, Df) at fTargetHz.  Returns the
    // inputs unchanged when target == measurement frequency or inputs are degenerate, so the
    // default 1 GHz/1 GHz configuration reproduces the non-dispersive result exactly.
    //
    // ε(ω) = ε∞ + (Δε / ln(ω2/ω1)) · ln[(ω2 + jω)/(ω1 + jω)]
    //   real:  ε'(ω)  = ε∞ + (Δε / 2ln(ω2/ω1)) · ln[(ω2²+ω²)/(ω1²+ω²)]
    //   imag:  ε''(ω) = (Δε / ln(ω2/ω1)) · [atan(ω/ω1) − atan(ω/ω2)]
    // ε∞ and Δε are solved so the model reproduces (dk0, df0) at ω0.
    // Ref: Djordjevic et al., "Wideband Frequency-Domain Characterization of FR-4 and
    // Time-Domain Causality", IEEE Trans. EMC 43(4), 2001. Transition band 1 kHz – 1 THz
    // (matches HFSS / Altium wideband-Debye defaults).
    std::pair<double, double> djordjevicSarkar( double dk0, double df0, double fMeasHz,
                                                double fTargetHz )
    {
        constexpr double F_LOW  = 1.0e3;     // lower transition frequency, Hz
        constexpr double F_HIGH = 1.0e12;    // upper transition frequency, Hz

        if( dk0 <= 0.0 || df0 <= 0.0 || fMeasHz <= 0.0 || fTargetHz <= 0.0
            || fTargetHz == fMeasHz )
        {
            return { dk0, df0 };
        }

        const double w1 = 2.0 * M_PI * F_LOW;
        const double w2 = 2.0 * M_PI * F_HIGH;
        const double w0 = 2.0 * M_PI * fMeasHz;
        const double w  = 2.0 * M_PI * fTargetHz;

        const double L           = std::log( w2 / w1 );
        const double atanSpread0 = std::atan( w0 / w1 ) - std::atan( w0 / w2 );

        if( L <= 0.0 || atanSpread0 <= 0.0 )
            return { dk0, df0 };

        // Solve Δε and ε∞ from the single (dk0, df0) measurement.
        const double epsImag0 = dk0 * df0;                    // ε''(ω0)
        const double deltaEps = epsImag0 * L / atanSpread0;
        const double epsInf   = dk0 - ( deltaEps / ( 2.0 * L ) )
                                          * std::log( ( w2 * w2 + w0 * w0 ) / ( w1 * w1 + w0 * w0 ) );

        // Evaluate at the target frequency.
        const double dk = epsInf + ( deltaEps / ( 2.0 * L ) )
                                       * std::log( ( w2 * w2 + w * w ) / ( w1 * w1 + w * w ) );
        const double epsImag = ( deltaEps / L ) * ( std::atan( w / w1 ) - std::atan( w / w2 ) );

        if( dk <= 0.0 )
            return { dk0, df0 };

        const double df = epsImag / dk;

        return { dk, df > 0.0 ? df : df0 };
    }


    /// Even/odd-mode result of a coupled (differential pair) analysis.
    struct COUPLED_RESULT
    {
        int    lineOhms       = 0;    ///< odd-mode impedance of one line
        int    diffOhms       = 0;    ///< differential impedance Z_diff
        double condLossDbPerM = 0.0;  ///< odd-mode conductor loss, dB/m
        double dielLossDbPerM = 0.0;  ///< odd-mode dielectric loss, dB/m
    };


    COUPLED_RESULT analyseCoupledMicrostrip( double aWidthM, double aSignalThicknessM,
                                             double aHeightM, double aDk, double aDf, double aGapM,
                                             const IMPEDANCE_PARAMS& aParams )
    {
        const double rho = aParams.conductorRho > 0.0 ? aParams.conductorRho : 1.72e-8;

        COUPLED_MICROSTRIP cms;
        cms.SetParameter( TRANSLINE_PARAMETERS::PHYS_WIDTH, aWidthM );
        cms.SetParameter( TRANSLINE_PARAMETERS::PHYS_S, aGapM );
        cms.SetParameter( TRANSLINE_PARAMETERS::T, aSignalThicknessM );
        cms.SetParameter( TRANSLINE_PARAMETERS::H, aHeightM );
        cms.SetParameter( TRANSLINE_PARAMETERS::H_T, 1e6 );  // free air above
        cms.SetParameter( TRANSLINE_PARAMETERS::EPSILONR, aDk );
        cms.SetParameter( TRANSLINE_PARAMETERS::MURC, 1.0 );
        cms.SetParameter( TRANSLINE_PARAMETERS::FREQUENCY, aParams.frequencyHz );
        cms.SetParameter( TRANSLINE_PARAMETERS::TAND, aDf );
        cms.SetParameter( TRANSLINE_PARAMETERS::ROUGH, aParams.roughnessM );
        cms.SetParameter( TRANSLINE_PARAMETERS::SIGMA, 1.0 / rho );
        cms.SetParameter( TRANSLINE_PARAMETERS::PHYS_LEN, 1.0 );
        cms.SetParameter( TRANSLINE_PARAMETERS::ANG_L, 1.0 );

        cms.Analyse();

        const TRANSLINE_RESULTS& results = cms.GetAnalysisResults();
        COUPLED_RESULT           out;

        if( auto it = results.find( TRANSLINE_PARAMETERS::Z_DIFF );
            it != results.end() && it->second.second == TRANSLINE_STATUS::OK )
        {
            out.diffOhms = static_cast<int>( it->second.first + 0.5 );
        }

        if( auto it = results.find( TRANSLINE_PARAMETERS::Z0_O );
            it != results.end() && it->second.second == TRANSLINE_STATUS::OK )
        {
            out.lineOhms = static_cast<int>( it->second.first + 0.5 );
        }

        // Odd-mode loss is the relevant one for differential signalling.  Roughness is applied
        // internally (the coupled model registers ROUGH).
        out.condLossDbPerM = resultValue( results, TRANSLINE_PARAMETERS::ATTEN_COND_ODD );
        out.dielLossDbPerM = resultValue( results, TRANSLINE_PARAMETERS::ATTEN_DILECTRIC_ODD );
        return out;
    }


    COUPLED_RESULT analyseCoupledStripline( double aWidthM, double aSignalThicknessM, double aTopH,
                                            double aBotH, double aDk, double aDf, double aGapM,
                                            const IMPEDANCE_PARAMS& aParams )
    {
        const double rho = aParams.conductorRho > 0.0 ? aParams.conductorRho : 1.72e-8;

        // Full ground-plane separation and the strip's offset from the top plane.  The model
        // handles the offset (image / partial-capacitance method) and now emits native odd-mode
        // conductor + dielectric loss; STRIPLINE_A = top gap (== centred when top == bottom).
        const double totalH = aTopH + aBotH + aSignalThicknessM;

        COUPLED_STRIPLINE sl;
        sl.SetParameter( TRANSLINE_PARAMETERS::PHYS_WIDTH, aWidthM );
        sl.SetParameter( TRANSLINE_PARAMETERS::PHYS_S, aGapM );
        sl.SetParameter( TRANSLINE_PARAMETERS::T, aSignalThicknessM );
        sl.SetParameter( TRANSLINE_PARAMETERS::H, totalH );
        sl.SetParameter( TRANSLINE_PARAMETERS::STRIPLINE_A, aTopH );
        sl.SetParameter( TRANSLINE_PARAMETERS::EPSILONR, aDk );
        sl.SetParameter( TRANSLINE_PARAMETERS::TAND, aDf );
        sl.SetParameter( TRANSLINE_PARAMETERS::ROUGH, aParams.roughnessM );
        sl.SetParameter( TRANSLINE_PARAMETERS::MURC, 1.0 );
        sl.SetParameter( TRANSLINE_PARAMETERS::FREQUENCY, aParams.frequencyHz );
        sl.SetParameter( TRANSLINE_PARAMETERS::SIGMA, 1.0 / rho );
        sl.SetParameter( TRANSLINE_PARAMETERS::PHYS_LEN, 1.0 );
        sl.SetParameter( TRANSLINE_PARAMETERS::ANG_L, 1.0 );

        sl.Analyse();

        const TRANSLINE_RESULTS& results = sl.GetAnalysisResults();
        COUPLED_RESULT           out;

        if( auto it = results.find( TRANSLINE_PARAMETERS::Z_DIFF );
            it != results.end() && it->second.second == TRANSLINE_STATUS::OK )
        {
            out.diffOhms = static_cast<int>( it->second.first + 0.5 );
        }

        if( auto it = results.find( TRANSLINE_PARAMETERS::Z0_O );
            it != results.end() && it->second.second == TRANSLINE_STATUS::OK )
        {
            out.lineOhms = static_cast<int>( it->second.first + 0.5 );
        }

        out.condLossDbPerM = resultValue( results, TRANSLINE_PARAMETERS::ATTEN_COND_ODD );
        out.dielLossDbPerM = resultValue( results, TRANSLINE_PARAMETERS::ATTEN_DILECTRIC_ODD );
        return out;
    }


    LINE_RESULT analyseCoplanar( double aWidthM, double aSignalThicknessM, double aHeightM,
                                 double aDk, double aDf, double aGapM, bool aGrounded,
                                 const IMPEDANCE_PARAMS& aParams )
    {
        const double rho = aParams.conductorRho > 0.0 ? aParams.conductorRho : 1.72e-8;

        COPLANAR cp( aGrounded );
        cp.SetParameter( TRANSLINE_PARAMETERS::PHYS_WIDTH, aWidthM );
        cp.SetParameter( TRANSLINE_PARAMETERS::PHYS_S, aGapM );
        cp.SetParameter( TRANSLINE_PARAMETERS::T, aSignalThicknessM );
        cp.SetParameter( TRANSLINE_PARAMETERS::H, aHeightM );
        cp.SetParameter( TRANSLINE_PARAMETERS::EPSILONR, aDk );
        cp.SetParameter( TRANSLINE_PARAMETERS::TAND, aDf );
        cp.SetParameter( TRANSLINE_PARAMETERS::MURC, 1.0 );
        cp.SetParameter( TRANSLINE_PARAMETERS::ROUGH, aParams.roughnessM );
        cp.SetParameter( TRANSLINE_PARAMETERS::FREQUENCY, aParams.frequencyHz );
        cp.SetParameter( TRANSLINE_PARAMETERS::SIGMA, 1.0 / rho );
        cp.SetParameter( TRANSLINE_PARAMETERS::PHYS_LEN, 1.0 );
        cp.SetParameter( TRANSLINE_PARAMETERS::ANG_L, 1.0 );

        cp.Analyse();

        const TRANSLINE_RESULTS& results = cp.GetAnalysisResults();
        const double             z0      = resultValue( results, TRANSLINE_PARAMETERS::Z0 );

        LINE_RESULT out;

        if( z0 <= 0.0 )
            return out;

        out.z0Ohms = static_cast<int>( z0 + 0.5 );

        // The coplanar model now applies the Hammerstad-Jensen roughness correction to the
        // conductor loss internally, so read it directly.
        out.condLossDbPerM = resultValue( results, TRANSLINE_PARAMETERS::LOSS_CONDUCTOR );
        out.dielLossDbPerM = resultValue( results, TRANSLINE_PARAMETERS::LOSS_DIELECTRIC );
        return out;
    }


    // Edge-to-edge gap (metres) to the nearest same-layer segment of the coupled net, or 0
    // if no coupled track exists on this layer (e.g. the partner isn't routed yet).
    double findDiffPairGapM( BOARD* aBoard, const PCB_TRACK* aTrack, int aCoupledNetCode )
    {
        const SEG          segA( aTrack->GetStart(), aTrack->GetEnd() );
        const PCB_LAYER_ID layer = aTrack->GetLayer();

        long long bestDist     = -1;
        int       partnerWidth = 0;

        for( PCB_TRACK* t : aBoard->Tracks() )
        {
            if( t->Type() != PCB_TRACE_T && t->Type() != PCB_ARC_T )
                continue;

            if( t->GetNetCode() != aCoupledNetCode || t->GetLayer() != layer )
                continue;

            const SEG       segB( t->GetStart(), t->GetEnd() );
            const long long d = segA.Distance( segB );

            if( bestDist < 0 || d < bestDist )
            {
                bestDist     = d;
                partnerWidth = t->GetWidth();
            }
        }

        if( bestDist < 0 )
            return 0.0;

        const double gapIU = (double) bestDist - ( aTrack->GetWidth() + partnerWidth ) / 2.0;
        return gapIU > 0.0 ? gapIU / 1e9 : 0.0;
    }


    // Build the set of net codes that act as a return / reference conductor.  A net qualifies
    // *structurally* — by what it physically is, never by net-name matching:
    //   * it connects to one or more power pins (ELECTRICAL_PINTYPE power_in / power_out) — this
    //     is how the schematic marks ground and power nets, so GND, VCC, +5V, +3V3 and arbitrary
    //     named rails all resolve uniformly with no hardcoded name table; or
    //   * it is poured as a copper zone (a plane is a reference conductor by construction).
    // Bare copper with no net is treated as a reference by the caller (aNetCode <= 0).
    std::unordered_set<int> collectReferenceNets( BOARD* aBoard )
    {
        std::unordered_set<int> refNets;

        for( PAD* pad : aBoard->GetPads() )
        {
            if( !pad || pad->GetNetCode() <= 0 )
                continue;

            const ELECTRICAL_PINTYPE pt = ElectricalPinTypeFromString( pad->GetPinType() );

            if( pt == ELECTRICAL_PINTYPE::PT_POWER_IN || pt == ELECTRICAL_PINTYPE::PT_POWER_OUT )
                refNets.insert( pad->GetNetCode() );
        }

        for( ZONE* zone : aBoard->Zones() )
        {
            if( zone && zone->GetNetCode() > 0 )
                refNets.insert( zone->GetNetCode() );
        }

        return refNets;
    }


    struct COPLANAR_GEOMETRY
    {
        bool   found = false;
        double gapM  = 0.0;
    };


    // Geometric coplanar (CPW / CPWG) detection.  Recognised ground/reference copper — a zone
    // fill or a ground/power track on the same layer — must sit close to the trace on BOTH sides
    // with roughly symmetric gaps before the trace is treated as coplanar; otherwise the caller
    // falls back to microstrip / stripline, so ordinary plane-referenced traces are unaffected.
    //
    // Proximity window: min(width-ratio x width, height-ratio x substrate height).  The height
    // cap matters on thin dielectrics — the grounded-CPW conformal model only tracks the true
    // impedance while the side grounds are within a few substrate heights of the trace; beyond
    // that it diverges upward from the (more physical) microstrip result, so a tight window both
    // keeps the model honest and minimises the CPW<->microstrip classification discontinuity.
    // Window / symmetry / height ratios are ADVANCED_CFG knobs.  @p aRefHeightIU is the distance
    // to the nearer reference plane (0 disables the height cap).
    //
    // Reference zones are probed perpendicular to the trace on BOTH sides — a single pour that
    // floods around the trace would otherwise register only one side via SquaredDistanceToSeg.
    // Discrete ground tracks are handled separately below.  The coplanar model assumes equal
    // side gaps: for moderate asymmetry the mean gap is used, and strongly-asymmetric copper is
    // rejected (fall back).  Explicit per-net CPW selection will arrive with impedance profiles.
    COPLANAR_GEOMETRY findCoplanarGround( BOARD* aBoard, const PCB_TRACK* aTrack,
                                          long long aRefHeightIU,
                                          const std::unordered_set<int>& aReferenceNets )
    {
        const SEG seg( aTrack->GetStart(), aTrack->GetEnd() );

        if( seg.A == seg.B )
            return {};

        const PCB_LAYER_ID layer   = aTrack->GetLayer();
        const int          width   = aTrack->GetWidth();
        const int          netCode = aTrack->GetNetCode();

        // Bare copper (no net) or a structurally-identified reference net (power/ground/plane).
        auto isRef = [&]( int aNetCode )
        {
            return aNetCode <= 0 || aReferenceNets.count( aNetCode ) > 0;
        };

        const ADVANCED_CFG& cfg = ADVANCED_CFG::GetCfg();
        const double widthRatio = cfg.m_CoplanarGapWidthRatio > 0.0 ? cfg.m_CoplanarGapWidthRatio : 3.0;
        const double heightRatio = cfg.m_CoplanarGapHeightRatio > 0.0 ? cfg.m_CoplanarGapHeightRatio : 4.0;
        const double symTol = cfg.m_CoplanarGapSymmetryRatio >= 1.0 ? cfg.m_CoplanarGapSymmetryRatio : 2.5;

        long long maxGapIU = static_cast<long long>( (double) width * widthRatio );

        // Tighten the window on thin dielectrics (see comment above).
        if( aRefHeightIU > 0 )
            maxGapIU = std::min( maxGapIU,
                                 static_cast<long long>( (double) aRefHeightIU * heightRatio ) );

        if( maxGapIU <= 0 )
            return {};

        long long leftGap = -1, rightGap = -1;

        auto consider = [&]( long long aGap, const VECTOR2I& aNear )
        {
            if( aGap < 0 )
                aGap = 0;

            if( aGap > maxGapIU )
                return;

            const long long side = seg.LineDistance( aNear, true );

            if( side >= 0 )
            {
                if( rightGap < 0 || aGap < rightGap )
                    rightGap = aGap;
            }
            else
            {
                if( leftGap < 0 || aGap < leftGap )
                    leftGap = aGap;
            }
        };

        // Segment bounding box (expanded by the window) for cheap candidate rejection.
        const int ex     = (int) maxGapIU + width;
        const int bbMinX = std::min( seg.A.x, seg.B.x ) - ex;
        const int bbMaxX = std::max( seg.A.x, seg.B.x ) + ex;
        const int bbMinY = std::min( seg.A.y, seg.B.y ) - ex;
        const int bbMaxY = std::max( seg.A.y, seg.B.y ) + ex;

        auto bboxNear = [&]( const VECTOR2I& a, const VECTOR2I& b, int halfW )
        {
            return !( std::min( a.x, b.x ) - halfW > bbMaxX || std::max( a.x, b.x ) + halfW < bbMinX
                      || std::min( a.y, b.y ) - halfW > bbMaxY
                      || std::max( a.y, b.y ) + halfW < bbMinY );
        };

        // Zone fills are the usual coplanar reference.  Probe perpendicular to the trace on each
        // side and take the nearest crossing of the pour boundary, so a pour that wraps around
        // the trace contributes a gap on BOTH sides (sampled at a few points along the trace to
        // tolerate partial coverage).
        const VECTOR2D segDir( seg.B.x - seg.A.x, seg.B.y - seg.A.y );
        const double   segLen = segDir.EuclideanNorm();

        if( segLen > 0.0 )
        {
            const VECTOR2D nrm( -segDir.y / segLen, segDir.x / segLen );  // unit perpendicular
            const double   probeLen = (double) ( maxGapIU + width );

            std::vector<ZONE*> refZones;

            for( ZONE* zone : aBoard->Zones() )
            {
                if( !zone || !zone->IsOnLayer( layer ) || zone->GetNetCode() == netCode )
                    continue;

                if( isRef( zone->GetNetCode() ) )
                    refZones.push_back( zone );
            }

            for( double f : { 0.25, 0.5, 0.75 } )
            {
                const VECTOR2I base( seg.A.x + (int) std::lround( f * segDir.x ),
                                     seg.A.y + (int) std::lround( f * segDir.y ) );

                for( int sgn : { +1, -1 } )
                {
                    const VECTOR2I tip( base.x + (int) std::lround( sgn * nrm.x * probeLen ),
                                        base.y + (int) std::lround( sgn * nrm.y * probeLen ) );
                    const SEG probe( base, tip );

                    long long nearestGap = -1;

                    for( ZONE* zone : refZones )
                    {
                        std::shared_ptr<SHAPE_POLY_SET> polys = zone->GetFilledPolysList( layer );

                        if( !polys || polys->IsEmpty() )
                            continue;

                        SHAPE_LINE_CHAIN::INTERSECTIONS ips;

                        for( int o = 0; o < polys->OutlineCount(); ++o )
                        {
                            polys->COutline( o ).Intersect( probe, ips );

                            for( int h = 0; h < polys->HoleCount( o ); ++h )
                                polys->CHole( o, h ).Intersect( probe, ips );
                        }

                        for( const SHAPE_LINE_CHAIN::INTERSECTION& ip : ips )
                        {
                            const long long dist = (long long) std::sqrt(
                                    (double) ( ip.p - base ).SquaredEuclideanNorm() );
                            const long long gap = dist - width / 2;

                            if( gap >= 0 && ( nearestGap < 0 || gap < nearestGap ) )
                                nearestGap = gap;
                        }
                    }

                    if( nearestGap >= 0 )
                    {
                        const double   off = width / 2.0 + (double) nearestGap + 1.0;
                        const VECTOR2I sidePt( base.x + (int) std::lround( sgn * nrm.x * off ),
                                               base.y + (int) std::lround( sgn * nrm.y * off ) );
                        consider( nearestGap, sidePt );
                    }
                }
            }
        }

        // Ground / power tracks on the same layer (e.g. coplanar guard traces).
        for( PCB_TRACK* t : aBoard->Tracks() )
        {
            if( ( t->Type() != PCB_TRACE_T && t->Type() != PCB_ARC_T ) || t->GetLayer() != layer
                || t->GetNetCode() == netCode )
                continue;

            if( !bboxNear( t->GetStart(), t->GetEnd(), t->GetWidth() / 2 ) )
                continue;

            if( !isRef( t->GetNetCode() ) )
                continue;

            const SEG       segB( t->GetStart(), t->GetEnd() );
            const long long d = seg.Distance( segB );
            const VECTOR2I  mid( ( t->GetStartX() + t->GetEndX() ) / 2,
                                 ( t->GetStartY() + t->GetEndY() ) / 2 );
            consider( d - ( width + t->GetWidth() ) / 2, mid );
        }

        if( leftGap < 0 || rightGap < 0 )
            return {};  // need reference copper on both sides to be coplanar

        const long long lo = std::min( leftGap, rightGap );
        const long long hi = std::max( leftGap, rightGap );

        // Symmetry gate (configurable): reject strongly-asymmetric copper.
        if( hi > (long long) ( (double) lo * symTol ) + 50000 )
            return {};

        COPLANAR_GEOMETRY out;
        out.found = true;
        out.gapM  = ( ( leftGap + rightGap ) / 2.0 ) / 1e9;  // symmetric-model effective gap
        return out;
    }
}


int IMPEDANCE_CALCULATOR::ComputeOhms( BOARD* aBoard, PCB_LAYER_ID aLayer, int aWidthIU )
{
    if( !aBoard )
        return 0;

    // Pull the board's signal-integrity analysis parameters (frequency, conductor
    // properties) so the computation reflects the user's configured settings rather than
    // hardcoded defaults.
    const BOARD_DESIGN_SETTINGS& bds = aBoard->GetDesignSettings();

    IMPEDANCE_PARAMS params;
    params.frequencyHz         = bds.m_SI_ReferenceFrequency > 0.0 ? bds.m_SI_ReferenceFrequency : 1.0e9;
    params.dkMeasurementFreqHz = bds.m_SI_DkMeasurementFrequency > 0.0 ? bds.m_SI_DkMeasurementFrequency : 1.0e9;
    params.conductorRho        = bds.m_SI_ConductorResistivity > 0.0 ? bds.m_SI_ConductorResistivity : 1.72e-8;
    params.roughnessM          = bds.m_SI_ConductorRoughness >= 0.0 ? bds.m_SI_ConductorRoughness : 0.0;

    SetParams( params );

    const BOARD_STACKUP stackup = aBoard->GetStackupOrDefault();
    return ComputeOhms( stackup, aLayer, aWidthIU );
}


int IMPEDANCE_CALCULATOR::ComputeOhms( const BOARD_STACKUP& aStackup, PCB_LAYER_ID aLayer,
                                       int aWidthIU )
{
    CACHE_KEY key{ aLayer, aWidthIU };
    auto      it = m_cache.find( key );

    if( it != m_cache.end() )
        return it->second;

    const std::vector<BOARD_STACKUP_ITEM*>& layers = aStackup.GetList();
    const int signalIdx = findStackupCopperIndex( layers, aLayer );

    wxLogTrace( IMPEDANCE_TRACE, "ComputeOhms: layer=%d width=%d signalIdx=%d stackupSize=%zu",
                (int) aLayer, aWidthIU, signalIdx, layers.size() );

    if( signalIdx < 0 || aWidthIU <= 0 )
    {
        wxLogTrace( IMPEDANCE_TRACE,
                    "  → early-out (signalIdx<0 or width<=0).  Dumping stackup:" );

        for( size_t i = 0; i < layers.size(); ++i )
        {
            BOARD_STACKUP_ITEM* item = layers[i];
            if( !item )
            {
                wxLogTrace( IMPEDANCE_TRACE, "    [%zu] null", i );
                continue;
            }
            wxLogTrace( IMPEDANCE_TRACE,
                        "    [%zu] type=%d enabled=%d brdLayerId=%d thickness=%d dk=%.3f",
                        i, (int) item->GetType(), (int) item->IsEnabled(),
                        (int) item->GetBrdLayerId(), item->GetThickness(),
                        item->HasEpsilonRValue() ? item->GetEpsilonR() : -1.0 );
        }

        m_cache[key] = 0;
        return 0;
    }

    BOARD_STACKUP_ITEM* signal           = layers[signalIdx];
    const double        signalThicknessM = signal->GetThickness() / 1e9;
    const double        widthM           = aWidthIU / 1e9;

    int z0Ohms = 0;

    if( aLayer == F_Cu || aLayer == B_Cu )
    {
        const DIELECTRIC_SPAN span = collectDielectricToReference( layers, signalIdx,
                                                                   ( aLayer == F_Cu ) ? +1 : -1 );

        // Causal frequency dispersion: evaluate the bulk Dk/Df at the analysis frequency
        // from the datasheet values (specified at the Dk-measurement frequency).
        auto [dk, df] = djordjevicSarkar( span.dk, span.df, m_params.dkMeasurementFreqHz,
                                          m_params.frequencyHz );

        wxLogTrace( IMPEDANCE_TRACE,
                    "  microstrip: signalT=%.6f widthM=%.6f heightM=%.6f dk0=%.3f→dk=%.3f df=%.4f",
                    signalThicknessM, widthM, span.heightM, span.dk, dk, df );

        if( span.valid() )
            z0Ohms = analyseMicrostrip( widthM, signalThicknessM, span.heightM, dk, df,
                                        m_params ).z0Ohms;
    }
    else
    {
        const DIELECTRIC_SPAN top = collectDielectricToReference( layers, signalIdx, -1 );
        const DIELECTRIC_SPAN bot = collectDielectricToReference( layers, signalIdx, +1 );

        wxLogTrace( IMPEDANCE_TRACE,
                    "  stripline: signalT=%.6f widthM=%.6f topH=%.6f topDk=%.3f "
                    "botH=%.6f botDk=%.3f",
                    signalThicknessM, widthM, top.heightM, top.dk, bot.heightM, bot.dk );

        if( top.heightM > 0.0 && bot.heightM > 0.0 )
        {
            const double totalH = top.heightM + bot.heightM;
            const double dk0    = ( top.heightM * top.dk + bot.heightM * bot.dk ) / totalH;
            const double df0    = ( top.heightM * top.df + bot.heightM * bot.df ) / totalH;

            // For stripline (pure TEM, no geometric dispersion in the model) the material
            // Dk(f) is the only frequency dependence of Z₀, so this is what makes the
            // reference-frequency setting actually move the inner-layer impedance.
            auto [dk, df] = djordjevicSarkar( dk0, df0, m_params.dkMeasurementFreqHz,
                                              m_params.frequencyHz );

            z0Ohms = analyseStripline( widthM, signalThicknessM, top.heightM,
                                       bot.heightM, dk, df, m_params ).z0Ohms;
        }
    }

    wxLogTrace( IMPEDANCE_TRACE, "  → z0=%d Ω", z0Ohms );

    m_cache[key] = z0Ohms;
    return z0Ohms;
}


void IMPEDANCE_CALCULATOR::applyBoardParams( BOARD* aBoard )
{
    const BOARD_DESIGN_SETTINGS& bds = aBoard->GetDesignSettings();

    IMPEDANCE_PARAMS params;
    params.frequencyHz         = bds.m_SI_ReferenceFrequency > 0.0 ? bds.m_SI_ReferenceFrequency : 1.0e9;
    params.dkMeasurementFreqHz = bds.m_SI_DkMeasurementFrequency > 0.0 ? bds.m_SI_DkMeasurementFrequency : 1.0e9;
    params.conductorRho        = bds.m_SI_ConductorResistivity > 0.0 ? bds.m_SI_ConductorResistivity : 1.72e-8;
    params.roughnessM          = bds.m_SI_ConductorRoughness >= 0.0 ? bds.m_SI_ConductorRoughness : 0.0;

    SetParams( params );
}


IMPEDANCE_RESULT IMPEDANCE_CALCULATOR::computeGeometry( const BOARD_STACKUP& aStackup,
                                                        PCB_LAYER_ID aLayer, int aWidthIU,
                                                        int aGapIU )
{
    IMPEDANCE_RESULT result;

    const std::vector<BOARD_STACKUP_ITEM*>& layers = aStackup.GetList();
    const int signalIdx = findStackupCopperIndex( layers, aLayer );

    if( signalIdx < 0 || aWidthIU <= 0 )
        return result;

    BOARD_STACKUP_ITEM* signal           = layers[signalIdx];
    const double        signalThicknessM = signal->GetThickness() / 1e9;
    const double        widthM           = aWidthIU / 1e9;
    const bool          outer            = ( aLayer == F_Cu || aLayer == B_Cu );
    const int           dir              = ( aLayer == F_Cu ) ? +1 : -1;
    const double        gapM             = aGapIU > 0 ? aGapIU / 1e9 : 0.0;

    // Differential pair (coupled microstrip / stripline) when a gap is supplied.
    if( gapM > 0.0 )
    {
        if( outer )
        {
            const DIELECTRIC_SPAN span = collectDielectricToReference( layers, signalIdx, dir );

            if( span.valid() )
            {
                auto [dk, df] = djordjevicSarkar( span.dk, span.df, m_params.dkMeasurementFreqHz,
                                                  m_params.frequencyHz );
                COUPLED_RESULT cr = analyseCoupledMicrostrip( widthM, signalThicknessM,
                                                              span.heightM, dk, df, gapM, m_params );

                if( cr.diffOhms > 0 )
                {
                    result.model                   = IMPEDANCE_MODEL::COUPLED_MICROSTRIP;
                    result.differentialOhms        = cr.diffOhms;
                    result.singleEndedOhms         = cr.lineOhms;
                    result.conductorLossDbPerInch  = cr.condLossDbPerM * M_PER_INCH;
                    result.dielectricLossDbPerInch = cr.dielLossDbPerM * M_PER_INCH;
                }
            }
        }
        else
        {
            const DIELECTRIC_SPAN top = collectDielectricToReference( layers, signalIdx, -1 );
            const DIELECTRIC_SPAN bot = collectDielectricToReference( layers, signalIdx, +1 );

            if( top.heightM > 0.0 && bot.heightM > 0.0 )
            {
                const double sumH = top.heightM + bot.heightM;
                const double dk0  = ( top.heightM * top.dk + bot.heightM * bot.dk ) / sumH;
                const double df0  = ( top.heightM * top.df + bot.heightM * bot.df ) / sumH;

                auto [dk, df] = djordjevicSarkar( dk0, df0, m_params.dkMeasurementFreqHz,
                                                  m_params.frequencyHz );

                // Real top/bottom heights → the model captures the strip's offset.
                COUPLED_RESULT cr = analyseCoupledStripline( widthM, signalThicknessM, top.heightM,
                                                             bot.heightM, dk, df, gapM, m_params );

                if( cr.diffOhms > 0 )
                {
                    result.model                   = IMPEDANCE_MODEL::COUPLED_STRIPLINE;
                    result.differentialOhms        = cr.diffOhms;
                    result.singleEndedOhms         = cr.lineOhms;
                    result.conductorLossDbPerInch  = cr.condLossDbPerM * M_PER_INCH;
                    result.dielectricLossDbPerInch = cr.dielLossDbPerM * M_PER_INCH;
                }
            }
        }

        return result;
    }

    // Single-ended (microstrip / stripline).
    LINE_RESULT line;

    if( outer )
    {
        const DIELECTRIC_SPAN span = collectDielectricToReference( layers, signalIdx, dir );

        if( span.valid() )
        {
            auto [dk, df] = djordjevicSarkar( span.dk, span.df, m_params.dkMeasurementFreqHz,
                                              m_params.frequencyHz );
            line = analyseMicrostrip( widthM, signalThicknessM, span.heightM, dk, df, m_params );
        }
    }
    else
    {
        const DIELECTRIC_SPAN top = collectDielectricToReference( layers, signalIdx, -1 );
        const DIELECTRIC_SPAN bot = collectDielectricToReference( layers, signalIdx, +1 );

        if( top.heightM > 0.0 && bot.heightM > 0.0 )
        {
            const double sumH = top.heightM + bot.heightM;
            const double dk0  = ( top.heightM * top.dk + bot.heightM * bot.dk ) / sumH;
            const double df0  = ( top.heightM * top.df + bot.heightM * bot.df ) / sumH;

            auto [dk, df] = djordjevicSarkar( dk0, df0, m_params.dkMeasurementFreqHz,
                                              m_params.frequencyHz );
            line = analyseStripline( widthM, signalThicknessM, top.heightM, bot.heightM, dk, df,
                                     m_params );
        }
    }

    if( line.z0Ohms > 0 )
    {
        result.model                   = outer ? IMPEDANCE_MODEL::MICROSTRIP : IMPEDANCE_MODEL::STRIPLINE;
        result.singleEndedOhms         = line.z0Ohms;
        result.conductorLossDbPerInch  = line.condLossDbPerM * M_PER_INCH;
        result.dielectricLossDbPerInch = line.dielLossDbPerM * M_PER_INCH;
    }

    return result;
}


IMPEDANCE_RESULT IMPEDANCE_CALCULATOR::ComputeForTrack( BOARD* aBoard, const PCB_TRACK* aTrack )
{
    IMPEDANCE_RESULT result;

    if( !aBoard || !aTrack )
        return result;

    applyBoardParams( aBoard );

    // Reference-net classification (power / ground / plane) is board-wide; build it once and
    // reuse across the many per-track queries (DRC iterates every track on the board).
    if( aBoard != m_referenceNetsBoard )
    {
        m_referenceNets      = collectReferenceNets( aBoard );
        m_referenceNetsBoard = aBoard;
    }

    const PCB_LAYER_ID layer = aTrack->GetLayer();
    const int          width = aTrack->GetWidth();

    if( width <= 0 || !IsCopperLayer( layer ) )
        return result;

    const BOARD_STACKUP stackup = aBoard->GetStackupOrDefault();

    // 1) Differential pair — the track's net has a coupled complement and the partner track is
    //    routed on this layer so the gap can be measured.
    if( aTrack->GetNetCode() > 0 )
    {
        if( NETINFO_ITEM* coupled = aBoard->DpCoupledNet( aTrack->GetNet() ) )
        {
            const double gapM = findDiffPairGapM( aBoard, aTrack, coupled->GetNetCode() );

            if( gapM > 0.0 )
            {
                result = computeGeometry( stackup, layer, width,
                                          static_cast<int>( gapM * 1e9 + 0.5 ) );

                if( result.valid() )
                    return result;
            }
        }
    }

    // 2) Coplanar — adjacent ground copper on both sides (needs the placed track to detect).
    //    Resolve the substrate height first: the coplanar model needs it, and the detection
    //    window is height-aware (tighter on thin dielectrics).
    {
        const std::vector<BOARD_STACKUP_ITEM*>& layers = stackup.GetList();
        const int                               signalIdx = findStackupCopperIndex( layers, layer );

        if( signalIdx >= 0 )
        {
            const bool outer = ( layer == F_Cu || layer == B_Cu );
            const int  dir   = ( layer == F_Cu ) ? +1 : -1;

            // Nearer reference plane → coplanar substrate height; with a plane present this is
            // grounded-coplanar (CPWG), the common PCB case.
            DIELECTRIC_SPAN refSpan;

            if( outer )
            {
                refSpan = collectDielectricToReference( layers, signalIdx, dir );
            }
            else
            {
                const DIELECTRIC_SPAN top = collectDielectricToReference( layers, signalIdx, -1 );
                const DIELECTRIC_SPAN bot = collectDielectricToReference( layers, signalIdx, +1 );

                if( top.valid() && ( !bot.valid() || top.heightM <= bot.heightM ) )
                    refSpan = top;
                else
                    refSpan = bot;
            }

            if( refSpan.valid() )
            {
                const long long refHeightIU = static_cast<long long>( refSpan.heightM * 1e9 + 0.5 );

                if( COPLANAR_GEOMETRY cpg = findCoplanarGround( aBoard, aTrack, refHeightIU,
                                                                m_referenceNets );
                    cpg.found )
                {
                    BOARD_STACKUP_ITEM* signal           = layers[signalIdx];
                    const double        signalThicknessM = signal->GetThickness() / 1e9;
                    const double        widthM           = width / 1e9;

                    auto [dk, df] = djordjevicSarkar( refSpan.dk, refSpan.df,
                                                      m_params.dkMeasurementFreqHz,
                                                      m_params.frequencyHz );
                    const LINE_RESULT cp = analyseCoplanar( widthM, signalThicknessM,
                                                            refSpan.heightM, dk, df, cpg.gapM,
                                                            /*grounded*/ true, m_params );

                    if( cp.z0Ohms > 0 )
                    {
                        result.model                   = IMPEDANCE_MODEL::GROUNDED_COPLANAR;
                        result.singleEndedOhms         = cp.z0Ohms;
                        result.conductorLossDbPerInch  = cp.condLossDbPerM * M_PER_INCH;
                        result.dielectricLossDbPerInch = cp.dielLossDbPerM * M_PER_INCH;
                        return result;
                    }
                }
            }
        }
    }

    // 3) Single-ended fallback (microstrip / stripline).
    return computeGeometry( stackup, layer, width, 0 );
}


IMPEDANCE_RESULT IMPEDANCE_CALCULATOR::ComputeForGeometry( BOARD* aBoard, PCB_LAYER_ID aLayer,
                                                           int aWidthIU, int aGapIU )
{
    if( !aBoard || aWidthIU <= 0 || !IsCopperLayer( aLayer ) )
        return {};

    applyBoardParams( aBoard );

    const BOARD_STACKUP stackup = aBoard->GetStackupOrDefault();

    IMPEDANCE_RESULT result = computeGeometry( stackup, aLayer, aWidthIU, aGapIU );

    // If a differential result couldn't be produced (e.g. no valid dielectric span), fall back
    // to single-ended so the router still shows a number.
    if( !result.valid() && aGapIU > 0 )
        result = computeGeometry( stackup, aLayer, aWidthIU, 0 );

    return result;
}
