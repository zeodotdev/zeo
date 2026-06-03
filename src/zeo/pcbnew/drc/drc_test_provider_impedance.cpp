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

#include <board.h>
#include <board_stackup_manager/board_stackup.h>
#include <board_stackup_manager/impedance_calculator.h>
#include <pcb_track.h>
#include <drc/drc_engine.h>
#include <drc/drc_item.h>
#include <drc/drc_test_provider.h>
#include <layer_ids.h>
#include <macros.h>

/*
    Impedance target test: for each routed track on a copper layer, compute Z₀ from
    the active stackup geometry (track width, layer thickness, adjacent dielectric Dk
    and height) and verify it falls inside the IMPEDANCE_TARGET_CONSTRAINT's min/max
    range.

    The model is chosen per track by IMPEDANCE_CALCULATOR::ComputeForTrack(): coupled
    microstrip / stripline for differential pairs (checked against the differential target),
    (grounded) coplanar where adjacent ground copper is detected, otherwise single-ended
    microstrip (outer) / stripline (inner).

    Errors generated:
    - DRCE_IMPEDANCE_OUT_OF_RANGE
*/


class DRC_TEST_PROVIDER_IMPEDANCE : public DRC_TEST_PROVIDER
{
public:
    DRC_TEST_PROVIDER_IMPEDANCE() = default;
    virtual ~DRC_TEST_PROVIDER_IMPEDANCE() = default;

    virtual bool Run() override;

    virtual const wxString GetName() const override { return wxT( "impedance" ); }

private:
    IMPEDANCE_CALCULATOR m_calculator;
};


bool DRC_TEST_PROVIDER_IMPEDANCE::Run()
{
    if( m_drcEngine->IsErrorLimitExceeded( DRCE_IMPEDANCE_OUT_OF_RANGE ) )
    {
        REPORT_AUX( wxT( "Impedance violations ignored. Skipping check." ) );
        return true;
    }

    if( !m_drcEngine->HasRulesForConstraintType( IMPEDANCE_TARGET_CONSTRAINT ) )
    {
        REPORT_AUX( wxT( "No impedance target constraints found. Tests not run." ) );
        return true;
    }

    if( !reportPhase( _( "Checking track impedance..." ) ) )
        return false;

    BOARD* board = m_drcEngine->GetBoard();

    if( !board )
        return true;

    const BOARD_STACKUP stackup = board->GetStackupOrDefault();

    // Sanity: at least one dielectric must declare a Dk; otherwise impedance is meaningless.
    bool stackupHasDk = false;

    for( BOARD_STACKUP_ITEM* item : stackup.GetList() )
    {
        if( item && item->IsEnabled() && item->GetType() == BS_ITEM_TYPE_DIELECTRIC
            && item->GetEpsilonR() > 1.0 )
        {
            stackupHasDk = true;
            break;
        }
    }

    if( !stackupHasDk )
    {
        REPORT_AUX( wxT( "Stackup has no dielectric constants; impedance check skipped." ) );
        return true;
    }

    m_calculator.ClearCache();

    LSET copperLayers = LSET::AllCuMask( board->GetCopperLayerCount() );

    size_t count = 0;
    size_t ii    = 0;

    forEachGeometryItem( { PCB_TRACE_T, PCB_ARC_T }, copperLayers,
            [&]( BOARD_ITEM* ) -> bool
            {
                count++;
                return true;
            } );

    if( count == 0 )
        return true;

    forEachGeometryItem( { PCB_TRACE_T, PCB_ARC_T }, copperLayers,
            [&]( BOARD_ITEM* item ) -> bool
            {
                if( !reportProgress( ii++, count, 100 ) )
                    return false;

                if( m_drcEngine->IsErrorLimitExceeded( DRCE_IMPEDANCE_OUT_OF_RANGE ) )
                    return false;

                PCB_TRACK* track = dynamic_cast<PCB_TRACK*>( item );

                if( !track )
                    return true;

                DRC_CONSTRAINT constraint = m_drcEngine->EvalRules( IMPEDANCE_TARGET_CONSTRAINT,
                                                                    track, nullptr,
                                                                    track->GetLayer() );

                if( constraint.IsNull() || constraint.GetSeverity() == RPT_SEVERITY_IGNORE )
                    return true;

                const IMPEDANCE_RESULT imp = m_calculator.ComputeForTrack( board, track );

                if( !imp.valid() )
                    return true;  // stackup didn't yield a usable impedance — skip silently

                // Differential pairs are checked against the differential target; all other
                // models against the single-ended Z₀.
                const bool     isDiff = imp.isDifferential() && imp.differentialOhms > 0;
                const int      z0     = isDiff ? imp.differentialOhms : imp.singleEndedOhms;
                const wxString label  = isDiff ? _( "differential impedance" ) : _( "impedance" );

                if( z0 <= 0 )
                    return true;

                const auto& mom = constraint.m_Value;

                const bool tooLow  = mom.HasMin() && z0 < mom.Min();
                const bool tooHigh = mom.HasMax() && z0 > mom.Max();

                if( !tooLow && !tooHigh )
                    return true;

                std::shared_ptr<DRC_ITEM> drcItem = DRC_ITEM::Create( DRCE_IMPEDANCE_OUT_OF_RANGE );

                wxString       msg;
                const wxString src = constraint.GetName();

                if( tooLow )
                {
                    msg = wxString::Format( _( "(%s %s %d Ω below minimum %d Ω)" ),
                                            src, label, z0, mom.Min() );
                }
                else
                {
                    msg = wxString::Format( _( "(%s %s %d Ω above maximum %d Ω)" ),
                                            src, label, z0, mom.Max() );
                }

                drcItem->SetErrorDetail( msg );
                drcItem->SetItems( track );
                drcItem->SetViolatingRule( constraint.GetParentRule() );

                reportViolation( drcItem, track->GetPosition(), track->GetLayer() );

                return true;
            } );

    return !m_drcEngine->IsCancelled();
}


namespace detail
{
    static DRC_REGISTER_TEST_PROVIDER<DRC_TEST_PROVIDER_IMPEDANCE> dummy;
}
