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

#include <dialogs/dialog_variant_diff.h>

#include <sch_edit_frame.h>
#include <schematic.h>
#include <sch_reference_list.h>
#include <sch_symbol.h>
#include <sch_field.h>
#include <wx/choice.h>
#include <wx/listctrl.h>
#include <wx/sizer.h>
#include <wx/stattext.h>


DIALOG_VARIANT_DIFF::DIALOG_VARIANT_DIFF( SCH_EDIT_FRAME* aParent ) :
        DIALOG_SHIM( aParent, wxID_ANY, _( "Compare Variants" ),
                     wxDefaultPosition, wxSize( 800, 500 ),
                     wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER ),
        m_parent( aParent )
{
    wxBoxSizer* topSizer = new wxBoxSizer( wxVERTICAL );

    // Top row: variant pickers.
    wxBoxSizer* pickerRow = new wxBoxSizer( wxHORIZONTAL );

    pickerRow->Add( new wxStaticText( this, wxID_ANY, _( "Variant A:" ) ), 0,
                    wxALIGN_CENTER_VERTICAL | wxRIGHT, 5 );

    m_variantAChoice = new wxChoice( this, wxID_ANY );
    pickerRow->Add( m_variantAChoice, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 15 );

    pickerRow->Add( new wxStaticText( this, wxID_ANY, _( "Variant B:" ) ), 0,
                    wxALIGN_CENTER_VERTICAL | wxRIGHT, 5 );

    m_variantBChoice = new wxChoice( this, wxID_ANY );
    pickerRow->Add( m_variantBChoice, 1, wxALIGN_CENTER_VERTICAL );

    topSizer->Add( pickerRow, 0, wxEXPAND | wxALL, 8 );

    // Populate both pickers with "(Base)" + all named variants.  Base = empty variant name.
    auto fillPicker = [this]( wxChoice* picker )
    {
        picker->Append( _( "(Base)" ) );

        for( const wxString& v : m_parent->Schematic().GetVariantNamesForUI() )
            picker->Append( v );

        picker->SetSelection( 0 );
    };

    fillPicker( m_variantAChoice );
    fillPicker( m_variantBChoice );

    // Default to a useful comparison: Base vs. first non-base variant if any exists.
    if( m_variantBChoice->GetCount() > 1 )
        m_variantBChoice->SetSelection( 1 );

    m_variantAChoice->Bind( wxEVT_CHOICE, &DIALOG_VARIANT_DIFF::onChoiceChanged, this );
    m_variantBChoice->Bind( wxEVT_CHOICE, &DIALOG_VARIANT_DIFF::onChoiceChanged, this );

    // The diff table itself.
    m_diffList = new wxListView( this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                 wxLC_REPORT | wxLC_SINGLE_SEL );
    m_diffList->AppendColumn( _( "Reference" ), wxLIST_FORMAT_LEFT, 90 );
    m_diffList->AppendColumn( _( "Field" ),     wxLIST_FORMAT_LEFT, 130 );
    m_diffList->AppendColumn( _( "Variant A" ), wxLIST_FORMAT_LEFT, 230 );
    m_diffList->AppendColumn( _( "Variant B" ), wxLIST_FORMAT_LEFT, 230 );

    topSizer->Add( m_diffList, 1, wxEXPAND | wxLEFT | wxRIGHT, 8 );

    m_summary = new wxStaticText( this, wxID_ANY, wxEmptyString );
    topSizer->Add( m_summary, 0, wxALL, 8 );

    topSizer->Add( CreateButtonSizer( wxCLOSE ), 0, wxEXPAND | wxALL, 8 );

    SetSizer( topSizer );

    rebuildList();
}


void DIALOG_VARIANT_DIFF::onChoiceChanged( wxCommandEvent& )
{
    rebuildList();
}


void DIALOG_VARIANT_DIFF::rebuildList()
{
    m_diffList->DeleteAllItems();

    auto selToVariantName = [this]( wxChoice* picker ) -> wxString
    {
        // Index 0 is "(Base)" which maps to an empty variant name (no override).
        if( !picker || picker->GetSelection() <= 0 )
            return wxEmptyString;

        return picker->GetString( picker->GetSelection() );
    };

    const wxString variantA = selToVariantName( m_variantAChoice );
    const wxString variantB = selToVariantName( m_variantBChoice );

    if( variantA == variantB )
    {
        m_summary->SetLabel( _( "Pick two different variants to see differences." ) );
        return;
    }

    SCH_REFERENCE_LIST refs;
    m_parent->Schematic().Hierarchy().GetSymbols( refs, SYMBOL_FILTER_ALL );

    // Field name set to compare.  Use Reference / Value / Footprint / Datasheet + any user
    // fields present on each symbol.  Hidden internal fields are skipped.
    int diffCount = 0;

    for( unsigned i = 0; i < refs.GetCount(); ++i )
    {
        SCH_REFERENCE&  ref     = refs[i];
        SCH_SYMBOL*     symbol  = ref.GetSymbol();
        SCH_SHEET_PATH& sheet   = ref.GetSheetPath();

        if( !symbol )
            continue;

        std::vector<SCH_FIELD*> symbolFields;

        for( SCH_FIELD& f : symbol->GetFields() )
        {
            if( !f.IsPrivate() )
                symbolFields.push_back( &f );
        }

        for( SCH_FIELD* field : symbolFields )
        {
            const wxString fieldName = field->GetCanonicalName().IsEmpty()
                                              ? field->GetName()
                                              : field->GetCanonicalName();

            const wxString aVal = symbol->GetFieldText( fieldName, &sheet, variantA );
            const wxString bVal = symbol->GetFieldText( fieldName, &sheet, variantB );

            if( aVal == bVal )
                continue;

            const long row = m_diffList->InsertItem( m_diffList->GetItemCount(),
                                                     ref.GetRef() );
            m_diffList->SetItem( row, 1, fieldName );
            m_diffList->SetItem( row, 2, aVal );
            m_diffList->SetItem( row, 3, bVal );
            diffCount++;
        }
    }

    if( diffCount == 0 )
    {
        m_summary->SetLabel(
                wxString::Format( _( "No field differences between %s and %s." ),
                                  variantA.IsEmpty() ? _( "(Base)" ) : variantA,
                                  variantB.IsEmpty() ? _( "(Base)" ) : variantB ) );
    }
    else
    {
        m_summary->SetLabel( wxString::Format( _( "%d field difference(s)." ), diffCount ) );
    }
}
