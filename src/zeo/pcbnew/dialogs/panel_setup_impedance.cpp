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

#include <dialogs/panel_setup_impedance.h>

#include <board.h>
#include <board_design_settings.h>
#include <pcb_edit_frame.h>
#include <widgets/std_bitmap_button.h>

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/choice.h>
#include <wx/statline.h>

namespace
{
    // Frequency unit factors (multiply field value by factor → Hz).
    constexpr double GHZ = 1.0e9;
    constexpr double MHZ = 1.0e6;
    constexpr double KHZ = 1.0e3;

    // Conductor resistivity is edited in µΩ·cm; 1 µΩ·cm = 1e-8 Ω·m.
    constexpr double UOHM_CM_TO_OHM_M = 1.0e-8;

    // Roughness is edited in µm; 1 µm = 1e-6 m.
    constexpr double UM_TO_M = 1.0e-6;


    double freqUnitFactor( int aSel )
    {
        switch( aSel )
        {
        case 1:  return MHZ;
        case 2:  return KHZ;
        default: return GHZ;
        }
    }


    /// Parse a positive double from a control; returns aFallback on blank/invalid/non-positive.
    double parsePositive( const wxTextCtrl* aCtrl, double aFallback )
    {
        double v = 0.0;

        if( aCtrl && aCtrl->GetValue().ToDouble( &v ) && v > 0.0 )
            return v;

        return aFallback;
    }
}


PANEL_SETUP_IMPEDANCE::PANEL_SETUP_IMPEDANCE( wxWindow* aParentWindow, PCB_EDIT_FRAME* aFrame ) :
        wxPanel( aParentWindow, wxID_ANY ),
        m_frame( aFrame ),
        m_bds( &aFrame->GetBoard()->GetDesignSettings() )
{
    wxBoxSizer* topSizer = new wxBoxSizer( wxVERTICAL );

    wxStaticText* intro = new wxStaticText(
            this, wxID_ANY,
            _( "Signal-integrity analysis parameters used by the impedance / transmission-line "
               "calculation.\nThese are board-wide; per-net impedance targets are set via design "
               "rules and tuning profiles." ) );
    topSizer->Add( intro, 0, wxALL, 10 );

    topSizer->Add( new wxStaticLine( this ), 0, wxEXPAND | wxLEFT | wxRIGHT, 10 );

    wxFlexGridSizer* grid = new wxFlexGridSizer( 0, 3, 8, 8 );
    grid->AddGrowableCol( 1 );
    grid->SetFlexibleDirection( wxBOTH );

    auto addFreqUnitChoice = [this]() -> wxChoice*
    {
        wxArrayString units;
        units.Add( wxT( "GHz" ) );
        units.Add( wxT( "MHz" ) );
        units.Add( wxT( "kHz" ) );
        wxChoice* c = new wxChoice( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, units );
        c->SetSelection( 0 );
        return c;
    };

    // Reference frequency
    grid->Add( new wxStaticText( this, wxID_ANY, _( "Reference frequency:" ) ), 0,
               wxALIGN_CENTER_VERTICAL );
    m_refFreqCtrl = new wxTextCtrl( this, wxID_ANY );
    grid->Add( m_refFreqCtrl, 1, wxEXPAND );
    m_refFreqUnit = addFreqUnitChoice();
    grid->Add( m_refFreqUnit, 0, wxALIGN_CENTER_VERTICAL );

    // Dk measurement frequency
    grid->Add( new wxStaticText( this, wxID_ANY, _( "Dk specified at frequency:" ) ), 0,
               wxALIGN_CENTER_VERTICAL );
    m_dkFreqCtrl = new wxTextCtrl( this, wxID_ANY );
    grid->Add( m_dkFreqCtrl, 1, wxEXPAND );
    m_dkFreqUnit = addFreqUnitChoice();
    grid->Add( m_dkFreqUnit, 0, wxALIGN_CENTER_VERTICAL );

    // Conductor resistivity
    grid->Add( new wxStaticText( this, wxID_ANY, _( "Conductor resistivity:" ) ), 0,
               wxALIGN_CENTER_VERTICAL );
    m_resistivityCtrl = new wxTextCtrl( this, wxID_ANY );
    grid->Add( m_resistivityCtrl, 1, wxEXPAND );
    grid->Add( new wxStaticText( this, wxID_ANY, wxT( "µΩ·cm" ) ), 0, wxALIGN_CENTER_VERTICAL );

    // Conductor roughness
    grid->Add( new wxStaticText( this, wxID_ANY, _( "Copper surface roughness:" ) ), 0,
               wxALIGN_CENTER_VERTICAL );
    m_roughnessCtrl = new wxTextCtrl( this, wxID_ANY );
    grid->Add( m_roughnessCtrl, 1, wxEXPAND );
    grid->Add( new wxStaticText( this, wxID_ANY, wxT( "µm" ) ), 0, wxALIGN_CENTER_VERTICAL );

    topSizer->Add( grid, 0, wxEXPAND | wxALL, 10 );

    SetSizer( topSizer );
}


void PANEL_SETUP_IMPEDANCE::loadFromSettings( const BOARD_DESIGN_SETTINGS& aBds )
{
    // Frequencies are edited in GHz (the natural scale for controlled-impedance work).
    m_refFreqUnit->SetSelection( 0 );
    m_refFreqCtrl->ChangeValue( wxString::Format( wxT( "%g" ), aBds.m_SI_ReferenceFrequency / GHZ ) );

    m_dkFreqUnit->SetSelection( 0 );
    m_dkFreqCtrl->ChangeValue( wxString::Format( wxT( "%g" ), aBds.m_SI_DkMeasurementFrequency / GHZ ) );

    m_resistivityCtrl->ChangeValue(
            wxString::Format( wxT( "%g" ), aBds.m_SI_ConductorResistivity / UOHM_CM_TO_OHM_M ) );

    m_roughnessCtrl->ChangeValue(
            wxString::Format( wxT( "%g" ), aBds.m_SI_ConductorRoughness / UM_TO_M ) );
}


bool PANEL_SETUP_IMPEDANCE::TransferDataToWindow()
{
    loadFromSettings( *m_bds );
    return true;
}


bool PANEL_SETUP_IMPEDANCE::TransferDataFromWindow()
{
    // Frequencies: value × selected-unit factor → Hz. Fall back to 1 GHz if blank/invalid.
    m_bds->m_SI_ReferenceFrequency =
            parsePositive( m_refFreqCtrl, 1.0 ) * freqUnitFactor( m_refFreqUnit->GetSelection() );
    m_bds->m_SI_DkMeasurementFrequency =
            parsePositive( m_dkFreqCtrl, 1.0 ) * freqUnitFactor( m_dkFreqUnit->GetSelection() );

    m_bds->m_SI_ConductorResistivity =
            parsePositive( m_resistivityCtrl, 1.72 ) * UOHM_CM_TO_OHM_M;

    // Roughness may legitimately be 0 (smooth foil), so don't force-positive.
    double rough = 0.0;
    m_roughnessCtrl->GetValue().ToDouble( &rough );
    m_bds->m_SI_ConductorRoughness = ( rough >= 0.0 ? rough : 0.0 ) * UM_TO_M;

    return true;
}


void PANEL_SETUP_IMPEDANCE::ImportSettingsFrom( BOARD* aBoard )
{
    loadFromSettings( aBoard->GetDesignSettings() );
}
