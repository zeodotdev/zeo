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
#include <random>
#include <sstream>
#include <iomanip>

AGENT_CHAT_HISTORY::AGENT_CHAT_HISTORY()
{
}


void AGENT_CHAT_HISTORY::SetTitle( const std::string& aTitle )
{
    m_title = aTitle;
}


std::string AGENT_CHAT_HISTORY::GenerateUUID()
{
    // Generate a simple UUID-like string: timestamp + random hex
    std::random_device rd;
    std::mt19937 gen( rd() );
    std::uniform_int_distribution<> dis( 0, 15 );

    const char* hexChars = "0123456789abcdef";
    std::string uuid;

    // Format: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
    for( int i = 0; i < 32; i++ )
    {
        if( i == 8 || i == 12 || i == 16 || i == 20 )
            uuid += '-';
        uuid += hexChars[dis( gen )];
    }

    return uuid;
}


std::string AGENT_CHAT_HISTORY::GetCurrentTimestamp()
{
    wxDateTime now = wxDateTime::Now();
    return now.FormatISOCombined().ToStdString();
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


/**
 * Strip large base64 data from chat history before persisting to disk.
 * Replaces base64 data with "__stripped__" placeholder to prevent multi-MB history files.
 * Attachments are only needed in-memory for the current session's API context.
 */
static nlohmann::json StripBase64FromHistory( const nlohmann::json& aHistory )
{
    nlohmann::json stripped = aHistory;

    for( auto& msg : stripped )
    {
        if( !msg.contains( "content" ) || !msg["content"].is_array() )
            continue;

        for( auto& block : msg["content"] )
        {
            std::string blockType = block.value( "type", "" );

            // Handle top-level image/document blocks (user-attached files)
            if( ( blockType == "image" || blockType == "document" )
                && block.contains( "source" ) && block["source"].contains( "data" ) )
            {
                block["source"]["data"] = "__stripped__";
            }

            // Handle tool_result blocks that may contain image/document content arrays
            if( blockType == "tool_result"
                && block.contains( "content" ) && block["content"].is_array() )
            {
                for( auto& inner : block["content"] )
                {
                    std::string innerType = inner.value( "type", "" );

                    if( ( innerType == "image" || innerType == "document" )
                        && inner.contains( "source" ) && inner["source"].contains( "data" ) )
                    {
                        inner["source"]["data"] = "__stripped__";
                    }
                }
            }
        }
    }

    return stripped;
}


void AGENT_CHAT_HISTORY::Save( const nlohmann::json& aChatHistory )
{
    if( m_conversationId.empty() )
        return;

    wxString dir = GetHistoryDir();

    // Create directory if it doesn't exist
    if( !wxFileName::DirExists( dir ) )
        wxFileName::Mkdir( dir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL );

    // Update last_updated timestamp
    m_lastUpdated = GetCurrentTimestamp();

    // Strip base64 image data before saving to avoid huge history files
    nlohmann::json persistHistory = StripBase64FromHistory( aChatHistory );

    // Create metadata wrapper
    nlohmann::json wrapper;
    wrapper["id"] = m_conversationId;
    wrapper["title"] = m_title;
    wrapper["created_at"] = m_createdAt;
    wrapper["last_updated"] = m_lastUpdated;
    wrapper["messages"] = persistHistory;

    wxString path = GetFilePath();
    std::ofstream file( path.ToStdString() );

    if( file.is_open() )
    {
        file << wrapper.dump( 2 );
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
            nlohmann::json data;
            file >> data;

            // Check if new format (object with messages) or legacy format (array)
            if( data.is_object() && data.contains( "messages" ) )
            {
                // New format - extract metadata
                m_title = data.value( "title", "" );
                m_createdAt = data.value( "created_at", "" );
                m_lastUpdated = data.value( "last_updated", "" );
                return data["messages"];
            }
            else if( data.is_array() )
            {
                // Legacy format - array of messages, no metadata
                m_title = "";
                m_createdAt = "";
                m_lastUpdated = "";
                return data;
            }
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
        wxFileName fn( dirPath, filename );
        std::string id = fn.GetName().ToStdString();
        wxString fullPath = fn.GetFullPath();

        HistoryEntry entry;
        entry.id = id;

        // Try to read metadata from file
        std::ifstream file( fullPath.ToStdString() );
        if( file.is_open() )
        {
            try
            {
                nlohmann::json data;
                file >> data;

                if( data.is_object() && data.contains( "title" ) )
                {
                    // New format with metadata
                    entry.title = data.value( "title", "" );
                    entry.createdAt = data.value( "created_at", "" );
                    entry.lastUpdated = data.value( "last_updated", "" );
                }
                else
                {
                    // Legacy format - use ID as fallback title
                    // Try to format timestamp ID nicely
                    if( id.length() >= 19 && id[4] == '-' && id[7] == '-' )
                    {
                        entry.title = id.substr( 0, 10 ) + " " + id.substr( 11, 2 ) + ":" + id.substr( 14, 2 );
                    }
                    else
                    {
                        entry.title = id;
                    }
                    entry.createdAt = id;
                    entry.lastUpdated = id;
                }
            }
            catch( ... )
            {
                entry.title = id;
                entry.createdAt = id;
                entry.lastUpdated = id;
            }
            file.close();
        }

        // Use title or fallback to formatted ID
        if( entry.title.empty() )
        {
            entry.title = "Untitled Chat";
        }

        list.push_back( entry );
        cont = dir.GetNext( &filename );
    }

    // Sort by lastUpdated descending (newest first)
    std::sort( list.begin(), list.end(), []( const HistoryEntry& a, const HistoryEntry& b ) {
        return a.lastUpdated > b.lastUpdated;
    } );

    return list;
}


void AGENT_CHAT_HISTORY::StartNewConversation()
{
    m_conversationId = GenerateUUID();
    m_title = "";
    m_createdAt = GetCurrentTimestamp();
    m_lastUpdated = m_createdAt;
}
