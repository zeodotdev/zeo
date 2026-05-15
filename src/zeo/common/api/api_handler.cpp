/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2023 Jon Evans <jon@craftyjon.com>
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

#include <api/api_handler.h>
#include <wx/wx.h>

using kiapi::common::ApiRequest, kiapi::common::ApiResponse, kiapi::common::ApiResponseStatus;


const wxString API_HANDLER::m_defaultCommitMessage = _( "Modification from API" );


API_RESULT API_HANDLER::Handle( ApiRequest& aMsg )
{
    ApiResponseStatus status;

    if( !aMsg.has_message() )
    {
        status.set_status( ApiStatusCode::AS_BAD_REQUEST );
        status.set_error_message( "request has no inner message" );
        return tl::unexpected( status );
    }

    std::string typeName;

    if( !google::protobuf::Any::ParseAnyTypeUrl( aMsg.message().type_url(), &typeName ) )
    {
        status.set_status( ApiStatusCode::AS_BAD_REQUEST );
        status.set_error_message( "could not parse inner message type" );
        return tl::unexpected( status );
    }

    auto it = m_handlers.find( typeName );

    if( it != m_handlers.end() )
    {
        REQUEST_HANDLER& handler = it->second;

        // Debug logging for TitleBlock/Origin commands
        bool isTitleBlockOrOrigin = typeName.find( "TitleBlock" ) != std::string::npos ||
                                    typeName.find( "BoardOrigin" ) != std::string::npos;
        if( isTitleBlockOrOrigin )
        {
            wxLogMessage( "API_HANDLER[%p]: Found handler for '%s', calling it...",
                          this, typeName );
        }

        API_RESULT result = handler( aMsg );

        if( isTitleBlockOrOrigin )
        {
            if( result.has_value() )
            {
                wxLogMessage( "API_HANDLER[%p]: Handler for '%s' succeeded",
                              this, typeName );
            }
            else
            {
                wxLogMessage( "API_HANDLER[%p]: Handler for '%s' returned error status=%d, msg='%s'",
                              this, typeName,
                              (int)result.error().status(),
                              result.error().error_message() );
            }
        }

        return result;
    }

    // Debug: log unhandled type names for title_block/origin related messages
    if( typeName.find( "TitleBlock" ) != std::string::npos ||
        typeName.find( "BoardOrigin" ) != std::string::npos )
    {
        wxLogMessage( "API_HANDLER[%p]: Unhandled type '%s', available handlers: %zu",
                      this, typeName, m_handlers.size() );
        for( const auto& [name, _] : m_handlers )
        {
            wxLogMessage( "  - registered: %s", name );
        }
        // Also check if we have ANY TitleBlock/Origin handlers
        bool hasTitleBlock = false, hasOrigin = false;
        for( const auto& [name, _] : m_handlers )
        {
            if( name.find( "TitleBlock" ) != std::string::npos )
                hasTitleBlock = true;
            if( name.find( "Origin" ) != std::string::npos )
                hasOrigin = true;
        }
        wxLogMessage( "  Has TitleBlock handlers: %s, Has Origin handlers: %s",
                      hasTitleBlock ? "YES" : "NO", hasOrigin ? "YES" : "NO" );
    }

    status.set_status( ApiStatusCode::AS_UNHANDLED );
    // This response is used internally; no need for an error message
    return tl::unexpected( status );
}
