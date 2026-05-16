/*
 * Copyright (C) 2026, Zeo <team@zeo.dev>
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
