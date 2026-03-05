/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2024 Jon Evans <jon@craftyjon.com>
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

#ifndef KICAD_API_SERVER_H
#define KICAD_API_SERVER_H

#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <queue>
#include <map>
#include <mutex>
#include <atomic>
#include <condition_variable>

#include <wx/event.h>
#include <wx/filename.h>

#include <kicommon.h>

class API_HANDLER;
class KINNG_REQUEST_SERVER;
class wxEvtHandler;


/**
 * Structure to track a pending API request and its reply.
 * Each request gets a unique ID to avoid race conditions when multiple
 * NNG threads are waiting for replies simultaneously.
 */
struct PENDING_REQUEST
{
    uint64_t              id;             ///< Unique request ID
    std::string           request;        ///< The serialized request data
    std::string           reply;          ///< The serialized reply (filled by main thread)
    bool                  replyReady;     ///< True when reply is ready to be sent
    KINNG_REQUEST_SERVER* server;         ///< The server that should send the reply

    PENDING_REQUEST() : id( 0 ), replyReady( false ), server( nullptr ) {}
    PENDING_REQUEST( uint64_t aId, const std::string& aRequest, KINNG_REQUEST_SERVER* aServer = nullptr )
        : id( aId ), request( aRequest ), replyReady( false ), server( aServer ) {}
};


wxDECLARE_EVENT( API_REQUEST_EVENT, wxCommandEvent );


class KICOMMON_API KICAD_API_SERVER : public wxEvtHandler
{
public:
    KICAD_API_SERVER();

    ~KICAD_API_SERVER();

    void Start();

    void Stop();

    bool Running() const;

    /**
     * Adds a new request handler to the server.  Each handler maintains its own list of API
     * messages that it knows how to handle, and the server will pass every incoming message to all
     * handlers in succession until one of them handles it.
     *
     * The caller is responsible for the lifetime of the handler and must call DeregisterHandler
     * before the pointer is freed.
     *
     * @param aHandler is a pointer (non-owned) to API_HANDLER
     */
    void RegisterHandler( API_HANDLER* aHandler );

    void DeregisterHandler( API_HANDLER* aHandler );

    void SetReadyToReply( bool aReady = true ) { m_readyToReply = aReady; }

    std::string SocketPath() const;

    /**
     * Return the socket path for the exec (long-running tool) server.
     * Derived from the primary socket path by replacing .sock with -exec.sock.
     */
    std::string ExecSocketPath() const;

    const std::string& Token() const { return m_token; }

private:

    /**
     * Callback that executes on the server thread and queues the request for processing
     * during idle time. This avoids processing requests during ProcessPendingEvents()
     * which can cause issues with nested event processing on macOS.
     *
     * @param aRequest is a pointer to a string containing bytes that came in over the wire
     * @param aServer is the KINNG_REQUEST_SERVER that received the request (for Reply routing)
     */
    void onApiRequest( std::string* aRequest, KINNG_REQUEST_SERVER* aServer );

    /**
     * Idle event handler that processes queued API requests when the event loop is truly idle.
     * This ensures API requests are not processed during ProcessPendingEvents() calls.
     */
    void onIdle( wxIdleEvent& aEvent );

    /**
     * Process a single API request. Called from onIdle when a request is available.
     * @param aRequestId the unique ID of this request
     * @param aRequest the serialized request string
     */
    void processApiRequest( uint64_t aRequestId, const std::string& aRequest );

    void log( const std::string& aOutput );

    std::unique_ptr<KINNG_REQUEST_SERVER> m_server;
    std::unique_ptr<KINNG_REQUEST_SERVER> m_execServer;  ///< Second socket for long-running tool execution

    std::set<API_HANDLER*> m_handlers;

    std::string m_token;

    bool m_readyToReply;

    static wxString s_logFileName;

    wxFileName m_logFilePath;

    // Thread-safe queue for pending API requests processed during idle time
    // This avoids processing during ProcessPendingEvents() which causes issues on macOS
    // Each request has a unique ID to ensure replies go to the correct waiting thread
    std::queue<uint64_t> m_requestQueue;              ///< Queue of request IDs to process
    std::map<uint64_t, PENDING_REQUEST> m_pendingRequests;  ///< Map of ID -> pending request
    std::mutex m_queueMutex;                          ///< Protects queue and pending map
    std::atomic<bool> m_hasQueuedRequests;
    std::atomic<uint64_t> m_nextRequestId;            ///< Counter for unique request IDs

    // Condition variable for signaling when any reply is ready
    // Each waiting thread checks its specific request's replyReady flag
    std::condition_variable m_replyReady;
    std::mutex m_replyMutex;
};

#endif //KICAD_API_SERVER_H
