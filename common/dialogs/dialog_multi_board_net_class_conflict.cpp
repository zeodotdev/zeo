/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
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

#include "dialog_multi_board_net_class_conflict.h"

#include <base_units.h>
#include <netclass.h>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/sizer.h>
#include <wx/stattext.h>


DIALOG_MULTI_BOARD_NET_CLASS_CONFLICT::DIALOG_MULTI_BOARD_NET_CLASS_CONFLICT(
        wxWindow* aParent, const MULTI_BOARD_NET_CLASS_CONFLICT& aConflict ) :
        DIALOG_SHIM( aParent, wxID_ANY,
                     wxString::Format( _( "Net Class Conflict — '%s'" ),
                                       aConflict.netClassName ),
                     wxDefaultPosition, wxDefaultSize,
                     wxDEFAULT_DIALOG_STYLE )
{
    wxBoxSizer* mainSizer = new wxBoxSizer( wxVERTICAL );

    // Header — explains why the dialog appeared. Same wording for every
    // conflict; the body has the actual values.
    wxStaticText* header = new wxStaticText(
            this, wxID_ANY,
            wxString::Format(
                    _( "The multi-board container and sub-project '%s' both define a net "
                       "class named '%s' but with different settings. Choose how to "
                       "resolve the conflict." ),
                    aConflict.subProjectDisplayName, aConflict.netClassName ) );
    header->Wrap( 480 );
    mainSizer->Add( header, 0, wxALL | wxEXPAND, 12 );

    // Side-by-side comparison. Two stat-box sizers under a horizontal
    // box so the user can visually diff container vs sub-project.
    wxBoxSizer* compareSizer = new wxBoxSizer( wxHORIZONTAL );

    wxStaticBoxSizer* containerBox =
            new wxStaticBoxSizer( wxVERTICAL, this, _( "Container value" ) );
    containerBox->Add( new wxStaticText(
                              this, wxID_ANY,
                              formatNetclassDescription( aConflict.containerNetClass ) ),
                       0, wxALL, 6 );

    wxStaticBoxSizer* subBox = new wxStaticBoxSizer(
            wxVERTICAL, this,
            wxString::Format( _( "Sub-project '%s' value" ), aConflict.subProjectDisplayName ) );
    subBox->Add( new wxStaticText(
                         this, wxID_ANY,
                         formatNetclassDescription( aConflict.subProjectNetClass ) ),
                 0, wxALL, 6 );

    compareSizer->Add( containerBox, 1, wxALL | wxEXPAND, 6 );
    compareSizer->Add( subBox,       1, wxALL | wxEXPAND, 6 );
    mainSizer->Add( compareSizer, 1, wxALL | wxEXPAND, 6 );

    // "Apply to all" — single checkbox above the action buttons.
    m_applyToAllCheckbox = new wxCheckBox(
            this, wxID_ANY,
            _( "Apply this choice to all remaining conflicts in this sync" ) );
    mainSizer->Add( m_applyToAllCheckbox, 0, wxALL | wxALIGN_LEFT, 12 );

    // Action buttons — three custom verbs (no OK/Cancel; Cancel maps to
    // SKIP via the dialog's close-box, see below).
    wxBoxSizer* btnSizer = new wxBoxSizer( wxHORIZONTAL );

    wxButton* useContainerBtn =
            new wxButton( this, wxID_ANY, _( "Use container value" ) );
    wxButton* keepSubBtn =
            new wxButton( this, wxID_ANY, _( "Keep sub-project value" ) );
    wxButton* skipBtn = new wxButton( this, wxID_ANY, _( "Skip this class" ) );

    useContainerBtn->Bind( wxEVT_BUTTON,
                           &DIALOG_MULTI_BOARD_NET_CLASS_CONFLICT::onUseContainer,
                           this );
    keepSubBtn->Bind( wxEVT_BUTTON,
                      &DIALOG_MULTI_BOARD_NET_CLASS_CONFLICT::onKeepSubProject,
                      this );
    skipBtn->Bind( wxEVT_BUTTON,
                   &DIALOG_MULTI_BOARD_NET_CLASS_CONFLICT::onSkip,
                   this );

    useContainerBtn->SetDefault();   // Enter accepts the recommended choice

    btnSizer->Add( skipBtn,         0, wxALL, 6 );
    btnSizer->AddStretchSpacer();
    btnSizer->Add( keepSubBtn,      0, wxALL, 6 );
    btnSizer->Add( useContainerBtn, 0, wxALL, 6 );
    mainSizer->Add( btnSizer, 0, wxALL | wxEXPAND, 6 );

    SetSizerAndFit( mainSizer );
    CenterOnParent();
}


wxString DIALOG_MULTI_BOARD_NET_CLASS_CONFLICT::formatNetclassDescription(
        const std::shared_ptr<NETCLASS>& aNc ) const
{
    if( !aNc )
        return _( "(missing)" );

    // Render in mm for length-like fields — same convention the rule
    // editor uses. Uses pcbIUScale for IU↔mm conversion.
    wxString out;

    auto addMm = [&]( const wxString& aLabel, int aIU )
    {
        out += wxString::Format( wxT( "%s: %.4f mm\n" ), aLabel,
                                 pcbIUScale.IUTomm( aIU ) );
    };

    if( aNc->HasClearance() )
        addMm( _( "Clearance" ), aNc->GetClearance() );
    if( aNc->HasTrackWidth() )
        addMm( _( "Track width" ), aNc->GetTrackWidth() );
    if( aNc->HasViaDiameter() )
        addMm( _( "Via diameter" ), aNc->GetViaDiameter() );
    if( aNc->HasViaDrill() )
        addMm( _( "Via drill" ), aNc->GetViaDrill() );
    if( aNc->HasuViaDiameter() )
        addMm( _( "Microvia diameter" ), aNc->GetuViaDiameter() );
    if( aNc->HasuViaDrill() )
        addMm( _( "Microvia drill" ), aNc->GetuViaDrill() );
    if( aNc->HasDiffPairWidth() )
        addMm( _( "Diff pair width" ), aNc->GetDiffPairWidth() );
    if( aNc->HasDiffPairGap() )
        addMm( _( "Diff pair gap" ), aNc->GetDiffPairGap() );
    if( aNc->HasDiffPairViaGap() )
        addMm( _( "Diff pair via gap" ), aNc->GetDiffPairViaGap() );

    if( aNc->HasWireWidth() )
        out += wxString::Format( _( "Wire width: %d mils\n" ),
                                 schIUScale.IUToMils( aNc->GetWireWidth() ) );
    if( aNc->HasBusWidth() )
        out += wxString::Format( _( "Bus width: %d mils\n" ),
                                 schIUScale.IUToMils( aNc->GetBusWidth() ) );
    if( aNc->HasLineStyle() )
        out += wxString::Format( _( "Line style: %d\n" ), aNc->GetLineStyle() );

    if( !aNc->GetTuningProfile().IsEmpty() )
        out += wxString::Format( _( "Tuning profile: %s\n" ), aNc->GetTuningProfile() );

    // Always render colors. Differences here are the most common silent
    // conflict driver — the equivalence check looks at both colors but
    // earlier this dialog only printed numeric fields, leaving the user
    // staring at "different settings" with two identical-looking panes.
    auto colorLabel = [&]( const KIGFX::COLOR4D& c ) -> wxString
    {
        if( c == KIGFX::COLOR4D::UNSPECIFIED )
            return _( "(unset)" );
        return c.ToCSSString();
    };

    out += wxString::Format( _( "PCB color: %s\n" ),
                             colorLabel( aNc->GetPcbColor( true ) ) );
    out += wxString::Format( _( "Schematic color: %s\n" ),
                             colorLabel( aNc->GetSchematicColor( true ) ) );

    // Priority is intentionally NOT compared by MultiBoardNetclassesEquivalent
    // (it's per-board grid-row metadata), so it's never the cause of a
    // conflict — but we still display it for diagnostic context.
    out += wxString::Format( _( "Priority: %d (info only)" ), aNc->GetPriority() );

    return out;
}


void DIALOG_MULTI_BOARD_NET_CLASS_CONFLICT::onUseContainer( wxCommandEvent& )
{
    m_resolution = MULTI_BOARD_NET_CLASS_RESOLUTION::USE_CONTAINER;
    m_applyToAll = m_applyToAllCheckbox && m_applyToAllCheckbox->GetValue();
    EndModal( wxID_OK );
}


void DIALOG_MULTI_BOARD_NET_CLASS_CONFLICT::onKeepSubProject( wxCommandEvent& )
{
    m_resolution = MULTI_BOARD_NET_CLASS_RESOLUTION::KEEP_SUB_PROJECT;
    m_applyToAll = m_applyToAllCheckbox && m_applyToAllCheckbox->GetValue();
    EndModal( wxID_OK );
}


void DIALOG_MULTI_BOARD_NET_CLASS_CONFLICT::onSkip( wxCommandEvent& )
{
    m_resolution = MULTI_BOARD_NET_CLASS_RESOLUTION::SKIP;
    m_applyToAll = m_applyToAllCheckbox && m_applyToAllCheckbox->GetValue();
    EndModal( wxID_OK );
}
