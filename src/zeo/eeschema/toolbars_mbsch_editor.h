/*
 * Copyright (C) 2026, Zeo <team@zeo.dev>
 */

#ifndef TOOLBARS_MBSCH_EDITOR_H_
#define TOOLBARS_MBSCH_EDITOR_H_

#include <tool/ui/toolbar_configuration.h>

/**
 * Toolbar configuration for the multi-board schematic editor frame.
 *
 * Trimmed variant of SCH_EDIT_TOOLBAR_SETTINGS: drops symbol/power
 * placement, hierarchical sheet creation, bus tools, annotation, ERC,
 * simulator, BOM, footprint/symbol editor launches, and PCB sync
 * shortcuts — none of those apply to a cross-board-wiring schematic.
 * Adds the MBS-specific "refresh module blocks from sub-project scan"
 * action alongside the standard save/undo/find/zoom toolbar.
 */
class MBSCH_EDIT_TOOLBAR_SETTINGS : public TOOLBAR_SETTINGS
{
public:
    MBSCH_EDIT_TOOLBAR_SETTINGS() :
            TOOLBAR_SETTINGS( "mbsch-toolbars" )
    {}

    ~MBSCH_EDIT_TOOLBAR_SETTINGS() {}

    std::optional<TOOLBAR_CONFIGURATION> DefaultToolbarConfig( TOOLBAR_LOC aToolbar ) override;
};

#endif /* TOOLBARS_MBSCH_EDITOR_H_ */
