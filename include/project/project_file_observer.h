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

#ifndef KICAD_PROJECT_FILE_OBSERVER_H
#define KICAD_PROJECT_FILE_OBSERVER_H

#include <kicommon.h>


class PROJECT_FILE;


/**
 * Identifies which `multi_board.*` field on a `PROJECT_FILE` changed.
 *
 * Used as the payload for change notifications to `PROJECT_FILE_OBSERVER`s
 * so subscribers can filter cheaply on the fields they care about. Adding
 * a new observable field means adding a new entry here and routing the
 * setter to call `PROJECT_FILE::notifyMultiBoardChanged(<entry>)`.
 *
 * Two synthetic entries cover the file-watcher path:
 *   - EXTERNAL_RELOAD       — file changed under us, in-memory was clean,
 *                             values were updated silently (Phase 4)
 *   - EXTERNAL_RELOAD_DIRTY — file changed under us, in-memory had unsaved
 *                             edits; the conflict is for the subscriber
 *                             (typically a dialog) to resolve (Phase 4 + 6)
 */
enum class MULTI_BOARD_FIELD
{
    // Container-only fields
    SUB_PROJECTS,
    CROSS_BOARD_NETS,
    CUSTOM_MATES,
    ASSEMBLY_INSTANCES,
    MBS_FILE_NAME,
    MIN_POWER_PINS,
    MAX_LENGTH_NM,
    CROSS_BOARD_DIFF_PAIRS,
    CURRENT_RULES,
    VOLTAGE_RULES,

    // Sub-project fields
    INHERIT_NET_SETTINGS,
    CONTAINER_PROJECT_PATH,

    // Synthetic — emitted by the file watcher (Phase 4)
    EXTERNAL_RELOAD,
    EXTERNAL_RELOAD_DIRTY,
};


/**
 * Subscriber interface for `PROJECT_FILE` change events.
 *
 * Implement and register via `PROJECT_FILE::RegisterObserver`. Prefer
 * the RAII helper `SCOPED_PROJECT_FILE_OBSERVER` to ensure the
 * registration matches the subscriber's lifetime.
 *
 * Dispatch is synchronous on the thread that performed the mutation
 * (in practice the wxWidgets main thread). Observers MUST NOT mutate
 * the same field they're being notified about — that would re-enter
 * the dispatch and produce undefined ordering. Use
 * `PROJECT_FILE_SUSPEND_NOTIFY` if you need to coalesce a batch of
 * mutations into a single late event.
 */
class KICOMMON_API PROJECT_FILE_OBSERVER
{
public:
    virtual ~PROJECT_FILE_OBSERVER() = default;

    /**
     * Called once per affected field after a setter mutates the
     * `PROJECT_FILE`. The observer may inspect the project file's
     * read-only accessors to obtain the new value(s).
     */
    virtual void OnMultiBoardFieldChanged( MULTI_BOARD_FIELD aField ) = 0;
};


/**
 * RAII subscription token. Construct on a `PROJECT_FILE` to register;
 * destruction unregisters. Frames typically hold one as a member
 * pinned to the lifetime of the frame; dialogs hold one for the
 * duration they're open.
 *
 * Copy is disabled (subscriptions are unique). Move transfers the
 * registration.
 */
class KICOMMON_API SCOPED_PROJECT_FILE_OBSERVER
{
public:
    SCOPED_PROJECT_FILE_OBSERVER( PROJECT_FILE& aProjectFile, PROJECT_FILE_OBSERVER* aObserver );
    ~SCOPED_PROJECT_FILE_OBSERVER();

    SCOPED_PROJECT_FILE_OBSERVER( const SCOPED_PROJECT_FILE_OBSERVER& )            = delete;
    SCOPED_PROJECT_FILE_OBSERVER& operator=( const SCOPED_PROJECT_FILE_OBSERVER& ) = delete;

    SCOPED_PROJECT_FILE_OBSERVER( SCOPED_PROJECT_FILE_OBSERVER&& aOther ) noexcept;
    SCOPED_PROJECT_FILE_OBSERVER& operator=( SCOPED_PROJECT_FILE_OBSERVER&& aOther ) noexcept;

private:
    PROJECT_FILE*          m_projectFile;
    PROJECT_FILE_OBSERVER* m_observer;
};


/**
 * RAII guard that suspends `PROJECT_FILE` change notifications for the
 * scope of the guard, then dispatches one coalesced event per affected
 * field on destruction.
 *
 * Use when applying a batch of edits where subscribers shouldn't see
 * each intermediate state. Nests safely — outer guard owns the
 * eventual flush. Re-entering during the flush is safe; nested
 * mutations during dispatch produce additional dispatches.
 */
class KICOMMON_API PROJECT_FILE_SUSPEND_NOTIFY
{
public:
    explicit PROJECT_FILE_SUSPEND_NOTIFY( PROJECT_FILE& aProjectFile );
    ~PROJECT_FILE_SUSPEND_NOTIFY();

    PROJECT_FILE_SUSPEND_NOTIFY( const PROJECT_FILE_SUSPEND_NOTIFY& )            = delete;
    PROJECT_FILE_SUSPEND_NOTIFY& operator=( const PROJECT_FILE_SUSPEND_NOTIFY& ) = delete;

private:
    PROJECT_FILE& m_projectFile;
};


#endif   // KICAD_PROJECT_FILE_OBSERVER_H
