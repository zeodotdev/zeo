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

#ifndef PANEL_BOARD_H
#define PANEL_BOARD_H

#include <kiid.h>
#include <geometry/shape_poly_set.h>
#include <math/vector2d.h>
#include <wx/string.h>

#include <memory>
#include <vector>

class BOARD;
class PAD;
class PCB_SHAPE;


/**
 * Type of tab connection between boards in a panel.
 */
enum class PANEL_TAB_TYPE
{
    SOLID,          ///< Full copper/substrate connection
    MOUSEBITE,      ///< Perforated holes for easy break-off
    V_GROOVE,       ///< V-cut for clean separation
    NONE            ///< Gap between boards (no physical connection)
};


/**
 * Configuration for tab generation.
 */
struct PANEL_TAB_SETTINGS
{
    PANEL_TAB_TYPE  type = PANEL_TAB_TYPE::MOUSEBITE;
    int             widthNm = 3000000;          ///< Tab width (3mm default)
    int             spacingNm = 50000000;       ///< Distance between tabs (50mm default)
    int             mousebiteHoleDiaNm = 500000;    ///< Mousebite hole diameter (0.5mm)
    int             mousebiteHoleSpacingNm = 800000; ///< Spacing between mousebite holes (0.8mm)
    int             vGrooveDepthPercent = 30;   ///< V-groove depth as percentage of board thickness

    PANEL_TAB_SETTINGS() = default;
};


/**
 * A single tab connecting a board instance to the panel frame or another board.
 */
struct PANEL_TAB
{
    VECTOR2I        position;       ///< Center position of the tab
    int             widthNm;        ///< Width of the tab
    int             heightNm;       ///< Height/length of the tab
    PANEL_TAB_TYPE  type;           ///< Tab type
    KIID            boardInstanceId; ///< Which board instance this tab connects to

    // For mousebite tabs
    std::vector<VECTOR2I> mousebiteHoles;

    PANEL_TAB() :
            widthNm( 3000000 ),
            heightNm( 3000000 ),
            type( PANEL_TAB_TYPE::MOUSEBITE )
    {}
};


/**
 * Tooling hole pattern for manufacturing.
 */
enum class TOOLING_PATTERN
{
    CORNERS,        ///< Holes at panel corners
    EDGES,          ///< Holes along edges
    CUSTOM          ///< User-defined positions
};


/**
 * Represents a single board placed in the panel.
 */
struct PANEL_BOARD_INSTANCE
{
    KIID            uuid;           ///< Unique ID for this instance
    KIID            sourceBoardUuid; ///< UUID of the source board
    BOARD*          sourceBoard;    ///< Pointer to source board (may be null if not loaded)
    wxString        instanceName;   ///< Display name (e.g., "Board1_A", "Board1_B")
    VECTOR2I        position;       ///< Position in panel coordinates (nm)
    double          rotationDeg;    ///< Rotation in degrees
    bool            mirrored;       ///< Whether the board is mirrored

    PANEL_BOARD_INSTANCE() :
            sourceBoard( nullptr ),
            rotationDeg( 0.0 ),
            mirrored( false )
    {}

    /**
     * Get the bounding box of this instance in panel coordinates.
     */
    BOX2I GetBoundingBox() const;

    /**
     * Transform a point from source board coordinates to panel coordinates.
     */
    VECTOR2I TransformToPanel( const VECTOR2I& aBoardPoint ) const;

    /**
     * Transform a point from panel coordinates to source board coordinates.
     */
    VECTOR2I TransformFromPanel( const VECTOR2I& aPanelPoint ) const;
};


/**
 * Configuration for panel frame/rails.
 */
struct PANEL_FRAME_SETTINGS
{
    bool    addRails = true;            ///< Whether to add manufacturing rails
    int     railWidthNm = 5000000;      ///< Rail width (5mm default)
    bool    railsOnTop = true;          ///< Rails on top edge
    bool    railsOnBottom = true;       ///< Rails on bottom edge
    bool    railsOnLeft = false;        ///< Rails on left edge
    bool    railsOnRight = false;       ///< Rails on right edge
};


/**
 * Configuration for tooling features.
 */
struct PANEL_TOOLING_SETTINGS
{
    bool            addToolingHoles = true;
    int             toolingHoleDiaNm = 3000000;  ///< 3mm diameter
    TOOLING_PATTERN pattern = TOOLING_PATTERN::CORNERS;
    std::vector<VECTOR2I> customPositions;

    bool            addFiducials = true;
    int             fiducialDiaNm = 1000000;     ///< 1mm diameter
    int             fiducialMaskMarginNm = 500000; ///< 0.5mm solder mask opening margin
};


/**
 * Panel layout strategy.
 */
enum class PANEL_LAYOUT
{
    GRID,           ///< Regular grid of rows × columns
    AUTO_OPTIMIZE,  ///< Automatically optimize for minimal waste
    CUSTOM          ///< User-defined placement
};


/**
 * Grid layout configuration.
 */
struct PANEL_GRID_SETTINGS
{
    int     rows = 1;
    int     cols = 1;
    int     spacingXNm = 3000000;    ///< Horizontal spacing (3mm)
    int     spacingYNm = 3000000;    ///< Vertical spacing (3mm)
    bool    alternateRotation = false; ///< Alternate 180° rotation for better nesting
};


/**
 * Manages a manufacturing panel containing multiple board instances.
 *
 * A panel is created from one or more source boards and includes:
 * - Multiple instances of boards arranged in a grid or custom layout
 * - Tabs (mousebite, V-groove, or solid) connecting boards to the frame
 * - Manufacturing rails on the edges
 * - Tooling holes and fiducial markers
 *
 * The panel is itself a BOARD that can be exported to Gerber files.
 */
class PANEL_BOARD
{
public:
    PANEL_BOARD();
    ~PANEL_BOARD();

    /**
     * Get the panel's unique identifier.
     */
    const KIID& GetUuid() const { return m_uuid; }

    /**
     * Set/get the panel name.
     */
    void SetName( const wxString& aName ) { m_name = aName; }
    const wxString& GetName() const { return m_name; }

    // ========== Board Instance Management ==========

    /**
     * Add a board instance to the panel.
     * @param aSourceBoard The source board to instantiate
     * @param aPosition Position in panel coordinates (nm)
     * @param aRotation Rotation in degrees
     * @param aInstanceName Optional instance name
     * @return The UUID of the created instance
     */
    KIID AddBoardInstance( BOARD* aSourceBoard, const VECTOR2I& aPosition,
                           double aRotation = 0.0, const wxString& aInstanceName = wxEmptyString );

    /**
     * Add a board instance by source board UUID (for when board not loaded).
     */
    KIID AddBoardInstance( const KIID& aSourceBoardUuid, const VECTOR2I& aPosition,
                           double aRotation = 0.0, const wxString& aInstanceName = wxEmptyString );

    /**
     * Remove a board instance from the panel.
     */
    bool RemoveBoardInstance( const KIID& aInstanceUuid );

    /**
     * Get all board instances.
     */
    const std::vector<PANEL_BOARD_INSTANCE>& GetBoardInstances() const { return m_boardInstances; }

    /**
     * Get a board instance by UUID.
     */
    PANEL_BOARD_INSTANCE* GetBoardInstance( const KIID& aInstanceUuid );
    const PANEL_BOARD_INSTANCE* GetBoardInstance( const KIID& aInstanceUuid ) const;

    /**
     * Move a board instance to a new position.
     */
    void MoveBoardInstance( const KIID& aInstanceUuid, const VECTOR2I& aNewPosition );

    /**
     * Rotate a board instance.
     */
    void RotateBoardInstance( const KIID& aInstanceUuid, double aDeltaDegrees );

    // ========== Layout ==========

    /**
     * Auto-arrange boards in a grid layout.
     * @param aSettings Grid configuration
     */
    void ArrangeGrid( const PANEL_GRID_SETTINGS& aSettings );

    /**
     * Auto-arrange boards to minimize panel size.
     * @param aSpacingNm Minimum spacing between boards
     */
    void ArrangeOptimized( int aSpacingNm = 3000000 );

    /**
     * Get the overall panel bounding box.
     */
    BOX2I GetBoardInstancesBoundingBox() const;

    // ========== Tab Generation ==========

    /**
     * Automatically generate tabs based on settings.
     * @param aSettings Tab configuration
     */
    void GenerateTabs( const PANEL_TAB_SETTINGS& aSettings );

    /**
     * Add a tab at a specific position.
     */
    void AddTab( const PANEL_TAB& aTab );

    /**
     * Remove all tabs.
     */
    void ClearTabs();

    /**
     * Get all tabs.
     */
    const std::vector<PANEL_TAB>& GetTabs() const { return m_tabs; }

    /**
     * Generate mousebite holes for a tab.
     */
    static std::vector<VECTOR2I> GenerateMousebiteHoles( const PANEL_TAB& aTab,
                                                          const PANEL_TAB_SETTINGS& aSettings );

    // ========== Frame and Tooling ==========

    /**
     * Generate manufacturing rails around the panel.
     */
    void GenerateRails( const PANEL_FRAME_SETTINGS& aSettings );

    /**
     * Add tooling holes to the panel.
     */
    void GenerateToolingHoles( const PANEL_TOOLING_SETTINGS& aSettings );

    /**
     * Add fiducial markers to the panel.
     */
    void GenerateFiducials( const PANEL_TOOLING_SETTINGS& aSettings );

    /**
     * Get the panel outline including rails.
     */
    const SHAPE_POLY_SET& GetPanelOutline() const { return m_panelOutline; }

    /**
     * Get tooling hole positions.
     */
    const std::vector<VECTOR2I>& GetToolingHoles() const { return m_toolingHoles; }

    /**
     * Get fiducial positions.
     */
    const std::vector<VECTOR2I>& GetFiducials() const { return m_fiducials; }

    // ========== Board Generation ==========

    /**
     * Generate the complete panel as a BOARD object.
     * This creates all copper, outline, and silk layers.
     * @return The generated board (caller owns it)
     */
    std::unique_ptr<BOARD> GenerateBoard() const;

    /**
     * Update an existing board with panel contents.
     * @param aBoard The board to update
     */
    void UpdateBoard( BOARD* aBoard ) const;

    // ========== Dimensions ==========

    /**
     * Get panel width in nm.
     */
    int GetWidthNm() const;

    /**
     * Get panel height in nm.
     */
    int GetHeightNm() const;

    /**
     * Get panel width in mm.
     */
    double GetWidthMm() const { return GetWidthNm() / 1000000.0; }

    /**
     * Get panel height in mm.
     */
    double GetHeightMm() const { return GetHeightNm() / 1000000.0; }

private:
    /**
     * Generate outline for board cutouts (gaps between instances).
     */
    void generateBoardCutouts( SHAPE_POLY_SET& aOutline ) const;

    /**
     * Generate V-groove lines for V-groove tabs.
     */
    void generateVGrooves( BOARD* aBoard ) const;

    /**
     * Copy items from a board instance to the panel board.
     */
    void copyBoardInstance( const PANEL_BOARD_INSTANCE& aInstance, BOARD* aPanel ) const;

    KIID                                m_uuid;
    wxString                            m_name;

    std::vector<PANEL_BOARD_INSTANCE>   m_boardInstances;
    std::vector<PANEL_TAB>              m_tabs;

    SHAPE_POLY_SET                      m_panelOutline;
    SHAPE_POLY_SET                      m_railAreas;
    std::vector<VECTOR2I>               m_toolingHoles;
    std::vector<VECTOR2I>               m_fiducials;

    // Settings used for generation
    PANEL_TAB_SETTINGS                  m_tabSettings;
    PANEL_FRAME_SETTINGS                m_frameSettings;
    PANEL_TOOLING_SETTINGS              m_toolingSettings;
    int                                 m_panelThicknessNm;
};

#endif // PANEL_BOARD_H
