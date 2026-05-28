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

#include "impedance_calculator.h"

#include <board.h>
#include <board_design_settings.h>
#include <board_stackup_manager/board_stackup.h>
#include <transline_calculations/microstrip.h>
#include <transline_calculations/stripline.h>
#include <transline_calculations/units_scales.h>
#include <trace_helpers.h>
#include <wx/log.h>

static const wxChar IMPEDANCE_TRACE[] = wxT( "IMPEDANCE" );

namespace
{
    constexpr double COPPER_RHO = 1.72e-8;  // Ω·m at 20 °C — matches tuning profile dialog


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
        double weightedDk        = 0.0;
        double weightedDf        = 0.0;

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


    int analyseMicrostrip( double aWidthM, double aSignalThicknessM, double aHeightM, double aDk,
                           double aDf, const IMPEDANCE_PARAMS& aParams )
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
        ms.SetParameter( TRANSLINE_PARAMETERS::ROUGH, aParams.roughnessM );
        ms.SetParameter( TRANSLINE_PARAMETERS::SIGMA, 1.0 / rho );
        ms.SetParameter( TRANSLINE_PARAMETERS::PHYS_LEN, 1.0 );
        ms.SetParameter( TRANSLINE_PARAMETERS::ANG_L, 1.0 );

        ms.Analyse();

        auto& results = ms.GetAnalysisResults();
        auto  zIt = results.find( TRANSLINE_PARAMETERS::Z0 );

        if( zIt == results.end() || zIt->second.second != TRANSLINE_STATUS::OK )
            return 0;

        return static_cast<int>( zIt->second.first + 0.5 );
    }


    int analyseStripline( double aWidthM, double aSignalThicknessM, double aTopH, double aBotH,
                          double aDk, double aDf, const IMPEDANCE_PARAMS& aParams )
    {
        const double rho = aParams.conductorRho > 0.0 ? aParams.conductorRho : 1.72e-8;

        // STRIPLINE's m_parameters map does NOT include MUR or ROUGH — SetParameter uses
        // unordered_map::at() and throws std::out_of_range if we pass unsupported keys.
        // Roughness therefore can't be modelled for striplines yet (tracked as Phase 2).
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
        sl.SetParameter( TRANSLINE_PARAMETERS::PHYS_LEN, 1.0 );
        sl.SetParameter( TRANSLINE_PARAMETERS::ANG_L, 1.0 );

        sl.Analyse();

        auto& results = sl.GetAnalysisResults();
        auto  zIt = results.find( TRANSLINE_PARAMETERS::Z0 );

        if( zIt == results.end() || zIt->second.second != TRANSLINE_STATUS::OK )
            return 0;

        return static_cast<int>( zIt->second.first + 0.5 );
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

        wxLogTrace( IMPEDANCE_TRACE,
                    "  microstrip: signalT=%.6f widthM=%.6f heightM=%.6f dk=%.3f df=%.4f",
                    signalThicknessM, widthM, span.heightM, span.dk, span.df );

        if( span.valid() )
            z0Ohms = analyseMicrostrip( widthM, signalThicknessM, span.heightM, span.dk, span.df,
                                        m_params );
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
            const double dk     = ( top.heightM * top.dk + bot.heightM * bot.dk ) / totalH;
            const double df     = ( top.heightM * top.df + bot.heightM * bot.df ) / totalH;
            z0Ohms              = analyseStripline( widthM, signalThicknessM, top.heightM,
                                                    bot.heightM, dk, df, m_params );
        }
    }

    wxLogTrace( IMPEDANCE_TRACE, "  → z0=%d Ω", z0Ohms );

    m_cache[key] = z0Ohms;
    return z0Ohms;
}
