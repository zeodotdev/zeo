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

#include "3d_viewer_assembly.h"

#include <board.h>
#include <board_design_settings.h>
#include <footprint.h>
#include <pad.h>
#include <project.h>
#include <project/project_file.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>


// ========== BOARD_3D_INSTANCE ==========

glm::mat4 BOARD_3D_INSTANCE::GetTransformMatrix() const
{
    glm::mat4 transform = glm::mat4( 1.0f );

    // Apply translation
    transform = glm::translate( transform, glm::vec3( position.x, position.y, position.z ) );

    // Apply rotations (Z, Y, X order - Euler angles)
    transform = glm::rotate( transform, glm::radians( rotation.z ), glm::vec3( 0.0f, 0.0f, 1.0f ) );
    transform = glm::rotate( transform, glm::radians( rotation.y ), glm::vec3( 0.0f, 1.0f, 0.0f ) );
    transform = glm::rotate( transform, glm::radians( rotation.x ), glm::vec3( 1.0f, 0.0f, 0.0f ) );

    return transform;
}


// ========== ASSEMBLY_3D_MANAGER ==========

ASSEMBLY_3D_MANAGER::ASSEMBLY_3D_MANAGER() :
        m_project( nullptr )
{
}


ASSEMBLY_3D_MANAGER::~ASSEMBLY_3D_MANAGER()
{
    if( m_project )
        m_project->RemoveDestroyHook( this );
}


void ASSEMBLY_3D_MANAGER::LoadProjectBoards( PROJECT* aProject )
{
    Clear();
    m_project = aProject;

    if( m_project )
        m_project->AddDestroyHook( this, [this]() { m_project = nullptr; } );

    if( !aProject )
        return;

    PROJECT_FILE& projectFile = aProject->GetProjectFile();
    const auto& boardInfos = projectFile.GetBoardInfos();

    // For each board in the project, create a 3D instance
    for( const BOARD_INFO& info : boardInfos )
    {
        BOARD_3D_INSTANCE instance;
        instance.uuid = KIID();
        instance.boardUuid = info.uuid;
        instance.board = nullptr;  // Board needs to be loaded separately
        instance.displayName = info.displayName;
        instance.visible = true;

        m_boardInstances.push_back( instance );
    }

    // Arrange boards in flat layout by default
    ArrangeBoards( BOARD_LAYOUT_MODE::FLAT, 20.0f );
}


void ASSEMBLY_3D_MANAGER::Clear()
{
    if( m_project )
        m_project->RemoveDestroyHook( this );

    m_boardInstances.clear();
    m_lastCollisions.clear();
    m_project = nullptr;
}


KIID ASSEMBLY_3D_MANAGER::AddBoardInstance( BOARD* aBoard, const wxString& aDisplayName )
{
    BOARD_3D_INSTANCE instance;
    instance.uuid = KIID();
    instance.boardUuid = aBoard ? aBoard->m_Uuid : KIID();
    instance.board = aBoard;
    instance.displayName = aDisplayName;
    instance.visible = true;

    m_boardInstances.push_back( instance );
    return instance.uuid;
}


bool ASSEMBLY_3D_MANAGER::RemoveBoardInstance( const KIID& aInstanceUuid )
{
    auto it = std::find_if( m_boardInstances.begin(), m_boardInstances.end(),
            [&aInstanceUuid]( const BOARD_3D_INSTANCE& inst )
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


BOARD_3D_INSTANCE* ASSEMBLY_3D_MANAGER::GetBoardInstance( const KIID& aInstanceUuid )
{
    for( auto& inst : m_boardInstances )
    {
        if( inst.uuid == aInstanceUuid )
            return &inst;
    }
    return nullptr;
}


const BOARD_3D_INSTANCE* ASSEMBLY_3D_MANAGER::GetBoardInstance( const KIID& aInstanceUuid ) const
{
    for( const auto& inst : m_boardInstances )
    {
        if( inst.uuid == aInstanceUuid )
            return &inst;
    }
    return nullptr;
}


void ASSEMBLY_3D_MANAGER::SetBoardPosition( const KIID& aInstanceUuid, const SFVEC3F& aPosition )
{
    if( BOARD_3D_INSTANCE* inst = GetBoardInstance( aInstanceUuid ) )
        inst->position = aPosition;
}


void ASSEMBLY_3D_MANAGER::SetBoardRotation( const KIID& aInstanceUuid, const SFVEC3F& aRotation )
{
    if( BOARD_3D_INSTANCE* inst = GetBoardInstance( aInstanceUuid ) )
        inst->rotation = aRotation;
}


void ASSEMBLY_3D_MANAGER::ArrangeBoards( BOARD_LAYOUT_MODE aMode, float aSpacingMm )
{
    if( m_boardInstances.empty() )
        return;

    switch( aMode )
    {
    case BOARD_LAYOUT_MODE::FLAT:
    {
        // Arrange boards side by side along X axis
        float xOffset = 0.0f;
        for( auto& inst : m_boardInstances )
        {
            inst.position = SFVEC3F( xOffset, 0.0f, 0.0f );
            inst.rotation = SFVEC3F( 0.0f, 0.0f, 0.0f );

            // Estimate board width (would need actual board dimensions)
            float boardWidth = 100.0f;  // Default 100mm
            if( inst.board )
            {
                BOX2I bbox = inst.board->GetBoardEdgesBoundingBox();
                boardWidth = static_cast<float>( bbox.GetWidth() ) / 1000000.0f;
            }

            xOffset += boardWidth + aSpacingMm;
        }
        break;
    }

    case BOARD_LAYOUT_MODE::STACKED:
    {
        // Stack boards along Z axis
        float zOffset = 0.0f;
        for( auto& inst : m_boardInstances )
        {
            inst.position = SFVEC3F( 0.0f, 0.0f, zOffset );
            inst.rotation = SFVEC3F( 0.0f, 0.0f, 0.0f );

            // Get board thickness
            float boardThickness = GetBoardThickness( inst.board );
            zOffset += boardThickness + aSpacingMm;
        }
        break;
    }

    case BOARD_LAYOUT_MODE::CUSTOM:
        // Don't change positions in custom mode
        break;
    }
}


void ASSEMBLY_3D_MANAGER::ResetPositions()
{
    for( auto& inst : m_boardInstances )
    {
        inst.position = SFVEC3F( 0.0f, 0.0f, 0.0f );
        inst.rotation = SFVEC3F( 0.0f, 0.0f, 0.0f );
    }

    ArrangeBoards( BOARD_LAYOUT_MODE::FLAT, 20.0f );
}


void ASSEMBLY_3D_MANAGER::SetBoardVisible( const KIID& aInstanceUuid, bool aVisible )
{
    if( BOARD_3D_INSTANCE* inst = GetBoardInstance( aInstanceUuid ) )
        inst->visible = aVisible;
}


void ASSEMBLY_3D_MANAGER::SetBoardTransparent( const KIID& aInstanceUuid, bool aTransparent,
                                                float aOpacity )
{
    if( BOARD_3D_INSTANCE* inst = GetBoardInstance( aInstanceUuid ) )
    {
        inst->transparent = aTransparent;
        inst->opacity = aOpacity;
    }
}


void ASSEMBLY_3D_MANAGER::ShowAllBoards()
{
    for( auto& inst : m_boardInstances )
        inst.visible = true;
}


void ASSEMBLY_3D_MANAGER::HideAllBoards()
{
    for( auto& inst : m_boardInstances )
        inst.visible = false;
}


void ASSEMBLY_3D_MANAGER::MateConnectors()
{
    if( !m_project || m_boardInstances.size() < 2 )
        return;

    PROJECT_FILE& projectFile = m_project->GetProjectFile();
    const auto& connections = projectFile.GetCrossBoardConnections();

    // For each cross-board connection, align the boards
    for( const CROSS_BOARD_CONNECTION& conn : connections )
    {
        // Find the two board instances
        BOARD_3D_INSTANCE* inst1 = nullptr;
        BOARD_3D_INSTANCE* inst2 = nullptr;

        for( auto& inst : m_boardInstances )
        {
            if( inst.boardUuid == conn.board1Uuid )
                inst1 = &inst;
            if( inst.boardUuid == conn.board2Uuid )
                inst2 = &inst;
        }

        if( !inst1 || !inst2 || !inst1->board || !inst2->board )
            continue;

        // Calculate mating offset
        SFVEC3F offset = CalculateMatingOffset( *inst1, *inst2, conn.pad1Uuid, conn.pad2Uuid );

        // Move inst2 relative to inst1
        inst2->position = inst1->position + offset;
    }

    m_state.mateConnectors = true;
}


bool ASSEMBLY_3D_MANAGER::CanMateConnectors() const
{
    if( !m_project )
        return false;

    PROJECT_FILE& projectFile = m_project->GetProjectFile();
    return !projectFile.GetCrossBoardConnections().empty();
}


std::vector<COLLISION_RESULT> ASSEMBLY_3D_MANAGER::RunCollisionCheck()
{
    m_lastCollisions.clear();

    // Simple bounding box collision detection
    // Full 3D collision detection would require mesh intersection
    for( size_t i = 0; i < m_boardInstances.size(); i++ )
    {
        const BOARD_3D_INSTANCE& inst1 = m_boardInstances[i];
        if( !inst1.visible || !inst1.board )
            continue;

        for( size_t j = i + 1; j < m_boardInstances.size(); j++ )
        {
            const BOARD_3D_INSTANCE& inst2 = m_boardInstances[j];
            if( !inst2.visible || !inst2.board )
                continue;

            // Get bounding boxes
            BOX2I bbox1 = inst1.board->GetBoardEdgesBoundingBox();
            BOX2I bbox2 = inst2.board->GetBoardEdgesBoundingBox();

            // Convert to 3D with positions
            float z1Min = inst1.position.z;
            float z1Max = inst1.position.z + GetBoardThickness( inst1.board );
            float z2Min = inst2.position.z;
            float z2Max = inst2.position.z + GetBoardThickness( inst2.board );

            // Check Z overlap
            if( z1Max <= z2Min || z2Max <= z1Min )
                continue;  // No Z overlap

            // Check XY overlap (simplified - doesn't account for rotation)
            float x1Min = inst1.position.x + bbox1.GetLeft() / 1000000.0f;
            float x1Max = inst1.position.x + bbox1.GetRight() / 1000000.0f;
            float x2Min = inst2.position.x + bbox2.GetLeft() / 1000000.0f;
            float x2Max = inst2.position.x + bbox2.GetRight() / 1000000.0f;

            float y1Min = inst1.position.y + bbox1.GetTop() / 1000000.0f;
            float y1Max = inst1.position.y + bbox1.GetBottom() / 1000000.0f;
            float y2Min = inst2.position.y + bbox2.GetTop() / 1000000.0f;
            float y2Max = inst2.position.y + bbox2.GetBottom() / 1000000.0f;

            bool xOverlap = x1Max > x2Min && x2Max > x1Min;
            bool yOverlap = y1Max > y2Min && y2Max > y1Min;

            if( xOverlap && yOverlap )
            {
                COLLISION_RESULT result;
                result.board1Uuid = inst1.uuid;
                result.board2Uuid = inst2.uuid;
                result.item1Desc = inst1.displayName;
                result.item2Desc = inst2.displayName;

                // Calculate collision point (center of overlap)
                float cx = ( std::max( x1Min, x2Min ) + std::min( x1Max, x2Max ) ) / 2.0f;
                float cy = ( std::max( y1Min, y2Min ) + std::min( y1Max, y2Max ) ) / 2.0f;
                float cz = ( std::max( z1Min, z2Min ) + std::min( z1Max, z2Max ) ) / 2.0f;
                result.collisionPoint = SFVEC3F( cx, cy, cz );

                // Calculate penetration depth
                float xPen = std::min( x1Max - x2Min, x2Max - x1Min );
                float yPen = std::min( y1Max - y2Min, y2Max - y1Min );
                float zPen = std::min( z1Max - z2Min, z2Max - z1Min );
                result.penetrationMm = std::min( { xPen, yPen, zPen } );

                m_lastCollisions.push_back( result );
            }
        }
    }

    return m_lastCollisions;
}


bool ASSEMBLY_3D_MANAGER::ExportAssemblySTEP( const wxString& aFilename )
{
    // TODO: Implement STEP assembly export using OpenCASCADE
    // This would:
    // 1. Create a STEP compound
    // 2. For each visible board instance:
    //    - Export board as STEP shape
    //    - Apply transformation (position/rotation)
    //    - Add to compound
    // 3. Write compound to file

    return false;
}


void ASSEMBLY_3D_MANAGER::GetAssemblyBoundingBox( SFVEC3F& aMin, SFVEC3F& aMax ) const
{
    bool first = true;

    for( const auto& inst : m_boardInstances )
    {
        if( !inst.visible || !inst.board )
            continue;

        BOX2I bbox = inst.board->GetBoardEdgesBoundingBox();
        float thickness = GetBoardThickness( inst.board );

        SFVEC3F instMin(
            inst.position.x + bbox.GetLeft() / 1000000.0f,
            inst.position.y + bbox.GetTop() / 1000000.0f,
            inst.position.z
        );

        SFVEC3F instMax(
            inst.position.x + bbox.GetRight() / 1000000.0f,
            inst.position.y + bbox.GetBottom() / 1000000.0f,
            inst.position.z + thickness
        );

        if( first )
        {
            aMin = instMin;
            aMax = instMax;
            first = false;
        }
        else
        {
            aMin.x = std::min( aMin.x, instMin.x );
            aMin.y = std::min( aMin.y, instMin.y );
            aMin.z = std::min( aMin.z, instMin.z );
            aMax.x = std::max( aMax.x, instMax.x );
            aMax.y = std::max( aMax.y, instMax.y );
            aMax.z = std::max( aMax.z, instMax.z );
        }
    }
}


SFVEC3F ASSEMBLY_3D_MANAGER::CalculateMatingOffset( const BOARD_3D_INSTANCE& aBoard1,
                                                     const BOARD_3D_INSTANCE& aBoard2,
                                                     const KIID& aConnector1Uuid,
                                                     const KIID& aConnector2Uuid )
{
    // Find connector footprints
    FOOTPRINT* fp1 = nullptr;
    FOOTPRINT* fp2 = nullptr;

    if( aBoard1.board )
    {
        for( FOOTPRINT* fp : aBoard1.board->Footprints() )
        {
            for( PAD* pad : fp->Pads() )
            {
                if( pad->m_Uuid == aConnector1Uuid )
                {
                    fp1 = fp;
                    break;
                }
            }
            if( fp1 )
                break;
        }
    }

    if( aBoard2.board )
    {
        for( FOOTPRINT* fp : aBoard2.board->Footprints() )
        {
            for( PAD* pad : fp->Pads() )
            {
                if( pad->m_Uuid == aConnector2Uuid )
                {
                    fp2 = fp;
                    break;
                }
            }
            if( fp2 )
                break;
        }
    }

    if( !fp1 || !fp2 )
        return SFVEC3F( 0, 0, 0 );

    // Calculate offset to align connectors
    VECTOR2I pos1 = fp1->GetPosition();
    VECTOR2I pos2 = fp2->GetPosition();

    float dx = ( pos1.x - pos2.x ) / 1000000.0f;
    float dy = ( pos1.y - pos2.y ) / 1000000.0f;

    // Offset in Z to stack boards with connector mating gap
    float thickness1 = GetBoardThickness( aBoard1.board );
    float connectorHeight = 5.0f;  // Assume 5mm connector height
    float dz = thickness1 + connectorHeight;

    return SFVEC3F( dx, dy, dz );
}


float ASSEMBLY_3D_MANAGER::GetBoardThickness( const BOARD* aBoard ) const
{
    if( !aBoard )
        return 1.6f;  // Default 1.6mm

    const BOARD_DESIGN_SETTINGS& bds = aBoard->GetDesignSettings();
    return static_cast<float>( bds.GetBoardThickness() ) / 1000000.0f;
}
