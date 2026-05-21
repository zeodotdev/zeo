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
#include <board_stackup_manager/board_stackup.h>
#include <transline_calculations/microstrip.h>
#include <transline_calculations/stripline.h>
#include <transline_calculations/units_scales.h>

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


    // Walk towards a side and accumulate dielectric height and weighted-average Dk
    // until the next copper layer.  Returns {0, 0} on failure.  Height in metres.
    std::pair<double, double> collectDielectricToReference(
            const std::vector<BOARD_STACKUP_ITEM*>& aLayers, int aSignalIdx, int aDirection )
    {
        double totalThicknessIU = 0.0;
        double weightedDk = 0.0;

        for( int i = aSignalIdx + aDirection; i >= 0 && i < (int) aLayers.size(); i += aDirection )
        {
            BOARD_STACKUP_ITEM* item = aLayers[i];

            if( !item || !item->IsEnabled() )
                continue;

            if( item->GetType() == BS_ITEM_TYPE_COPPER )
                break;

            if( item->GetType() != BS_ITEM_TYPE_DIELECTRIC )
                continue;

            const int t = item->GetThickness();

            if( t <= 0 )
                continue;

            totalThicknessIU += t;
            weightedDk += t * item->GetEpsilonR();
        }

        if( totalThicknessIU <= 0 )
            return { 0.0, 0.0 };

        return { totalThicknessIU / 1e9, weightedDk / totalThicknessIU };
    }


    int analyseMicrostrip( double aWidthM, double aSignalThicknessM, double aHeightM, double aDk )
    {
        MICROSTRIP ms;
        ms.SetParameter( TRANSLINE_PARAMETERS::PHYS_WIDTH, aWidthM );
        ms.SetParameter( TRANSLINE_PARAMETERS::T, aSignalThicknessM );
        ms.SetParameter( TRANSLINE_PARAMETERS::H, aHeightM );
        ms.SetParameter( TRANSLINE_PARAMETERS::H_T, 1e6 );  // free air above; effectively infinite
        ms.SetParameter( TRANSLINE_PARAMETERS::EPSILONR, aDk );
        ms.SetParameter( TRANSLINE_PARAMETERS::MUR, 1.0 );
        ms.SetParameter( TRANSLINE_PARAMETERS::MURC, 1.0 );
        ms.SetParameter( TRANSLINE_PARAMETERS::FREQUENCY, 1e9 );
        ms.SetParameter( TRANSLINE_PARAMETERS::TAND, 0.0 );
        ms.SetParameter( TRANSLINE_PARAMETERS::ROUGH, 0.0 );
        ms.SetParameter( TRANSLINE_PARAMETERS::SIGMA, 1.0 / COPPER_RHO );
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
                          double aDk )
    {
        STRIPLINE sl;
        sl.SetParameter( TRANSLINE_PARAMETERS::PHYS_WIDTH, aWidthM );
        sl.SetParameter( TRANSLINE_PARAMETERS::T, aSignalThicknessM );
        sl.SetParameter( TRANSLINE_PARAMETERS::H, aTopH + aBotH + aSignalThicknessM );
        sl.SetParameter( TRANSLINE_PARAMETERS::STRIPLINE_A, aTopH );
        sl.SetParameter( TRANSLINE_PARAMETERS::EPSILONR, aDk );
        sl.SetParameter( TRANSLINE_PARAMETERS::MUR, 1.0 );
        sl.SetParameter( TRANSLINE_PARAMETERS::MURC, 1.0 );
        sl.SetParameter( TRANSLINE_PARAMETERS::FREQUENCY, 1e9 );
        sl.SetParameter( TRANSLINE_PARAMETERS::TAND, 0.0 );
        sl.SetParameter( TRANSLINE_PARAMETERS::SIGMA, 1.0 / COPPER_RHO );
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

    if( signalIdx < 0 || aWidthIU <= 0 )
    {
        m_cache[key] = 0;
        return 0;
    }

    BOARD_STACKUP_ITEM* signal           = layers[signalIdx];
    const double        signalThicknessM = signal->GetThickness() / 1e9;
    const double        widthM           = aWidthIU / 1e9;

    int z0Ohms = 0;

    if( aLayer == F_Cu || aLayer == B_Cu )
    {
        auto [heightM, dk] = collectDielectricToReference( layers, signalIdx,
                                                           ( aLayer == F_Cu ) ? +1 : -1 );

        if( heightM > 0.0 && dk > 0.0 )
            z0Ohms = analyseMicrostrip( widthM, signalThicknessM, heightM, dk );
    }
    else
    {
        auto [topH, topDk] = collectDielectricToReference( layers, signalIdx, -1 );
        auto [botH, botDk] = collectDielectricToReference( layers, signalIdx, +1 );

        if( topH > 0.0 && botH > 0.0 )
        {
            const double totalH = topH + botH;
            const double dk     = ( topH * topDk + botH * botDk ) / totalH;
            z0Ohms              = analyseStripline( widthM, signalThicknessM, topH, botH, dk );
        }
    }

    m_cache[key] = z0Ohms;
    return z0Ohms;
}
