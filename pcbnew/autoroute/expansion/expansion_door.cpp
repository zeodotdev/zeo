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

#include "expansion_door.h"
#include "expansion_room.h"


// Static member initialization
int EXPANDABLE_OBJECT::s_nextId = 0;
int EXPANSION_DOOR::s_minSectionLength = 500000;  // 0.5mm default


//-----------------------------------------------------------------------------
// EXPANDABLE_OBJECT Implementation
//-----------------------------------------------------------------------------

EXPANDABLE_OBJECT::EXPANDABLE_OBJECT() :
    m_id( s_nextId++ )
{
}


bool EXPANDABLE_OBJECT::IsOccupied( int aSection ) const
{
    if( aSection < 0 || aSection >= static_cast<int>( m_occupied.size() ) )
        return false;

    return m_occupied[aSection];
}


void EXPANDABLE_OBJECT::SetOccupied( int aSection, bool aOccupied )
{
    int sectionCount = GetSectionCount();
    if( m_occupied.size() < static_cast<size_t>( sectionCount ) )
        m_occupied.resize( sectionCount, false );

    if( aSection >= 0 && aSection < sectionCount )
        m_occupied[aSection] = aOccupied;
}


void EXPANDABLE_OBJECT::ClearOccupied()
{
    std::fill( m_occupied.begin(), m_occupied.end(), false );
}


//-----------------------------------------------------------------------------
// EXPANSION_DOOR Implementation
//-----------------------------------------------------------------------------

EXPANSION_DOOR::EXPANSION_DOOR() :
    EXPANDABLE_OBJECT(),
    m_room1( nullptr ),
    m_room2( nullptr )
{
}


EXPANSION_DOOR::EXPANSION_DOOR( EXPANSION_ROOM* aRoom1, EXPANSION_ROOM* aRoom2,
                                const SEG& aSegment ) :
    EXPANDABLE_OBJECT(),
    m_room1( aRoom1 ),
    m_room2( aRoom2 ),
    m_segment( aSegment )
{
    // Initialize occupied array for all sections
    m_occupied.resize( GetSectionCount(), false );
}


VECTOR2I EXPANSION_DOOR::GetCenter() const
{
    return m_segment.Center();
}


int EXPANSION_DOOR::GetLayer() const
{
    if( m_room1 )
        return m_room1->GetLayer();
    if( m_room2 )
        return m_room2->GetLayer();
    return 0;
}


EXPANSION_ROOM* EXPANSION_DOOR::GetOtherRoom( const EXPANSION_ROOM* aRoom ) const
{
    if( aRoom == m_room1 )
        return m_room2;
    if( aRoom == m_room2 )
        return m_room1;
    return nullptr;
}


bool EXPANSION_DOOR::ContainsPoint( const VECTOR2I& aPt ) const
{
    return m_segment.Contains( aPt );
}


int EXPANSION_DOOR::GetSectionCount() const
{
    if( s_minSectionLength <= 0 )
        return 1;

    int length = m_segment.Length();
    if( length <= s_minSectionLength )
        return 1;

    return ( length + s_minSectionLength - 1 ) / s_minSectionLength;
}


SEG EXPANSION_DOOR::GetSectionSegment( int aSection ) const
{
    int sectionCount = GetSectionCount();

    if( sectionCount <= 1 || aSection < 0 || aSection >= sectionCount )
        return m_segment;

    // Divide the door segment into equal sections
    VECTOR2I dir = m_segment.B - m_segment.A;
    int length = m_segment.Length();

    int startFrac = aSection * length / sectionCount;
    int endFrac = ( aSection + 1 ) * length / sectionCount;

    VECTOR2I start = m_segment.A + VECTOR2I(
        (int64_t)dir.x * startFrac / length,
        (int64_t)dir.y * startFrac / length );

    VECTOR2I end = m_segment.A + VECTOR2I(
        (int64_t)dir.x * endFrac / length,
        (int64_t)dir.y * endFrac / length );

    return SEG( start, end );
}


VECTOR2I EXPANSION_DOOR::GetSectionCenter( int aSection ) const
{
    return GetSectionSegment( aSection ).Center();
}
