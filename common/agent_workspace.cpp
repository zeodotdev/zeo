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
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#include <agent_workspace.h>
#include <kiway.h>
#include <mail_type.h>
#include <nlohmann/json.hpp>
#include <wx/log.h>


AGENT_WORKSPACE::AGENT_WORKSPACE() :
        m_kiway( nullptr ),
        m_transactionActive( false )
{
}


AGENT_WORKSPACE::~AGENT_WORKSPACE()
{
    if( m_transactionActive )
    {
        RejectChanges();
    }
}


void AGENT_WORKSPACE::SetTargetSheet( const KIID& aSheetId )
{
    m_targetSheet = aSheetId;
    m_agentCommit.SetTargetSheet( aSheetId );

    // Notify editors of the new target sheet
    if( m_kiway )
    {
        nlohmann::json j;
        j["sheet_uuid"] = m_targetSheet.AsStdString();
        std::string payload = j.dump();

        m_kiway->ExpressMail( FRAME_SCH, MAIL_AGENT_TARGET_SHEET, payload );
    }
}


bool AGENT_WORKSPACE::IsTargetingDifferentSheet( const KIID& aCurrentUserSheet ) const
{
    return m_targetSheet != aCurrentUserSheet && m_targetSheet != NilUuid();
}


bool AGENT_WORKSPACE::BeginTransaction()
{
    if( m_transactionActive )
    {
        wxLogWarning( "AGENT_WORKSPACE: Transaction already active" );
        return false;
    }

    m_transactionActive = true;
    m_agentCommit.BeginTransaction();

    notifyTransactionStateChanged();
    notifyWorkingSetChanged();

    return true;
}


bool AGENT_WORKSPACE::EndTransaction( bool aCommit )
{
    if( !m_transactionActive )
    {
        wxLogWarning( "AGENT_WORKSPACE: No active transaction to end" );
        return false;
    }

    if( !aCommit )
    {
        // Discard all changes
        m_agentCommit.RevertAll();
        m_transactionActive = false;
        notifyTransactionStateChanged();
        return true;
    }

    // Keep transaction active until user approves
    // Changes are staged in m_agentCommit
    notifyWorkingSetChanged();

    return true;
}


void AGENT_WORKSPACE::RecordUserEdit( const KIID& aItemId, const wxString& aPropertyName,
                                       const wxString& aOldValue, const wxString& aNewValue )
{
    if( !m_transactionActive )
    {
        return;  // No need to track if no agent transaction is active
    }

    m_agentCommit.RecordUserModification( aItemId, aPropertyName, aOldValue, aNewValue );

    // Check for conflict
    CONFLICT_INFO conflict = m_agentCommit.CheckConflict( aItemId );

    if( conflict.m_type != CONFLICT_TYPE::NONE )
    {
        NotifyConflict( aItemId, conflict );
    }
}


CONFLICT_INFO AGENT_WORKSPACE::HasConflictWith( const KIID& aItemId ) const
{
    return m_agentCommit.CheckConflict( aItemId );
}


std::vector<CONFLICT_INFO> AGENT_WORKSPACE::GetConflicts() const
{
    return m_agentCommit.GetConflicts();
}


void AGENT_WORKSPACE::ResolveConflict( const KIID& aItemId, CONFLICT_RESOLUTION aResolution )
{
    m_agentCommit.ResolveConflict( aItemId, aResolution );

    // Notify about resolution
    if( m_kiway )
    {
        nlohmann::json j;
        j["item_uuid"] = aItemId.AsStdString();

        switch( aResolution )
        {
        case CONFLICT_RESOLUTION::KEEP_USER:
            j["resolution"] = "keep_user";
            break;
        case CONFLICT_RESOLUTION::KEEP_AGENT:
            j["resolution"] = "keep_agent";
            break;
        case CONFLICT_RESOLUTION::AUTO_MERGE:
            j["resolution"] = "merge";
            break;
        case CONFLICT_RESOLUTION::MANUAL:
            j["resolution"] = "manual";
            break;
        }

        std::string payload = j.dump();
        m_kiway->ExpressMail( FRAME_SCH, MAIL_CONFLICT_RESOLVED, payload );
    }
}


bool AGENT_WORKSPACE::AllConflictsResolved() const
{
    return m_agentCommit.AllConflictsResolved();
}


bool AGENT_WORKSPACE::ApproveChanges( COMMIT& aCommit, const wxString& aMessage, int aFlags )
{
    if( !m_agentCommit.HasChanges() )
    {
        wxLogWarning( "AGENT_WORKSPACE: No changes to approve" );
        return false;
    }

    if( !AllConflictsResolved() )
    {
        wxLogWarning( "AGENT_WORKSPACE: Unresolved conflicts exist" );
        return false;
    }

    // Apply all staged changes through the provided commit
    m_agentCommit.ApplyToCommit( aCommit, aMessage, aFlags );

    // End the transaction
    m_transactionActive = false;
    m_agentCommit.Clear();

    notifyTransactionStateChanged();

    return true;
}


bool AGENT_WORKSPACE::RejectChanges()
{
    if( !m_transactionActive && !m_agentCommit.HasChanges() )
    {
        return true;  // Nothing to reject
    }

    m_agentCommit.RevertAll();
    m_transactionActive = false;

    notifyTransactionStateChanged();

    return true;
}


void AGENT_WORKSPACE::Reset()
{
    if( m_transactionActive )
    {
        RejectChanges();
    }

    m_targetSheet = NilUuid();
    m_agentCommit.Clear();
    m_transactionActive = false;
}


void AGENT_WORKSPACE::NotifyConflict( const KIID& aItemId, const CONFLICT_INFO& aInfo )
{
    // Call the registered callback
    if( m_conflictCallback )
    {
        m_conflictCallback( aItemId, aInfo );
    }

    // Send mail notification to the agent frame
    if( m_kiway )
    {
        nlohmann::json j;
        j["item_uuid"] = aItemId.AsStdString();
        j["property"] = aInfo.m_propertyName.ToStdString();
        j["user_value"] = aInfo.m_userValue.ToStdString();
        j["agent_value"] = aInfo.m_agentValue.ToStdString();
        j["can_auto_merge"] = aInfo.m_canAutoMerge;

        switch( aInfo.m_type )
        {
        case CONFLICT_TYPE::SAME_ITEM:
            j["conflict_type"] = "same_item";
            break;
        case CONFLICT_TYPE::SAME_PROPERTY:
            j["conflict_type"] = "same_property";
            break;
        case CONFLICT_TYPE::SPATIAL_OVERLAP:
            j["conflict_type"] = "spatial_overlap";
            break;
        case CONFLICT_TYPE::CONNECTION:
            j["conflict_type"] = "connection";
            break;
        default:
            j["conflict_type"] = "none";
            break;
        }

        std::string payload = j.dump();
        m_kiway->ExpressMail( FRAME_AGENT, MAIL_CONFLICT_DETECTED, payload );
    }
}


void AGENT_WORKSPACE::notifyWorkingSetChanged()
{
    if( !m_kiway )
        return;

    const auto& workingSet = m_agentCommit.GetModifiedByAgent();

    nlohmann::json j;
    j["items"] = nlohmann::json::array();

    for( const auto& itemId : workingSet )
    {
        j["items"].push_back( itemId.AsStdString() );
    }

    std::string payload = j.dump();
    m_kiway->ExpressMail( FRAME_SCH, MAIL_AGENT_WORKING_SET, payload );
    m_kiway->ExpressMail( FRAME_PCB_EDITOR, MAIL_AGENT_WORKING_SET, payload );
}


void AGENT_WORKSPACE::notifyTransactionStateChanged()
{
    if( m_transactionCallback )
    {
        m_transactionCallback( m_transactionActive, m_targetSheet );
    }

    if( !m_kiway )
        return;

    nlohmann::json j;
    j["active"] = m_transactionActive;
    j["sheet_uuid"] = m_targetSheet.AsStdString();
    std::string payload = j.dump();

    if( m_transactionActive )
    {
        m_kiway->ExpressMail( FRAME_SCH, MAIL_AGENT_BEGIN_TRANSACTION, payload );
        m_kiway->ExpressMail( FRAME_PCB_EDITOR, MAIL_AGENT_BEGIN_TRANSACTION, payload );
    }
    else
    {
        m_kiway->ExpressMail( FRAME_SCH, MAIL_AGENT_END_TRANSACTION, payload );
        m_kiway->ExpressMail( FRAME_PCB_EDITOR, MAIL_AGENT_END_TRANSACTION, payload );
    }
}
