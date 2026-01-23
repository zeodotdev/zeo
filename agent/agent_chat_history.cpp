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

#include "agent_chat_history.h"

#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/dir.h>
#include <wx/datetime.h>
#include <fstream>
#include <algorithm>

AGENT_CHAT_HISTORY::AGENT_CHAT_HISTORY()
{
}


void AGENT_CHAT_HISTORY::SetConversationId( const std::string& aId )
{
    m_conversationId = aId;
}


wxString AGENT_CHAT_HISTORY::GetHistoryDir()
{
    wxString appSupport = wxStandardPaths::Get().GetUserDataDir();
    // wxStandardPaths returns ~/Library/Application Support/{appname}
    // We want ~/Library/Application Support/kicad/agent_chats
    wxFileName dir( appSupport, wxEmptyString );
    dir.RemoveLastDir();  // Remove app-specific dir
    dir.AppendDir( "kicad" );
    dir.AppendDir( "agent_chats" );
    return dir.GetPath();
}


wxString AGENT_CHAT_HISTORY::GetFilePath()
{
    wxFileName fn( GetHistoryDir(), wxString::FromUTF8( m_conversationId ), "json" );
    return fn.GetFullPath();
}


void AGENT_CHAT_HISTORY::Save( const nlohmann::json& aChatHistory )
{
    if( m_conversationId.empty() )
        return;

    wxString dir = GetHistoryDir();
    
    // Create directory if it doesn't exist
    if( !wxFileName::DirExists( dir ) )
        wxFileName::Mkdir( dir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL );

    wxString path = GetFilePath();
    std::ofstream file( path.ToStdString() );
    
    if( file.is_open() )
    {
        file << aChatHistory.dump( 2 );
        file.close();
    }
}


nlohmann::json AGENT_CHAT_HISTORY::Load( const std::string& aConversationId )
{
    m_conversationId = aConversationId;
    wxString path = GetFilePath();

    if( !wxFileName::FileExists( path ) )
        return nlohmann::json::array();

    std::ifstream file( path.ToStdString() );
    
    if( file.is_open() )
    {
        try
        {
            nlohmann::json history;
            file >> history;
            return history;
        }
        catch( ... )
        {
            return nlohmann::json::array();
        }
    }

    return nlohmann::json::array();
}

std::vector<AGENT_CHAT_HISTORY::HistoryEntry> AGENT_CHAT_HISTORY::GetHistoryList()
{
    std::vector<HistoryEntry> list;
    wxString dirPath = GetHistoryDir();

    if( !wxFileName::DirExists( dirPath ) )
        return list;

    wxDir dir( dirPath );
    if( !dir.IsOpened() )
        return list;

    wxString filename;
    bool cont = dir.GetFirst( &filename, "*.json", wxDIR_FILES );
    while( cont )
    {
        // Filename format: YYYY-MM-DD_HH-MM-SS.json
        // ID is the filename without extension
        wxFileName fn( dirPath, filename );
        std::string id = fn.GetName().ToStdString();
        
        // Manual formatting: YYYY-MM-DD_HH-MM-SS -> YYYY-MM-DD HH:MM
        std::string display = id;
        if( id.length() >= 19 ) 
        {
            display = id.substr( 0, 10 ) + " " + id.substr( 11, 2 ) + ":" + id.substr( 14, 2 );
        }

        list.push_back( { id, display } );
        cont = dir.GetNext( &filename );
    }

    // Sort descending (newest first)
    std::sort( list.begin(), list.end(), []( const HistoryEntry& a, const HistoryEntry& b ) {
        return a.id > b.id;
    } );

    return list;
}


void AGENT_CHAT_HISTORY::StartNewConversation()
{
    wxDateTime now = wxDateTime::Now();
    m_conversationId = now.Format( "%Y-%m-%d_%H-%M-%S" ).ToStdString();
}
