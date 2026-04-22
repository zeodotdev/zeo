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

#include <tools/kicad_manager_actions.h>
#include <toolbars_kicad_manager.h>


std::optional<TOOLBAR_CONFIGURATION> KICAD_MANAGER_TOOLBAR_SETTINGS::DefaultToolbarConfig( TOOLBAR_LOC aToolbar )
{
    TOOLBAR_CONFIGURATION config;

    // clang-format off
    switch( aToolbar )
    {
    // No other toolbars
    case TOOLBAR_LOC::RIGHT:
    case TOOLBAR_LOC::TOP_AUX:
    case TOOLBAR_LOC::TOP_MAIN:
        return std::nullopt;

    case TOOLBAR_LOC::LEFT:
        // IDE-sidebar layout: the tree handles per-project actions (open
        // schematic/PCB, manage sub-boards) via double-click and right-
        // click, so this bar is reserved for *global* library editors and
        // utilities, plus the shell-integration group pinned at the
        // bottom.
        config.AppendAction( KICAD_MANAGER_ACTIONS::editSymbols )
              .AppendAction( KICAD_MANAGER_ACTIONS::editFootprints )
              .AppendAction( KICAD_MANAGER_ACTIONS::convertImage )
              .AppendAction( KICAD_MANAGER_ACTIONS::showCalculator )
              .AppendAction( KICAD_MANAGER_ACTIONS::editDrawingSheet )
              .AppendAction( KICAD_MANAGER_ACTIONS::showPluginManager );

        config.AppendStretchSpacer();

        config.AppendSeparator()
              .AppendAction( KICAD_MANAGER_ACTIONS::openProjectDirectory )
              .AppendAction( KICAD_MANAGER_ACTIONS::showTerminal )
              .AppendAction( KICAD_MANAGER_ACTIONS::showVersionControl );

        break;
    }

    // clang-format on
    return config;
}
