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

#include "dialog_module_block_properties.h"

#include <sch_module_block.h>
#include <sch_module_pin.h>
#include <base_units.h>

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>


DIALOG_MODULE_BLOCK_PROPERTIES::DIALOG_MODULE_BLOCK_PROPERTIES( wxWindow* aParent,
                                                                SCH_MODULE_BLOCK* aBlock ) :
        DIALOG_SHIM( aParent, wxID_ANY, _( "Module Block Properties" ),
                     wxDefaultPosition, wxDefaultSize,
                     wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER ),
        m_block( aBlock )
{
    wxBoxSizer*  mainSizer = new wxBoxSizer( wxVERTICAL );
    wxFlexGridSizer* grid = new wxFlexGridSizer( 0, 2, 6, 8 );
    grid->AddGrowableCol( 1 );

    auto addRow = [&]( const wxString& aLabel, wxTextCtrl** aCtrl, bool aReadOnly )
    {
        grid->Add( new wxStaticText( this, wxID_ANY, aLabel ), 0,
                   wxALIGN_CENTER_VERTICAL | wxLEFT, 4 );

        long style = aReadOnly ? wxTE_READONLY : 0;
        *aCtrl = new wxTextCtrl( this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                 wxDefaultSize, style );
        grid->Add( *aCtrl, 1, wxEXPAND | wxRIGHT, 4 );
    };

    addRow( _( "Display name:" ),       &m_displayName,     false );
    addRow( _( "MBS reference:" ),      &m_mbsReference,    false );
    addRow( _( "Connector ref (sub):" ), &m_componentRef,    true  );
    addRow( _( "Sub-project path:" ),   &m_subProjectPath,  true  );
    addRow( _( "Width (mm):" ),         &m_widthMm,         false );
    addRow( _( "Height (mm):" ),        &m_heightMm,        false );
    addRow( _( "Pins:" ),               &m_pinCount,        true  );

    mainSizer->Add( grid, 1, wxALL | wxEXPAND, 12 );

    wxStdDialogButtonSizer* buttons = CreateStdDialogButtonSizer( wxOK | wxCANCEL );
    mainSizer->Add( buttons, 0, wxALL | wxEXPAND, 8 );

    SetSizerAndFit( mainSizer );
    SetMinSize( wxSize( 400, -1 ) );

    finishDialogSettings();
}


bool DIALOG_MODULE_BLOCK_PROPERTIES::TransferDataToWindow()
{
    if( !m_block )
        return false;

    m_displayName->SetValue( m_block->GetDisplayName() );
    m_mbsReference->SetValue( m_block->GetMbsReference() );
    m_componentRef->SetValue( m_block->GetComponentRef() );
    m_subProjectPath->SetValue( m_block->GetSubProjectPath() );

    auto toMmStr = []( int aIu )
    {
        return wxString::FromDouble( schIUScale.IUTomm( aIu ), 3 );
    };

    m_widthMm->SetValue( toMmStr( m_block->GetSize().x ) );
    m_heightMm->SetValue( toMmStr( m_block->GetSize().y ) );

    m_pinCount->SetValue( wxString::Format( wxT( "%zu" ), m_block->GetPins().size() ) );

    return true;
}


bool DIALOG_MODULE_BLOCK_PROPERTIES::TransferDataFromWindow()
{
    if( !m_block )
        return false;

    m_block->SetDisplayName( m_displayName->GetValue() );
    m_block->SetMbsReference( m_mbsReference->GetValue() );

    auto parseMm = []( const wxString& aText, int aFallback ) -> int
    {
        double mm = 0.0;

        if( aText.ToDouble( &mm ) && mm > 0.0 )
            return schIUScale.mmToIU( mm );

        return aFallback;
    };

    VECTOR2I newSize( parseMm( m_widthMm->GetValue(), m_block->GetSize().x ),
                      parseMm( m_heightMm->GetValue(), m_block->GetSize().y ) );

    m_block->SetSize( newSize );

    return true;
}
