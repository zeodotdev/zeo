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

#ifndef MULTI_BOARD_NET_EXTRACTOR_H
#define MULTI_BOARD_NET_EXTRACTOR_H

#include <project/project_file.h>

#include <vector>

class SCHEMATIC;


/**
 * Walk a multi-board schematic's connection graph and extract the set of
 * nets whose subgraph touches SCH_MODULE_PIN items belonging to two or
 * more distinct sub-projects.
 *
 * Uses KiCad's CONNECTION_GRAPH rather than a custom extractor: after
 * the plumbing that lets SCH_MODULE_BLOCK / SCH_MODULE_PIN participate
 * in the graph, label-on-wire net naming, ERC dangling-end tracking,
 * and pin-anchor snapping "just work". The extractor becomes a simple
 * subgraph query.
 *
 * `aMultiBoard` is used to map each module block's `sub_project_path`
 * back to a sub-project uuid — blocks whose path does not match a
 * registered sub-project are still emitted (with a nil uuid) so the
 * caller can diagnose.
 */
std::vector<MB_CROSS_BOARD_NET> ExtractCrossBoardNets( SCHEMATIC& aMbsSchematic,
                                                    const PROJECT_FILE& aMultiBoard );

#endif // MULTI_BOARD_NET_EXTRACTOR_H
