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

#include <fmt/format.h>
#include <wx/app.h>
#include <wx/datetime.h>
#include <wx/event.h>
#include <wx/stdpaths.h>

#include <advanced_config.h>
#include <api/api_handler.h>
#include <api/api_utils.h> // traceApi
#include <api/api_server.h>
#include <kiid.h>
#include <kinng.h>
#include <paths.h>
#include <pgm_base.h>
#include <settings/common_settings.h>
#include <string_utils.h>

#include <api/common/envelope.pb.h>

#ifdef __UNIX__
#include <sys/file.h>
#endif

using kiapi::common::ApiRequest, kiapi::common::ApiResponse, kiapi::common::ApiStatusCode;


wxString KICAD_API_SERVER::s_logFileName = "api.log";


wxDEFINE_EVENT( API_REQUEST_EVENT, wxCommandEvent );


KICAD_API_SERVER::KICAD_API_SERVER() :
        wxEvtHandler(),
        m_token( KIID().AsStdString() ),
        m_readyToReply( false ),
        m_hasQueuedRequests( false ),
        m_nextRequestId( 1 )
{
    if( !Pgm().GetCommonSettings()->m_Api.enable_server )
    {
        wxLogTrace( traceApi, "Server: disabled by user preferences." );
        return;
    }

    Start();
}


KICAD_API_SERVER::~KICAD_API_SERVER()
{
    Stop();
}


void KICAD_API_SERVER::Start()
{
    if( Running() )
        return;

    wxFileName socket;
#ifdef __WXMAC__
    socket.AssignDir( wxS( "/tmp" ) );
#else
    socket.AssignDir( wxStandardPaths::Get().GetTempDir() );
#endif
    socket.AppendDir( wxS( "kicad" ) );
    socket.SetFullName( wxS( "api.sock" ) );

    if( !PATHS::EnsurePathExists( socket.GetPath() ) )
    {
        wxLogTrace( traceApi, wxString::Format( "Server: socket path %s could not be created", socket.GetPath() ) );
        return;
    }

#ifndef __WINDOWS__
    // We use non-abstract sockets because macOS and some other non-Linux platforms don't support
    // abstract sockets, which means there might be an old socket to unlink.  In order to try to
    // recover this, we lock a file (which will be unlocked on process exit) and if we get the lock,
    // we know the old socket is orphaned and can be removed.
    wxFileName lockFilePath( socket.GetPath(), wxS( "api.lock" ) );

    int lockFile = open( lockFilePath.GetFullPath().c_str(), O_RDONLY | O_CREAT, 0600 );

    if( lockFile >= 0 && flock( lockFile, LOCK_EX | LOCK_NB ) == 0 )
    {
        if( socket.Exists() )
        {
            wxLogTrace( traceApi,
                        wxString::Format( "Server: cleaning up stale socket path %s", socket.GetFullPath() ) );
            wxRemoveFile( socket.GetFullPath() );
        }
    }
#endif

    if( socket.Exists() )
    {
        socket.SetFullName( wxString::Format( wxS( "api-%lu.sock" ), ::wxGetProcessId() ) );

        if( socket.Exists() )
        {
            wxLogTrace( traceApi,
                        wxString::Format( "Server: PID socket path %s already exists!", socket.GetFullPath() ) );
            return;
        }
    }

    m_server = std::make_unique<KINNG_REQUEST_SERVER>( fmt::format( "ipc://{}", socket.GetFullPath().ToStdString() ) );
    m_server->SetCallback(
            [&]( std::string* aRequest )
            {
                onApiRequest( aRequest );
            } );

    m_logFilePath.AssignDir( PATHS::GetLogsPath() );
    m_logFilePath.SetName( s_logFileName );

    if( ADVANCED_CFG::GetCfg().m_EnableAPILogging )
    {
        PATHS::EnsurePathExists( PATHS::GetLogsPath() );
        log( fmt::format( "--- KiCad API server started at {} ---\n", SocketPath() ) );
    }

    wxLogTrace( traceApi, wxString::Format( "Server: listening at %s", SocketPath() ) );

    // Bind idle handler to wxTheApp for processing API requests during true idle time
    // This avoids processing during ProcessPendingEvents() which causes issues on macOS
    // We bind to wxTheApp because idle events are more reliably delivered to the app
    if( wxTheApp )
        wxTheApp->Bind( wxEVT_IDLE, &KICAD_API_SERVER::onIdle, this );
}


void KICAD_API_SERVER::Stop()
{
    if( !Running() )
        return;

    wxLogTrace( traceApi, "Stopping server" );
    if( wxTheApp )
        wxTheApp->Unbind( wxEVT_IDLE, &KICAD_API_SERVER::onIdle, this );

    // Wake up any waiting NNG threads by marking all pending requests as ready
    {
        std::lock_guard<std::mutex> queueLock( m_queueMutex );
        for( auto& [id, pending] : m_pendingRequests )
            pending.replyReady = true;
    }
    m_replyReady.notify_all();

    m_server->Stop();
    m_server.reset( nullptr );
}


bool KICAD_API_SERVER::Running() const
{
    return m_server && m_server->Running();
}


void KICAD_API_SERVER::RegisterHandler( API_HANDLER* aHandler )
{
    wxCHECK( aHandler, /* void */ );
    m_handlers.insert( aHandler );
    wxLogTrace( traceApi, "API_SERVER[%p]: Registered handler %p, total handlers: %zu", this, aHandler, m_handlers.size() );
}


void KICAD_API_SERVER::DeregisterHandler( API_HANDLER* aHandler )
{
    m_handlers.erase( aHandler );
}


std::string KICAD_API_SERVER::SocketPath() const
{
    return m_server ? m_server->SocketPath() : "";
}


void KICAD_API_SERVER::onApiRequest( std::string* aRequest )
{
    if( !m_readyToReply )
    {
        ApiResponse notHandled;
        notHandled.mutable_status()->set_status( ApiStatusCode::AS_NOT_READY );
        notHandled.mutable_status()->set_error_message( "KiCad is not ready to reply" );
        m_server->Reply( notHandled.SerializeAsString() );
        log( "Got incoming request but was not yet ready to reply." );
        // Note: Do NOT delete aRequest - it's managed by KINNG internally
        return;
    }

    // Assign a unique ID to this request for tracking
    uint64_t requestId = m_nextRequestId.fetch_add( 1 );

    // Queue the request for processing during idle time
    // This avoids processing during ProcessPendingEvents() which causes issues on macOS
    {
        std::lock_guard<std::mutex> queueLock( m_queueMutex );
        m_pendingRequests[requestId] = PENDING_REQUEST( requestId, *aRequest );
        m_requestQueue.push( requestId );
        m_hasQueuedRequests.store( true );
    }

    // Note: Do NOT delete aRequest - it's managed by KINNG internally

    // Wake the main thread's event loop to process the request during idle time
    wxWakeUpIdle();

    // Wait for THIS request's reply to be ready (each request waits for its own reply)
    std::string replyString;
    {
        std::unique_lock<std::mutex> lock( m_replyMutex );

        // Wait until our specific request has its reply ready
        m_replyReady.wait( lock, [this, requestId]()
        {
            std::lock_guard<std::mutex> queueLock( m_queueMutex );
            auto it = m_pendingRequests.find( requestId );
            return it == m_pendingRequests.end() || it->second.replyReady;
        } );

        // Retrieve and remove our reply
        {
            std::lock_guard<std::mutex> queueLock( m_queueMutex );
            auto it = m_pendingRequests.find( requestId );
            if( it != m_pendingRequests.end() )
            {
                replyString = std::move( it->second.reply );
                m_pendingRequests.erase( it );
            }
        }
    }

    // Send the reply back to the client
    m_server->Reply( replyString );
}


void KICAD_API_SERVER::onIdle( wxIdleEvent& aEvent )
{
    // Allow other idle handlers to run
    aEvent.Skip();

    // Check if we have any queued requests to process
    if( !m_hasQueuedRequests.load() )
        return;

    uint64_t requestId = 0;
    std::string requestString;

    // Get one request from the queue
    {
        std::lock_guard<std::mutex> lock( m_queueMutex );
        if( m_requestQueue.empty() )
        {
            m_hasQueuedRequests.store( false );
            return;
        }

        requestId = m_requestQueue.front();
        m_requestQueue.pop();

        auto it = m_pendingRequests.find( requestId );
        if( it == m_pendingRequests.end() )
        {
            // Request was somehow removed, skip it
            if( m_requestQueue.empty() )
                m_hasQueuedRequests.store( false );
            return;
        }

        requestString = it->second.request;

        if( m_requestQueue.empty() )
            m_hasQueuedRequests.store( false );
    }


    // Process the request and store the reply
    processApiRequest( requestId, requestString );
}


void KICAD_API_SERVER::processApiRequest( uint64_t aRequestId, const std::string& aRequestString )
{
    ApiRequest request;
    std::string replyString;

    if( !request.ParseFromString( aRequestString ) )
    {
        ApiResponse error;
        error.mutable_header()->set_kicad_token( m_token );
        error.mutable_status()->set_status( ApiStatusCode::AS_BAD_REQUEST );
        error.mutable_status()->set_error_message( "request could not be parsed" );
        replyString = error.SerializeAsString();

        if( ADVANCED_CFG::GetCfg().m_EnableAPILogging )
            log( "Response (ERROR): " + error.Utf8DebugString() );
    }
    else
    {
        if( ADVANCED_CFG::GetCfg().m_EnableAPILogging )
            log( "Request: " + request.Utf8DebugString() );

        if( !request.header().kicad_token().empty() && request.header().kicad_token().compare( m_token ) != 0 )
        {
            ApiResponse error;
            error.mutable_header()->set_kicad_token( m_token );
            error.mutable_status()->set_status( ApiStatusCode::AS_TOKEN_MISMATCH );
            error.mutable_status()->set_error_message(
                    "the provided kicad_token did not match this KiCad instance's token" );
            replyString = error.SerializeAsString();

            if( ADVANCED_CFG::GetCfg().m_EnableAPILogging )
                log( "Response (ERROR): " + error.Utf8DebugString() );
        }
        else
        {
            API_RESULT result;

            wxLogTrace( traceApi, "API_SERVER[%p]: Processing request %lu, checking %zu handlers",
                        this, aRequestId, m_handlers.size() );

            for( API_HANDLER* handler : m_handlers )
            {
                result = handler->Handle( request );

                if( result.has_value() )
                    break;
                else if( result.error().status() != ApiStatusCode::AS_UNHANDLED )
                    break;
            }

            if( result.has_value() )
            {
                result->mutable_header()->set_kicad_token( m_token );
                replyString = result->SerializeAsString();

                if( ADVANCED_CFG::GetCfg().m_EnableAPILogging )
                    log( "Response: " + result->Utf8DebugString() );
            }
            else
            {
                ApiResponse error;
                error.mutable_status()->CopyFrom( result.error() );
                error.mutable_header()->set_kicad_token( m_token );

                if( result.error().status() == ApiStatusCode::AS_UNHANDLED )
                {
                    std::string type = "<unparseable Any>";
                    google::protobuf::Any::ParseAnyTypeUrl( request.message().type_url(), &type );
                    std::string msg = fmt::format( "no handler available for request of type {}", type );
                    error.mutable_status()->set_error_message( msg );
                }

                replyString = error.SerializeAsString();

                if( ADVANCED_CFG::GetCfg().m_EnableAPILogging )
                    log( "Response (ERROR): " + error.Utf8DebugString() );
            }
        }
    }

    // Store the reply in this request's pending entry and mark it ready
    {
        std::lock_guard<std::mutex> queueLock( m_queueMutex );
        auto it = m_pendingRequests.find( aRequestId );
        if( it != m_pendingRequests.end() )
        {
            it->second.reply = std::move( replyString );
            it->second.replyReady = true;
        }
    }

    // Wake all waiting threads - each will check if its specific reply is ready
    m_replyReady.notify_all();
}


void KICAD_API_SERVER::log( const std::string& aOutput )
{
    FILE* fp = wxFopen( m_logFilePath.GetFullPath(), wxT( "a" ) );

    if( !fp )
        return;

    wxString   out;
    wxDateTime now = wxDateTime::Now();

    fprintf( fp, "%s", TO_UTF8( out.Format( wxS( "%s: %s" ), now.FormatISOCombined(), aOutput ) ) );
    fclose( fp );
}
