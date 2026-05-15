/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <wx/filedlgcustomize.h>


/// Where a newly created library should be registered. The Container
/// option only renders when the active project is part of a multi-board
/// container (M7.1).
enum class LIBRARY_SAVE_TARGET
{
    GLOBAL,
    CONTAINER,
    PROJECT
};


class FILEDLG_HOOK_NEW_LIBRARY : public wxFileDialogCustomizeHook
{
public:
    /// Legacy 2-way (Global / Project) constructor. Kept so call sites
    /// outside the multi-board flow don't have to thread a target enum.
    FILEDLG_HOOK_NEW_LIBRARY( bool aDefaultUseGlobalTable ) :
            FILEDLG_HOOK_NEW_LIBRARY(
                    aDefaultUseGlobalTable ? LIBRARY_SAVE_TARGET::GLOBAL
                                           : LIBRARY_SAVE_TARGET::PROJECT,
                    /*aShowContainer=*/false )
    {}

    /// 3-way (Global / Container / Project) constructor. The Container
    /// radio is rendered only when @a aShowContainer is true. Used from
    /// MBSCH and sub-project editors so the user can fan a new library
    /// out across every board with one click — see locked-in decision
    /// #8 (physical replication).
    FILEDLG_HOOK_NEW_LIBRARY( LIBRARY_SAVE_TARGET aDefault, bool aShowContainer ) :
            m_default( aDefault ),
            m_showContainer( aShowContainer ),
            m_target( aDefault )
    {}

    virtual void AddCustomControls( wxFileDialogCustomize& customizer ) override
    {
        wxString padding;
#ifdef __WXMAC__
        padding = wxT( "                    " );
        customizer.AddStaticText( wxT( "\n\n" ) );
#endif

        // Radio buttons are only grouped if they are consecutive. If we
        // want padding, we need to add it to the first radio button.
        m_addGlobalTableEntry = customizer.AddRadioButton(
                _( "Add new library to global library table" ) + padding );

        if( m_showContainer )
        {
            m_addContainerTableEntry = customizer.AddRadioButton(
                    _( "Add to multi-board container "
                       "(replicates to all sub-boards)" ) );
        }

        m_addProjectTableEntry = customizer.AddRadioButton(
                _( "Add new library to project library table" ) );

        switch( m_default )
        {
        case LIBRARY_SAVE_TARGET::GLOBAL:
            m_addGlobalTableEntry->SetValue( true );
            break;

        case LIBRARY_SAVE_TARGET::CONTAINER:
            // Fall back to Project when caller passed CONTAINER but
            // the option isn't available — keeps callers from having
            // to pre-clamp.
            if( m_addContainerTableEntry )
                m_addContainerTableEntry->SetValue( true );
            else
                m_addProjectTableEntry->SetValue( true );
            break;

        case LIBRARY_SAVE_TARGET::PROJECT:
            m_addProjectTableEntry->SetValue( true );
            break;
        }
    }

    virtual void TransferDataFromCustomControls() override
    {
        if( m_addGlobalTableEntry && m_addGlobalTableEntry->GetValue() )
            m_target = LIBRARY_SAVE_TARGET::GLOBAL;
        else if( m_addContainerTableEntry && m_addContainerTableEntry->GetValue() )
            m_target = LIBRARY_SAVE_TARGET::CONTAINER;
        else
            m_target = LIBRARY_SAVE_TARGET::PROJECT;
    }

    LIBRARY_SAVE_TARGET GetSaveTarget() const { return m_target; }

    /// Backwards-compat shim for callers that still treat scope as a
    /// boolean Global/Project switch. Returns false for the Container
    /// case — callers that need to know about Container should use
    /// `GetSaveTarget()` directly.
    bool GetUseGlobalTable() const { return m_target == LIBRARY_SAVE_TARGET::GLOBAL; }

private:
    LIBRARY_SAVE_TARGET m_default;
    bool                m_showContainer;
    LIBRARY_SAVE_TARGET m_target;

    wxFileDialogRadioButton* m_addGlobalTableEntry = nullptr;
    wxFileDialogRadioButton* m_addContainerTableEntry = nullptr;
    wxFileDialogRadioButton* m_addProjectTableEntry = nullptr;

    wxDECLARE_NO_COPY_CLASS( FILEDLG_HOOK_NEW_LIBRARY );
};
