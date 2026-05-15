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

#ifndef SUB_PROJECT_BOARD_LOADER_H
#define SUB_PROJECT_BOARD_LOADER_H

#include <memory>

class BOARD;
class KIID;
class PROJECT;
struct SUB_PROJECT_INFO;


/**
 * Load a sub-project's BOARD from disk for the multi-board container model.
 *
 * Resolves the sub-project's `.kicad_pro` relative to @p aContainer using
 * `PROJECT_FILE::ResolveSubProjectPath`, then loads the sibling
 * `.kicad_pcb` (per `MultiBoardMainPcb` convention) via
 * `PCB_IO_KICAD_SEXPR::LoadBoard`.
 *
 * Caller owns the returned BOARD (it is not cached anywhere). On failure
 * (file missing, parse error, container is not a multi-board container)
 * returns nullptr; parse errors are logged via `wxLogTrace` under
 * `MULTI_BOARD` and swallowed — callers that need rich error reporting
 * should invoke the underlying IO layer directly.
 *
 * The loaded BOARD's owning `PROJECT*` is left null. Callers that need
 * project-aware operations (library lookups, design-rule resolution
 * against the sub-project's settings) must arrange their own PROJECT
 * loading via SETTINGS_MANAGER and assign it post-load — see the M4
 * peer-project pattern in `KIWAY_HOLDER::SetPrjOverride`.
 *
 * Lifetime: per Phase M5/M6 (R9), the loader keeps no shared cache.
 * Each caller (3D viewer, DRC engine, netlist updater) caches its own
 * copy and is responsible for invalidating on `MAIL_RELOAD_SUB_PROJECT`
 * once peer editors broadcast that mail.
 */
std::unique_ptr<BOARD> LoadSubProjectBoard( const PROJECT&          aContainer,
                                            const SUB_PROJECT_INFO& aSubProject );


/**
 * Convenience overload that resolves the SUB_PROJECT_INFO by UUID first.
 *
 * Returns nullptr if @p aContainer has no sub-project registered with
 * UUID @p aSubProjectUuid, or if the BOARD load itself fails.
 */
std::unique_ptr<BOARD> LoadSubProjectBoard( const PROJECT& aContainer,
                                            const KIID&    aSubProjectUuid );


#endif // SUB_PROJECT_BOARD_LOADER_H
