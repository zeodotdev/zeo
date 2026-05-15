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

#include "panel_board.h"

#include <board.h>
#include <footprint.h>
#include <pad.h>
#include <pcb_shape.h>
#include <pcb_track.h>
#include <zone.h>

#include <geometry/shape_rect.h>
#include <trigo.h>

#include <algorithm>
#include <cmath>


// ========== PANEL_BOARD_INSTANCE ==========

BOX2I PANEL_BOARD_INSTANCE::GetBoundingBox() const
{
    if( !sourceBoard )
        return BOX2I();

    BOX2I bbox = sourceBoard->GetBoardEdgesBoundingBox();

    // Apply rotation around origin then translate
    if( std::abs( rotationDeg ) > 0.01 )
    {
        // Get the four corners
        std::vector<VECTOR2I> corners = {
            { bbox.GetLeft(), bbox.GetTop() },
            { bbox.GetRight(), bbox.GetTop() },
            { bbox.GetRight(), bbox.GetBottom() },
            { bbox.GetLeft(), bbox.GetBottom() }
        };

        // Rotate corners
        EDA_ANGLE angle( rotationDeg, DEGREES_T );
        for( auto& pt : corners )
            RotatePoint( pt, VECTOR2I( 0, 0 ), angle );

        // Find new bounding box
        int minX = corners[0].x, maxX = corners[0].x;
        int minY = corners[0].y, maxY = corners[0].y;
        for( const auto& pt : corners )
        {
            minX = std::min( minX, pt.x );
            maxX = std::max( maxX, pt.x );
            minY = std::min( minY, pt.y );
            maxY = std::max( maxY, pt.y );
        }
        bbox = BOX2I( VECTOR2I( minX, minY ), VECTOR2I( maxX - minX, maxY - minY ) );
    }

    // Translate to panel position
    bbox.Move( position );

    return bbox;
}


VECTOR2I PANEL_BOARD_INSTANCE::TransformToPanel( const VECTOR2I& aBoardPoint ) const
{
    VECTOR2I result = aBoardPoint;

    // Apply rotation
    if( std::abs( rotationDeg ) > 0.01 )
    {
        EDA_ANGLE angle( rotationDeg, DEGREES_T );
        RotatePoint( result, VECTOR2I( 0, 0 ), angle );
    }

    // Apply mirror if needed
    if( mirrored )
        result.x = -result.x;

    // Translate to panel position
    result += position;

    return result;
}


VECTOR2I PANEL_BOARD_INSTANCE::TransformFromPanel( const VECTOR2I& aPanelPoint ) const
{
    VECTOR2I result = aPanelPoint;

    // Reverse translate
    result -= position;

    // Reverse mirror
    if( mirrored )
        result.x = -result.x;

    // Reverse rotation
    if( std::abs( rotationDeg ) > 0.01 )
    {
        EDA_ANGLE angle( -rotationDeg, DEGREES_T );
        RotatePoint( result, VECTOR2I( 0, 0 ), angle );
    }

    return result;
}


// ========== PANEL_BOARD ==========

PANEL_BOARD::PANEL_BOARD() :
        m_name( _( "Panel" ) ),
        m_panelThicknessNm( 1600000 )  // 1.6mm default PCB thickness
{
}


PANEL_BOARD::~PANEL_BOARD()
{
}


KIID PANEL_BOARD::AddBoardInstance( BOARD* aSourceBoard, const VECTOR2I& aPosition,
                                     double aRotation, const wxString& aInstanceName )
{
    PANEL_BOARD_INSTANCE instance;
    instance.uuid = KIID();
    instance.sourceBoard = aSourceBoard;
    instance.sourceBoardUuid = aSourceBoard ? aSourceBoard->m_Uuid : KIID();
    instance.position = aPosition;
    instance.rotationDeg = aRotation;
    instance.mirrored = false;

    if( aInstanceName.IsEmpty() && aSourceBoard )
    {
        instance.instanceName = wxString::Format( "%s_%c",
                aSourceBoard->GetFileName().AfterLast( '/' ).BeforeLast( '.' ),
                'A' + static_cast<char>( m_boardInstances.size() ) );
    }
    else
    {
        instance.instanceName = aInstanceName;
    }

    m_boardInstances.push_back( instance );
    return instance.uuid;
}


KIID PANEL_BOARD::AddBoardInstance( const KIID& aSourceBoardUuid, const VECTOR2I& aPosition,
                                     double aRotation, const wxString& aInstanceName )
{
    PANEL_BOARD_INSTANCE instance;
    instance.uuid = KIID();
    instance.sourceBoard = nullptr;  // Not loaded
    instance.sourceBoardUuid = aSourceBoardUuid;
    instance.position = aPosition;
    instance.rotationDeg = aRotation;
    instance.mirrored = false;
    instance.instanceName = aInstanceName.IsEmpty()
            ? wxString::Format( "Board_%c", 'A' + static_cast<char>( m_boardInstances.size() ) )
            : aInstanceName;

    m_boardInstances.push_back( instance );
    return instance.uuid;
}


bool PANEL_BOARD::RemoveBoardInstance( const KIID& aInstanceUuid )
{
    auto it = std::find_if( m_boardInstances.begin(), m_boardInstances.end(),
            [&aInstanceUuid]( const PANEL_BOARD_INSTANCE& inst )
            {
                return inst.uuid == aInstanceUuid;
            } );

    if( it != m_boardInstances.end() )
    {
        m_boardInstances.erase( it );
        return true;
    }
    return false;
}


PANEL_BOARD_INSTANCE* PANEL_BOARD::GetBoardInstance( const KIID& aInstanceUuid )
{
    for( auto& inst : m_boardInstances )
    {
        if( inst.uuid == aInstanceUuid )
            return &inst;
    }
    return nullptr;
}


const PANEL_BOARD_INSTANCE* PANEL_BOARD::GetBoardInstance( const KIID& aInstanceUuid ) const
{
    for( const auto& inst : m_boardInstances )
    {
        if( inst.uuid == aInstanceUuid )
            return &inst;
    }
    return nullptr;
}


void PANEL_BOARD::MoveBoardInstance( const KIID& aInstanceUuid, const VECTOR2I& aNewPosition )
{
    if( PANEL_BOARD_INSTANCE* inst = GetBoardInstance( aInstanceUuid ) )
        inst->position = aNewPosition;
}


void PANEL_BOARD::RotateBoardInstance( const KIID& aInstanceUuid, double aDeltaDegrees )
{
    if( PANEL_BOARD_INSTANCE* inst = GetBoardInstance( aInstanceUuid ) )
    {
        inst->rotationDeg += aDeltaDegrees;
        // Normalize to 0-360
        while( inst->rotationDeg >= 360.0 )
            inst->rotationDeg -= 360.0;
        while( inst->rotationDeg < 0.0 )
            inst->rotationDeg += 360.0;
    }
}


void PANEL_BOARD::ArrangeGrid( const PANEL_GRID_SETTINGS& aSettings )
{
    if( m_boardInstances.empty() )
        return;

    // Get the bounding box of the first board to determine cell size
    BOX2I firstBbox;
    for( const auto& inst : m_boardInstances )
    {
        if( inst.sourceBoard )
        {
            firstBbox = inst.sourceBoard->GetBoardEdgesBoundingBox();
            break;
        }
    }

    if( firstBbox.GetWidth() == 0 || firstBbox.GetHeight() == 0 )
        return;

    int cellWidth = firstBbox.GetWidth() + aSettings.spacingXNm;
    int cellHeight = firstBbox.GetHeight() + aSettings.spacingYNm;

    // Position each instance in grid
    size_t index = 0;
    for( int row = 0; row < aSettings.rows && index < m_boardInstances.size(); row++ )
    {
        for( int col = 0; col < aSettings.cols && index < m_boardInstances.size(); col++ )
        {
            VECTOR2I pos( col * cellWidth, row * cellHeight );

            // Alternate rotation if requested
            double rotation = 0.0;
            if( aSettings.alternateRotation && ( ( row + col ) % 2 == 1 ) )
                rotation = 180.0;

            m_boardInstances[index].position = pos;
            m_boardInstances[index].rotationDeg = rotation;
            index++;
        }
    }
}


void PANEL_BOARD::ArrangeOptimized( int aSpacingNm )
{
    // For now, just use grid arrangement
    // TODO: Implement bin-packing algorithm for optimal arrangement
    PANEL_GRID_SETTINGS settings;
    settings.spacingXNm = aSpacingNm;
    settings.spacingYNm = aSpacingNm;

    // Calculate grid dimensions to be roughly square
    int count = static_cast<int>( m_boardInstances.size() );
    settings.cols = static_cast<int>( std::ceil( std::sqrt( static_cast<double>( count ) ) ) );
    settings.rows = static_cast<int>( std::ceil( static_cast<double>( count ) / settings.cols ) );

    ArrangeGrid( settings );
}


BOX2I PANEL_BOARD::GetBoardInstancesBoundingBox() const
{
    BOX2I bbox;
    bool first = true;

    for( const auto& inst : m_boardInstances )
    {
        BOX2I instBox = inst.GetBoundingBox();
        if( instBox.GetWidth() > 0 && instBox.GetHeight() > 0 )
        {
            if( first )
            {
                bbox = instBox;
                first = false;
            }
            else
            {
                bbox.Merge( instBox );
            }
        }
    }

    return bbox;
}


void PANEL_BOARD::GenerateTabs( const PANEL_TAB_SETTINGS& aSettings )
{
    m_tabSettings = aSettings;
    ClearTabs();

    if( m_boardInstances.empty() )
        return;

    // For each board instance, add tabs around its perimeter
    for( const auto& inst : m_boardInstances )
    {
        if( !inst.sourceBoard )
            continue;

        BOX2I bbox = inst.GetBoundingBox();

        // Calculate number of tabs per edge based on spacing
        int edgeLength = bbox.GetWidth();
        int numTabsX = std::max( 2, ( edgeLength / aSettings.spacingNm ) + 1 );

        edgeLength = bbox.GetHeight();
        int numTabsY = std::max( 2, ( edgeLength / aSettings.spacingNm ) + 1 );

        // Top edge tabs
        for( int i = 0; i < numTabsX; i++ )
        {
            PANEL_TAB tab;
            tab.type = aSettings.type;
            tab.widthNm = aSettings.widthNm;
            tab.heightNm = aSettings.widthNm;  // Square tabs
            tab.boardInstanceId = inst.uuid;

            int x = bbox.GetLeft() + ( bbox.GetWidth() * ( i + 1 ) / ( numTabsX + 1 ) );
            tab.position = VECTOR2I( x, bbox.GetTop() - tab.heightNm / 2 );

            if( aSettings.type == PANEL_TAB_TYPE::MOUSEBITE )
                tab.mousebiteHoles = GenerateMousebiteHoles( tab, aSettings );

            m_tabs.push_back( tab );
        }

        // Bottom edge tabs
        for( int i = 0; i < numTabsX; i++ )
        {
            PANEL_TAB tab;
            tab.type = aSettings.type;
            tab.widthNm = aSettings.widthNm;
            tab.heightNm = aSettings.widthNm;
            tab.boardInstanceId = inst.uuid;

            int x = bbox.GetLeft() + ( bbox.GetWidth() * ( i + 1 ) / ( numTabsX + 1 ) );
            tab.position = VECTOR2I( x, bbox.GetBottom() + tab.heightNm / 2 );

            if( aSettings.type == PANEL_TAB_TYPE::MOUSEBITE )
                tab.mousebiteHoles = GenerateMousebiteHoles( tab, aSettings );

            m_tabs.push_back( tab );
        }

        // Left edge tabs
        for( int i = 0; i < numTabsY; i++ )
        {
            PANEL_TAB tab;
            tab.type = aSettings.type;
            tab.widthNm = aSettings.widthNm;
            tab.heightNm = aSettings.widthNm;
            tab.boardInstanceId = inst.uuid;

            int y = bbox.GetTop() + ( bbox.GetHeight() * ( i + 1 ) / ( numTabsY + 1 ) );
            tab.position = VECTOR2I( bbox.GetLeft() - tab.widthNm / 2, y );

            if( aSettings.type == PANEL_TAB_TYPE::MOUSEBITE )
                tab.mousebiteHoles = GenerateMousebiteHoles( tab, aSettings );

            m_tabs.push_back( tab );
        }

        // Right edge tabs
        for( int i = 0; i < numTabsY; i++ )
        {
            PANEL_TAB tab;
            tab.type = aSettings.type;
            tab.widthNm = aSettings.widthNm;
            tab.heightNm = aSettings.widthNm;
            tab.boardInstanceId = inst.uuid;

            int y = bbox.GetTop() + ( bbox.GetHeight() * ( i + 1 ) / ( numTabsY + 1 ) );
            tab.position = VECTOR2I( bbox.GetRight() + tab.widthNm / 2, y );

            if( aSettings.type == PANEL_TAB_TYPE::MOUSEBITE )
                tab.mousebiteHoles = GenerateMousebiteHoles( tab, aSettings );

            m_tabs.push_back( tab );
        }
    }
}


void PANEL_BOARD::AddTab( const PANEL_TAB& aTab )
{
    m_tabs.push_back( aTab );
}


void PANEL_BOARD::ClearTabs()
{
    m_tabs.clear();
}


std::vector<VECTOR2I> PANEL_BOARD::GenerateMousebiteHoles( const PANEL_TAB& aTab,
                                                            const PANEL_TAB_SETTINGS& aSettings )
{
    std::vector<VECTOR2I> holes;

    // Generate holes along the center line of the tab
    int numHoles = aTab.widthNm / aSettings.mousebiteHoleSpacingNm;
    if( numHoles < 3 )
        numHoles = 3;

    for( int i = 0; i < numHoles; i++ )
    {
        int offset = ( i - numHoles / 2 ) * aSettings.mousebiteHoleSpacingNm;
        VECTOR2I holePos = aTab.position;

        // Determine orientation based on tab position relative to board
        // For horizontal edges (top/bottom), holes run horizontally
        // For vertical edges (left/right), holes run vertically
        if( aTab.heightNm > aTab.widthNm )
            holePos.y += offset;
        else
            holePos.x += offset;

        holes.push_back( holePos );
    }

    return holes;
}


void PANEL_BOARD::GenerateRails( const PANEL_FRAME_SETTINGS& aSettings )
{
    m_frameSettings = aSettings;
    m_railAreas.RemoveAllContours();

    if( !aSettings.addRails )
        return;

    BOX2I bbox = GetBoardInstancesBoundingBox();
    if( bbox.GetWidth() == 0 || bbox.GetHeight() == 0 )
        return;

    // Expand bbox to include tab area
    int margin = m_tabSettings.widthNm;
    bbox.Inflate( margin );

    // Create rail polygons
    if( aSettings.railsOnTop )
    {
        SHAPE_POLY_SET rail;
        rail.NewOutline();
        rail.Append( bbox.GetLeft(), bbox.GetTop() - aSettings.railWidthNm );
        rail.Append( bbox.GetRight(), bbox.GetTop() - aSettings.railWidthNm );
        rail.Append( bbox.GetRight(), bbox.GetTop() );
        rail.Append( bbox.GetLeft(), bbox.GetTop() );
        m_railAreas.Append( rail );
    }

    if( aSettings.railsOnBottom )
    {
        SHAPE_POLY_SET rail;
        rail.NewOutline();
        rail.Append( bbox.GetLeft(), bbox.GetBottom() );
        rail.Append( bbox.GetRight(), bbox.GetBottom() );
        rail.Append( bbox.GetRight(), bbox.GetBottom() + aSettings.railWidthNm );
        rail.Append( bbox.GetLeft(), bbox.GetBottom() + aSettings.railWidthNm );
        m_railAreas.Append( rail );
    }

    if( aSettings.railsOnLeft )
    {
        SHAPE_POLY_SET rail;
        rail.NewOutline();
        rail.Append( bbox.GetLeft() - aSettings.railWidthNm, bbox.GetTop() );
        rail.Append( bbox.GetLeft(), bbox.GetTop() );
        rail.Append( bbox.GetLeft(), bbox.GetBottom() );
        rail.Append( bbox.GetLeft() - aSettings.railWidthNm, bbox.GetBottom() );
        m_railAreas.Append( rail );
    }

    if( aSettings.railsOnRight )
    {
        SHAPE_POLY_SET rail;
        rail.NewOutline();
        rail.Append( bbox.GetRight(), bbox.GetTop() );
        rail.Append( bbox.GetRight() + aSettings.railWidthNm, bbox.GetTop() );
        rail.Append( bbox.GetRight() + aSettings.railWidthNm, bbox.GetBottom() );
        rail.Append( bbox.GetRight(), bbox.GetBottom() );
        m_railAreas.Append( rail );
    }

    // Update panel outline
    m_panelOutline = m_railAreas;
}


void PANEL_BOARD::GenerateToolingHoles( const PANEL_TOOLING_SETTINGS& aSettings )
{
    m_toolingSettings = aSettings;
    m_toolingHoles.clear();

    if( !aSettings.addToolingHoles )
        return;

    BOX2I bbox = GetBoardInstancesBoundingBox();
    if( bbox.GetWidth() == 0 || bbox.GetHeight() == 0 )
        return;

    // Expand to include rails
    int margin = m_tabSettings.widthNm + m_frameSettings.railWidthNm;
    bbox.Inflate( margin );

    // Inset slightly from edges
    int inset = aSettings.toolingHoleDiaNm;

    switch( aSettings.pattern )
    {
    case TOOLING_PATTERN::CORNERS:
        m_toolingHoles.push_back( VECTOR2I( bbox.GetLeft() + inset, bbox.GetTop() + inset ) );
        m_toolingHoles.push_back( VECTOR2I( bbox.GetRight() - inset, bbox.GetTop() + inset ) );
        m_toolingHoles.push_back( VECTOR2I( bbox.GetLeft() + inset, bbox.GetBottom() - inset ) );
        m_toolingHoles.push_back( VECTOR2I( bbox.GetRight() - inset, bbox.GetBottom() - inset ) );
        break;

    case TOOLING_PATTERN::EDGES:
        // Corner holes plus edge centers
        m_toolingHoles.push_back( VECTOR2I( bbox.GetLeft() + inset, bbox.GetTop() + inset ) );
        m_toolingHoles.push_back( VECTOR2I( bbox.GetRight() - inset, bbox.GetTop() + inset ) );
        m_toolingHoles.push_back( VECTOR2I( bbox.GetLeft() + inset, bbox.GetBottom() - inset ) );
        m_toolingHoles.push_back( VECTOR2I( bbox.GetRight() - inset, bbox.GetBottom() - inset ) );
        m_toolingHoles.push_back( VECTOR2I( bbox.GetCenter().x, bbox.GetTop() + inset ) );
        m_toolingHoles.push_back( VECTOR2I( bbox.GetCenter().x, bbox.GetBottom() - inset ) );
        break;

    case TOOLING_PATTERN::CUSTOM:
        m_toolingHoles = aSettings.customPositions;
        break;
    }
}


void PANEL_BOARD::GenerateFiducials( const PANEL_TOOLING_SETTINGS& aSettings )
{
    m_fiducials.clear();

    if( !aSettings.addFiducials )
        return;

    BOX2I bbox = GetBoardInstancesBoundingBox();
    if( bbox.GetWidth() == 0 || bbox.GetHeight() == 0 )
        return;

    // Expand to include rails
    int margin = m_tabSettings.widthNm + m_frameSettings.railWidthNm;
    bbox.Inflate( margin );

    // Place fiducials at three corners (standard placement)
    int inset = aSettings.fiducialDiaNm * 2;
    m_fiducials.push_back( VECTOR2I( bbox.GetLeft() + inset, bbox.GetTop() + inset ) );
    m_fiducials.push_back( VECTOR2I( bbox.GetRight() - inset, bbox.GetTop() + inset ) );
    m_fiducials.push_back( VECTOR2I( bbox.GetLeft() + inset, bbox.GetBottom() - inset ) );
}


int PANEL_BOARD::GetWidthNm() const
{
    BOX2I bbox = GetBoardInstancesBoundingBox();
    int margin = m_tabSettings.widthNm + m_frameSettings.railWidthNm;

    if( m_frameSettings.railsOnLeft )
        margin += m_frameSettings.railWidthNm;
    if( m_frameSettings.railsOnRight )
        margin += m_frameSettings.railWidthNm;

    return bbox.GetWidth() + margin * 2;
}


int PANEL_BOARD::GetHeightNm() const
{
    BOX2I bbox = GetBoardInstancesBoundingBox();
    int margin = m_tabSettings.widthNm + m_frameSettings.railWidthNm;

    if( m_frameSettings.railsOnTop )
        margin += m_frameSettings.railWidthNm;
    if( m_frameSettings.railsOnBottom )
        margin += m_frameSettings.railWidthNm;

    return bbox.GetHeight() + margin * 2;
}


std::unique_ptr<BOARD> PANEL_BOARD::GenerateBoard() const
{
    auto board = std::make_unique<BOARD>();
    UpdateBoard( board.get() );
    return board;
}


void PANEL_BOARD::UpdateBoard( BOARD* aBoard ) const
{
    if( !aBoard )
        return;

    // Copy each board instance
    for( const auto& inst : m_boardInstances )
    {
        copyBoardInstance( inst, aBoard );
    }

    // Add panel outline on Edge.Cuts layer
    for( int i = 0; i < m_panelOutline.OutlineCount(); i++ )
    {
        const SHAPE_LINE_CHAIN& outline = m_panelOutline.COutline( i );

        for( int j = 0; j < outline.PointCount(); j++ )
        {
            PCB_SHAPE* segment = new PCB_SHAPE( aBoard, SHAPE_T::SEGMENT );
            segment->SetLayer( Edge_Cuts );
            segment->SetStart( outline.CPoint( j ) );
            segment->SetEnd( outline.CPoint( ( j + 1 ) % outline.PointCount() ) );
            segment->SetStroke( STROKE_PARAMS( 100000, LINE_STYLE::SOLID ) ); // 0.1mm line
            aBoard->Add( segment );
        }
    }

    // Add mousebite holes as pads or non-plated holes
    for( const auto& tab : m_tabs )
    {
        if( tab.type == PANEL_TAB_TYPE::MOUSEBITE )
        {
            for( const auto& holePos : tab.mousebiteHoles )
            {
                // Create a circular hole on Edge.Cuts
                PCB_SHAPE* hole = new PCB_SHAPE( aBoard, SHAPE_T::CIRCLE );
                hole->SetLayer( Edge_Cuts );
                hole->SetCenter( holePos );
                hole->SetEnd( holePos + VECTOR2I( m_tabSettings.mousebiteHoleDiaNm / 2, 0 ) );
                hole->SetStroke( STROKE_PARAMS( 100000, LINE_STYLE::SOLID ) );
                aBoard->Add( hole );
            }
        }
    }

    // Add V-groove lines
    generateVGrooves( aBoard );

    // Add tooling holes
    for( const auto& pos : m_toolingHoles )
    {
        PCB_SHAPE* hole = new PCB_SHAPE( aBoard, SHAPE_T::CIRCLE );
        hole->SetLayer( Edge_Cuts );
        hole->SetCenter( pos );
        hole->SetEnd( pos + VECTOR2I( m_toolingSettings.toolingHoleDiaNm / 2, 0 ) );
        hole->SetStroke( STROKE_PARAMS( 100000, LINE_STYLE::SOLID ) );
        aBoard->Add( hole );
    }

    // Add fiducials (copper circles with solder mask opening)
    for( const auto& pos : m_fiducials )
    {
        // Copper circle
        PCB_SHAPE* copper = new PCB_SHAPE( aBoard, SHAPE_T::CIRCLE );
        copper->SetLayer( F_Cu );
        copper->SetCenter( pos );
        copper->SetEnd( pos + VECTOR2I( m_toolingSettings.fiducialDiaNm / 2, 0 ) );
        copper->SetFilled( true );
        aBoard->Add( copper );

        // Also add to back copper
        PCB_SHAPE* copperBack = new PCB_SHAPE( aBoard, SHAPE_T::CIRCLE );
        copperBack->SetLayer( B_Cu );
        copperBack->SetCenter( pos );
        copperBack->SetEnd( pos + VECTOR2I( m_toolingSettings.fiducialDiaNm / 2, 0 ) );
        copperBack->SetFilled( true );
        aBoard->Add( copperBack );
    }
}


void PANEL_BOARD::copyBoardInstance( const PANEL_BOARD_INSTANCE& aInstance, BOARD* aPanel ) const
{
    if( !aInstance.sourceBoard || !aPanel )
        return;

    BOARD* source = aInstance.sourceBoard;

    // Copy footprints
    for( FOOTPRINT* fp : source->Footprints() )
    {
        FOOTPRINT* newFp = static_cast<FOOTPRINT*>( fp->Clone() );

        // Transform position
        VECTOR2I pos = aInstance.TransformToPanel( fp->GetPosition() );
        newFp->SetPosition( pos );

        // Apply rotation
        newFp->SetOrientation( fp->GetOrientation() + EDA_ANGLE( aInstance.rotationDeg, DEGREES_T ) );

        aPanel->Add( newFp );
    }

    // Copy tracks
    for( PCB_TRACK* track : source->Tracks() )
    {
        PCB_TRACK* newTrack = static_cast<PCB_TRACK*>( track->Clone() );

        newTrack->SetStart( aInstance.TransformToPanel( track->GetStart() ) );
        newTrack->SetEnd( aInstance.TransformToPanel( track->GetEnd() ) );

        aPanel->Add( newTrack );
    }

    // Copy zones
    for( ZONE* zone : source->Zones() )
    {
        ZONE* newZone = static_cast<ZONE*>( zone->Clone() );

        // Transform zone outline
        SHAPE_POLY_SET* outline = newZone->Outline();
        for( int i = 0; i < outline->OutlineCount(); i++ )
        {
            SHAPE_LINE_CHAIN& chain = outline->Outline( i );
            for( int j = 0; j < chain.PointCount(); j++ )
            {
                chain.SetPoint( j, aInstance.TransformToPanel( chain.CPoint( j ) ) );
            }
        }

        aPanel->Add( newZone );
    }

    // Copy drawings (including board outline)
    for( BOARD_ITEM* item : source->Drawings() )
    {
        BOARD_ITEM* newItem = static_cast<BOARD_ITEM*>( item->Clone() );

        if( PCB_SHAPE* shape = dynamic_cast<PCB_SHAPE*>( newItem ) )
        {
            shape->Move( aInstance.position );
            if( std::abs( aInstance.rotationDeg ) > 0.01 )
                shape->Rotate( aInstance.position, EDA_ANGLE( aInstance.rotationDeg, DEGREES_T ) );
        }

        aPanel->Add( newItem );
    }
}


void PANEL_BOARD::generateBoardCutouts( SHAPE_POLY_SET& aOutline ) const
{
    // Generate cutouts between board instances where there are no tabs
    // This is called during panel outline generation
    // TODO: Implement cutout generation
}


void PANEL_BOARD::generateVGrooves( BOARD* aBoard ) const
{
    // Add V-groove indicators on silk screen
    for( const auto& tab : m_tabs )
    {
        if( tab.type == PANEL_TAB_TYPE::V_GROOVE )
        {
            // Draw dashed line on silk screen to indicate V-groove
            PCB_SHAPE* line = new PCB_SHAPE( aBoard, SHAPE_T::SEGMENT );
            line->SetLayer( F_SilkS );

            // Determine line orientation and extent
            // V-grooves typically run the full width/height of the panel
            if( tab.heightNm > tab.widthNm )
            {
                // Vertical V-groove
                BOX2I bbox = GetBoardInstancesBoundingBox();
                line->SetStart( VECTOR2I( tab.position.x, bbox.GetTop() ) );
                line->SetEnd( VECTOR2I( tab.position.x, bbox.GetBottom() ) );
            }
            else
            {
                // Horizontal V-groove
                BOX2I bbox = GetBoardInstancesBoundingBox();
                line->SetStart( VECTOR2I( bbox.GetLeft(), tab.position.y ) );
                line->SetEnd( VECTOR2I( bbox.GetRight(), tab.position.y ) );
            }

            line->SetStroke( STROKE_PARAMS( 200000, LINE_STYLE::DASH ) );
            aBoard->Add( line );
        }
    }
}
