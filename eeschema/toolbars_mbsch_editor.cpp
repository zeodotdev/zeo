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

#include "toolbars_mbsch_editor.h"

#include <tool/actions.h>
#include <tools/sch_actions.h>


std::optional<TOOLBAR_CONFIGURATION> MBSCH_EDIT_TOOLBAR_SETTINGS::DefaultToolbarConfig( TOOLBAR_LOC aToolbar )
{
    TOOLBAR_CONFIGURATION config;

    // clang-format off
    switch( aToolbar )
    {
    case TOOLBAR_LOC::TOP_AUX:
        return std::nullopt;

    case TOOLBAR_LOC::LEFT:
        // Grid / units / line modes — core editing ergonomics that
        // apply identically to an MBS. Skip the SCH-specific toggles
        // (hidden pins, annotate-auto, hierarchy pane) that don't make
        // sense in a flat cross-board wiring sheet.
        config.AppendAction( ACTIONS::toggleGrid )
              .AppendAction( ACTIONS::toggleGridOverrides )
              .AppendGroup( TOOLBAR_GROUP_CONFIG( _( "Units" ) )
                            .AddAction( ACTIONS::inchesUnits )
                            .AddAction( ACTIONS::milsUnits )
                            .AddAction( ACTIONS::millimetersUnits ) )
              .AppendGroup( TOOLBAR_GROUP_CONFIG( _( "Crosshair modes" ) )
                            .AddAction( ACTIONS::cursorSmallCrosshairs )
                            .AddAction( ACTIONS::cursorFullCrosshairs )
                            .AddAction( ACTIONS::cursor45Crosshairs ) );

        config.AppendSeparator()
              .AppendGroup( TOOLBAR_GROUP_CONFIG( _( "Line modes" ) )
                            .AddAction( SCH_ACTIONS::lineModeFree )
                            .AddAction( SCH_ACTIONS::lineMode90 )
                            .AddAction( SCH_ACTIONS::lineMode45 ) );

        config.AppendSeparator()
              .AppendAction( ACTIONS::showProperties );

        break;

    case TOOLBAR_LOC::RIGHT:
        // Selection + highlight net are essential. Wiring and labels
        // are the core MBS editing vocabulary — everything else
        // (symbols, power, buses, hierarchical sheets, bus entries,
        // shapes, images) does not belong on a cross-board schematic.
        config.AppendGroup( TOOLBAR_GROUP_CONFIG( _( "Selection modes" ) )
                            .AddAction( ACTIONS::selectSetRect )
                            .AddAction( ACTIONS::selectSetLasso ) )
              .AppendAction( SCH_ACTIONS::highlightNetTool );

        config.AppendSeparator()
              .AppendAction( SCH_ACTIONS::drawWire )
              .AppendAction( SCH_ACTIONS::drawBus )
              .AppendAction( SCH_ACTIONS::placeBusWireEntry )
              .AppendAction( SCH_ACTIONS::placeJunction )
              .AppendGroup( TOOLBAR_GROUP_CONFIG( _( "Labels" ) )
                            .AddAction( SCH_ACTIONS::placeLabel )
                            .AddAction( SCH_ACTIONS::placeGlobalLabel ) );

        config.AppendSeparator()
              .AppendAction( SCH_ACTIONS::placeSchematicText )
              .AppendAction( SCH_ACTIONS::drawTextBox )
              .AppendAction( SCH_ACTIONS::drawTable )
              .AppendAction( SCH_ACTIONS::drawRectangle )
              .AppendAction( SCH_ACTIONS::drawCircle )
              .AppendAction( SCH_ACTIONS::drawArc )
              .AppendAction( SCH_ACTIONS::drawBezier )
              .AppendAction( SCH_ACTIONS::drawLines )
              .AppendAction( SCH_ACTIONS::placeImage )
              .AppendAction( ACTIONS::deleteTool );

        break;

    case TOOLBAR_LOC::TOP_MAIN:
        // File I/O / undo-redo / zoom / find are all useful.
        // Printing is useful. Schematic Setup is meaningful (net
        // classes, drawing sheet). Drop annotation, ERC, simulator,
        // BOM, assign-footprints, symbol/footprint editor launches,
        // pcbnew sync, IPC plugins, variants, python — none of those
        // are well-defined for a multi-board container.
        config.AppendAction( ACTIONS::save )
              .AppendAction( ACTIONS::showVersionControl );

        config.AppendSeparator()
              .AppendAction( SCH_ACTIONS::schematicSetup );

        config.AppendSeparator()
              .AppendAction( ACTIONS::pageSettings )
              .AppendAction( ACTIONS::print )
              .AppendAction( ACTIONS::plot );

        config.AppendSeparator()
              .AppendAction( ACTIONS::paste );

        config.AppendSeparator()
              .AppendAction( ACTIONS::undo )
              .AppendAction( ACTIONS::redo );

        config.AppendSeparator()
              .AppendAction( ACTIONS::find )
              .AppendAction( ACTIONS::findAndReplace );

        config.AppendSeparator()
              .AppendAction( ACTIONS::zoomRedraw )
              .AppendAction( ACTIONS::zoomInCenter )
              .AppendAction( ACTIONS::zoomOutCenter )
              .AppendAction( ACTIONS::zoomFitScreen )
              .AppendAction( ACTIONS::zoomFitObjects )
              .AppendAction( ACTIONS::zoomTool );

        config.AppendSeparator()
              .AppendAction( SCH_ACTIONS::rotateCCW )
              .AppendAction( SCH_ACTIONS::rotateCW )
              .AppendAction( SCH_ACTIONS::mirrorV )
              .AppendAction( SCH_ACTIONS::mirrorH );

        // ERC stays in. The cross-board connectivity test (M5.8)
        // surfaces unwired module pins and single-endpoint nets that
        // pretend to cross boards but actually don't — both
        // genuinely-MBS issues that the regular ERC infrastructure
        // catches once invoked from this frame.
        config.AppendSeparator()
              .AppendAction( SCH_ACTIONS::runERC );

        // Multi-board management group: surfaces the project-manager
        // actions that make sense while the MBSCH is the active
        // editor. Each delegates via SCH_EDITOR_CONTROL to the KiCad
        // manager frame's tool manager so the handlers live in one
        // place. Order: refresh pulls fresh pins in, sync pushes nets
        // out, manage opens the setup dialog, and the Open SCH / PCB
        // buttons show a picker for sub-board navigation (one board
        // at a time — there's no Switch affordance because the
        // active container is fixed for the MBSCH session).
        config.AppendSeparator()
              .AppendAction( SCH_ACTIONS::refreshMbsFromSubProjects )
              .AppendAction( SCH_ACTIONS::mbsSyncCrossBoardNets )
              .AppendAction( SCH_ACTIONS::mbsManageSubBoards )
              .AppendAction( SCH_ACTIONS::mbsCrossBoardRules )
              .AppendAction( SCH_ACTIONS::mbsOpenSubProjectSchematic )
              .AppendAction( SCH_ACTIONS::mbsOpenSubProjectPcb )
              .AppendAction( SCH_ACTIONS::mbsOpen3DAssembly );

        // Agent — same action as the SCH toolbar; opens the AI agent panel
        // against whichever editor is active (FRAME_MBSCH here).
        config.AppendSeparator()
              .AppendAction( SCH_ACTIONS::showAgent );

        break;
    }

    // clang-format on
    return config;
}
