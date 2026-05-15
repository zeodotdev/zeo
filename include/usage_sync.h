/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.TXT for contributors.
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

#pragma once

#include <kicommon.h>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

/**
 * Lightweight, privacy-respecting usage telemetry.
 *
 * Singleton parallel to APP_MONITOR::SENTRY. Records DAU/WAU and a curated
 * set of feature-engagement events to a Supabase Postgres table.
 *
 * Identity:
 *   - Signed-in users: user_key = email (set via SetUser)
 *   - Anonymous:       user_key = "anon:<uid>" (uid persisted to disk,
 *                      independent of Sentry's opt-in state)
 *
 * Delivery is fire-and-forget. TrackEvent() appends a line to a local
 * JSONL buffer under the user cache directory; a background worker
 * flushes batches to Supabase. Network failures retry from the buffer.
 */
class KICOMMON_API USAGE_SYNC
{
public:
    static USAGE_SYNC* Instance();

    /// Point USAGE_SYNC at a Supabase project. Until this is called, events
    /// are appended to the local buffer but no upload is attempted. Safe to
    /// call multiple times.
    void Configure( const std::string& aSupabaseUrl, const std::string& aAnonKey );

    /// Load Supabase project URL + anon key from the same config file
    /// AGENT_AUTH uses, and call Configure() on success. No-op if the file
    /// is missing or malformed. Lets us configure telemetry at PGM_BASE
    /// init time so anonymous (non-logged-in) sessions still upload.
    void AutoConfigure();

    /// Switch identity to a signed-in user. Pass an empty string to revert
    /// to the anonymous UID. Affects subsequent events only.
    void SetUser( const std::string& aEmail );

    /// Fire one feature-engagement event.
    /// @param aName        dotted event name, e.g. "pcb.save"
    /// @param aArea        coarse app area: "eeschema" | "pcbnew" | "agent" | "mbsch" | "kicad"
    /// @param aProperties  small JSON object (durations, counts). Pass nullptr for none.
    void TrackEvent( const std::string& aName,
                     const std::string& aArea,
                     const nlohmann::json* aProperties = nullptr );

    /// Emit a session_start event. Idempotent within a single session.
    void SessionStart();

    /// Emit a session_end event and block briefly to flush the buffer.
    void SessionEnd();

    /// Master toggle. Persists to ~/.cache/Zeo/usage-opt-out (file present == disabled).
    /// Default: enabled.
    void    SetEnabled( bool aEnabled );
    bool    IsEnabled() const;

    /// Join workers and stop. Called from PGM_BASE::Destroy().
    void Cleanup();

    USAGE_SYNC( const USAGE_SYNC& ) = delete;
    USAGE_SYNC& operator=( const USAGE_SYNC& ) = delete;

private:
    USAGE_SYNC();
    ~USAGE_SYNC();

    void                   spawnWorker( std::function<void()> aWork );
    std::string            readOrCreateAnonUid();
    std::string            getUserKey();    // resolves email or "anon:<uid>"
    bool                   getIsAnonymous();
    std::string            getBufferPath();
    std::string            getOptOutPath();
    std::string            getAnonUidPath();

    /// Append one event row to the local JSONL buffer, then trigger an async flush.
    void                   recordEvent( const nlohmann::json& aRow );

    /// Pull pending rows from the JSONL buffer, POST them in batches.
    /// Removes successfully uploaded rows; leaves the rest for the next flush.
    void                   flushBlocking();

    /// Build a row payload with all the common fields populated.
    nlohmann::json         buildRow( const std::string& aType,
                                     const std::string& aName,
                                     const std::string& aArea,
                                     const nlohmann::json* aProperties );

    std::string            m_supabaseUrl;
    std::string            m_anonKey;
    std::mutex             m_configMutex;
    std::atomic<bool>      m_configured{ false };
    std::atomic<bool>      m_enabled{ true };
    std::atomic<bool>      m_sessionStarted{ false };

    std::string            m_userEmail;     // empty == anonymous
    std::mutex             m_userMutex;
    std::string            m_anonUid;
    std::string            m_sessionId;
    std::string            m_appVersion;
    std::string            m_os;
    std::string            m_platform;

    std::mutex             m_bufferMutex;
    std::atomic<bool>      m_flushInFlight{ false };

    struct WorkerEntry
    {
        std::thread                       thread;
        std::shared_ptr<std::atomic<bool>> done;
    };

    std::vector<WorkerEntry> m_workers;
    std::mutex               m_workersMutex;
    std::atomic<bool>        m_stopping{ false };
};
