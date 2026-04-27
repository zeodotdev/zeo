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

#include "sch_module_block.h"
#include "sch_module_pin.h"

#include <bitmaps.h>
#include <geometry/geometry_utils.h>
#include <layer_ids.h>
#include <trigo.h>

#include <algorithm>


SCH_MODULE_BLOCK::SCH_MODULE_BLOCK( const VECTOR2I& aPos ) :
        SCH_ITEM( nullptr, SCH_MODULE_BLOCK_T ),
        m_pos( aPos ),
        m_size( DEFAULT_WIDTH, DEFAULT_HEIGHT ),
        m_displayName( wxT( "Module" ) )
{
    SetLayer( LAYER_NOTES );
}


SCH_MODULE_BLOCK::SCH_MODULE_BLOCK( const SCH_MODULE_BLOCK& aOther ) :
        SCH_ITEM( aOther ),
        m_pos( aOther.m_pos ),
        m_size( aOther.m_size ),
        m_subProjectUuid( aOther.m_subProjectUuid ),
        m_subProjectPath( aOther.m_subProjectPath ),
        m_componentRef( aOther.m_componentRef ),
        m_mbsReference( aOther.m_mbsReference ),
        m_displayName( aOther.m_displayName )
{
    copyPinsFrom( aOther );
}


SCH_MODULE_BLOCK& SCH_MODULE_BLOCK::operator=( const SCH_MODULE_BLOCK& aOther )
{
    if( this == &aOther )
        return *this;

    SCH_ITEM::operator=( aOther );

    m_pos            = aOther.m_pos;
    m_size           = aOther.m_size;
    m_subProjectUuid = aOther.m_subProjectUuid;
    m_subProjectPath = aOther.m_subProjectPath;
    m_componentRef   = aOther.m_componentRef;
    m_mbsReference   = aOther.m_mbsReference;
    m_displayName    = aOther.m_displayName;

    clearPins();
    copyPinsFrom( aOther );

    return *this;
}


SCH_MODULE_BLOCK::~SCH_MODULE_BLOCK()
{
    clearPins();
}


void SCH_MODULE_BLOCK::clearPins()
{
    for( SCH_MODULE_PIN* pin : m_pins )
        delete pin;

    m_pins.clear();
}


void SCH_MODULE_BLOCK::copyPinsFrom( const SCH_MODULE_BLOCK& aOther )
{
    m_pins.reserve( aOther.m_pins.size() );

    for( SCH_MODULE_PIN* src : aOther.m_pins )
    {
        SCH_MODULE_PIN* copy = static_cast<SCH_MODULE_PIN*>( src->Clone() );
        copy->SetParent( this );
        m_pins.push_back( copy );
    }
}


void SCH_MODULE_BLOCK::AddPin( SCH_MODULE_PIN* aPin )
{
    wxCHECK_RET( aPin, wxT( "null pin" ) );
    aPin->SetParent( this );
    m_pins.push_back( aPin );
}


bool SCH_MODULE_BLOCK::RemovePin( SCH_MODULE_PIN* aPin )
{
    auto it = std::find( m_pins.begin(), m_pins.end(), aPin );

    if( it == m_pins.end() )
        return false;

    delete *it;
    m_pins.erase( it );
    return true;
}


const BOX2I SCH_MODULE_BLOCK::GetBoundingBox() const
{
    BOX2I bbox( m_pos, m_size );

    // Expand by half a grid so the pin arrow graphics (which protrude
    // outward from the edge) are fully covered.
    bbox.Inflate( schIUScale.MilsToIU( 50 ) );
    return bbox;
}


std::vector<int> SCH_MODULE_BLOCK::ViewGetLayers() const
{
    return { LAYER_SHEET_BACKGROUND,
             LAYER_SHEET,
             LAYER_SHEETNAME,
             LAYER_SHEETFILENAME,
             LAYER_SHEETLABEL,
             LAYER_SELECTION_SHADOWS };
}


void SCH_MODULE_BLOCK::SetPosition( const VECTOR2I& aPos )
{
    Move( aPos - m_pos );
}


void SCH_MODULE_BLOCK::Move( const VECTOR2I& aDelta )
{
    m_pos += aDelta;

    // Keep child pins locked to the block's edges.
    for( SCH_MODULE_PIN* pin : m_pins )
        pin->Move( aDelta );
}


bool SCH_MODULE_BLOCK::HitTest( const VECTOR2I& aPosition, int aAccuracy ) const
{
    BOX2I bbox( m_pos, m_size );
    bbox.Inflate( aAccuracy );
    return bbox.Contains( aPosition );
}


bool SCH_MODULE_BLOCK::HitTest( const BOX2I& aRect, bool aContained, int aAccuracy ) const
{
    BOX2I bbox( m_pos, m_size );
    BOX2I test = aRect;
    test.Inflate( aAccuracy );

    if( aContained )
        return test.Contains( bbox );

    return test.Intersects( bbox );
}


bool SCH_MODULE_BLOCK::HitTest( const SHAPE_LINE_CHAIN& aPoly, bool aContained ) const
{
    return KIGEOM::BoxHitTest( aPoly, BOX2I( m_pos, m_size ), aContained );
}


INSPECT_RESULT SCH_MODULE_BLOCK::Visit( INSPECTOR aInspector, void* testData,
                                        const std::vector<KICAD_T>& aScanTypes )
{
    for( KICAD_T scanType : aScanTypes )
    {
        if( scanType == SCH_LOCATE_ANY_T || scanType == Type() )
        {
            if( INSPECT_RESULT::QUIT == aInspector( this, nullptr ) )
                return INSPECT_RESULT::QUIT;
        }

        if( scanType == SCH_LOCATE_ANY_T || scanType == SCH_MODULE_PIN_T )
        {
            for( SCH_MODULE_PIN* pin : m_pins )
            {
                if( INSPECT_RESULT::QUIT == aInspector( pin, this ) )
                    return INSPECT_RESULT::QUIT;
            }
        }
    }

    return INSPECT_RESULT::CONTINUE;
}


void SCH_MODULE_BLOCK::RunOnChildren( const std::function<void( SCH_ITEM* )>& aFunction,
                                      RECURSE_MODE aMode )
{
    for( SCH_MODULE_PIN* pin : m_pins )
        aFunction( pin );
}


std::vector<VECTOR2I> SCH_MODULE_BLOCK::GetConnectionPoints() const
{
    std::vector<VECTOR2I> out;
    out.reserve( m_pins.size() );

    for( const SCH_MODULE_PIN* pin : m_pins )
        out.push_back( pin->GetPosition() );

    return out;
}


void SCH_MODULE_BLOCK::GetEndPoints( std::vector<DANGLING_END_ITEM>& aItemList )
{
    for( SCH_MODULE_PIN* pin : m_pins )
        pin->GetEndPoints( aItemList );
}


bool SCH_MODULE_BLOCK::HasConnectivityChanges( const SCH_ITEM* aItem,
                                               const SCH_SHEET_PATH* aInstance ) const
{
    if( aItem->Type() != SCH_MODULE_BLOCK_T )
        return true;

    const SCH_MODULE_BLOCK* other = static_cast<const SCH_MODULE_BLOCK*>( aItem );

    if( m_pins.size() != other->m_pins.size() )
        return true;

    for( size_t i = 0; i < m_pins.size(); i++ )
    {
        if( m_pins[i]->HasConnectivityChanges( other->m_pins[i] ) )
            return true;
    }

    return false;
}


bool SCH_MODULE_BLOCK::doIsConnected( const VECTOR2I& aPosition ) const
{
    for( const SCH_MODULE_PIN* pin : m_pins )
    {
        if( pin->GetPosition() == aPosition )
            return true;
    }

    return false;
}


EDA_ITEM* SCH_MODULE_BLOCK::Clone() const
{
    return new SCH_MODULE_BLOCK( *this );
}


wxString SCH_MODULE_BLOCK::GetItemDescription( UNITS_PROVIDER* aUnitsProvider, bool aFull ) const
{
    if( m_displayName.IsEmpty() )
        return _( "Multi-Board Module Block" );

    return wxString::Format( _( "Multi-Board Module Block (%s)" ), m_displayName );
}


BITMAPS SCH_MODULE_BLOCK::GetMenuImage() const
{
    return BITMAPS::add_hierarchical_subsheet;
}


double SCH_MODULE_BLOCK::Similarity( const SCH_ITEM& aOther ) const
{
    if( aOther.Type() != Type() )
        return 0.0;

    const SCH_MODULE_BLOCK& other = static_cast<const SCH_MODULE_BLOCK&>( aOther );

    return ( m_subProjectUuid == other.m_subProjectUuid ) ? 1.0 : 0.0;
}


bool SCH_MODULE_BLOCK::operator==( const SCH_ITEM& aOther ) const
{
    if( aOther.Type() != Type() )
        return false;

    const SCH_MODULE_BLOCK& other = static_cast<const SCH_MODULE_BLOCK&>( aOther );

    return m_pos == other.m_pos && m_size == other.m_size
           && m_subProjectUuid == other.m_subProjectUuid
           && m_componentRef == other.m_componentRef
           && m_mbsReference == other.m_mbsReference
           && m_displayName == other.m_displayName;
}


void SCH_MODULE_BLOCK::swapData( SCH_ITEM* aItem )
{
    wxCHECK_RET( aItem && aItem->Type() == SCH_MODULE_BLOCK_T,
                 wxT( "SCH_MODULE_BLOCK::swapData: wrong item type" ) );

    SCH_MODULE_BLOCK* other = static_cast<SCH_MODULE_BLOCK*>( aItem );

    std::swap( m_pos, other->m_pos );
    std::swap( m_size, other->m_size );
    std::swap( m_subProjectUuid, other->m_subProjectUuid );
    std::swap( m_subProjectPath, other->m_subProjectPath );
    std::swap( m_componentRef, other->m_componentRef );
    std::swap( m_mbsReference, other->m_mbsReference );
    std::swap( m_displayName, other->m_displayName );
    std::swap( m_pins, other->m_pins );

    // Re-parent pins to their new owners after the swap.
    for( SCH_MODULE_PIN* pin : m_pins )
        pin->SetParent( this );

    for( SCH_MODULE_PIN* pin : other->m_pins )
        pin->SetParent( other );
}
