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

#include "sch_module_pin.h"
#include "sch_module_block.h"

#include <bitmaps.h>
#include <geometry/shape_line_chain.h>
#include <schematic.h>
#include <string_utils.h>
#include <trigo.h>


SCH_MODULE_PIN::SCH_MODULE_PIN( SCH_MODULE_BLOCK* aParent, const VECTOR2I& aPos,
                                const wxString& aText ) :
        SCH_HIERLABEL( aPos, aText, SCH_MODULE_PIN_T ),
        m_side( SHEET_SIDE::UNDEFINED ),
        m_electricalType( ELECTRICAL_PINTYPE::PT_PASSIVE )
{
    SetParent( aParent );
    m_layer = LAYER_SHEETLABEL;

    SetTextPos( aPos );

    // Default to LEFT side; callers (parser / setup) will SetSide properly.
    if( aParent )
        SetSide( SHEET_SIDE::LEFT );

    m_shape      = LABEL_FLAG_SHAPE::L_INPUT;
    m_isDangling = true;
}


EDA_ITEM* SCH_MODULE_PIN::Clone() const
{
    return new SCH_MODULE_PIN( *this );
}


int SCH_MODULE_PIN::GetPenWidth() const
{
    if( Schematic() )
        return Schematic()->Settings().m_DefaultLineWidth;

    return schIUScale.MilsToIU( DEFAULT_LINE_WIDTH_MILS );
}


void SCH_MODULE_PIN::SetSide( SHEET_SIDE aEdge )
{
    SCH_MODULE_BLOCK* parent = GetParent();

    if( !parent )
    {
        m_side = aEdge;
        return;
    }

    const VECTOR2I& pos  = parent->GetPosition();
    const VECTOR2I& size = parent->GetSize();

    switch( aEdge )
    {
    case SHEET_SIDE::LEFT:
        m_side = aEdge;
        SetTextX( pos.x );
        SetSpinStyle( SPIN_STYLE::RIGHT );  // text points INTO the block
        break;

    case SHEET_SIDE::RIGHT:
        m_side = aEdge;
        SetTextX( pos.x + size.x );
        SetSpinStyle( SPIN_STYLE::LEFT );
        break;

    case SHEET_SIDE::TOP:
        m_side = aEdge;
        SetTextY( pos.y );
        SetSpinStyle( SPIN_STYLE::BOTTOM );
        break;

    case SHEET_SIDE::BOTTOM:
        m_side = aEdge;
        SetTextY( pos.y + size.y );
        SetSpinStyle( SPIN_STYLE::UP );
        break;

    default:
        break;
    }
}


void SCH_MODULE_PIN::ConstrainOnEdge( const VECTOR2I& aPos, bool aAllowEdgeSwitch )
{
    SCH_MODULE_BLOCK* parent = GetParent();

    if( !parent )
        return;

    const VECTOR2I& bpos  = parent->GetPosition();
    const VECTOR2I& bsize = parent->GetSize();

    int leftSide  = bpos.x;
    int rightSide = bpos.x + bsize.x;
    int topSide   = bpos.y;
    int botSide   = bpos.y + bsize.y;

    if( aAllowEdgeSwitch )
    {
        SHAPE_LINE_CHAIN edge;
        edge.Append( leftSide,  topSide );
        edge.Append( rightSide, topSide );
        edge.Append( rightSide, botSide );
        edge.Append( leftSide,  botSide );
        edge.Append( leftSide,  topSide );

        switch( edge.NearestSegment( aPos ) )
        {
        case 0:  SetSide( SHEET_SIDE::TOP );    break;
        case 1:  SetSide( SHEET_SIDE::RIGHT );  break;
        case 2:  SetSide( SHEET_SIDE::BOTTOM ); break;
        case 3:  SetSide( SHEET_SIDE::LEFT );   break;
        default: break;
        }
    }
    else
    {
        SetSide( m_side );
    }

    switch( m_side )
    {
    case SHEET_SIDE::LEFT:
    case SHEET_SIDE::RIGHT:
        SetTextY( std::clamp( aPos.y, topSide, botSide ) );
        break;

    case SHEET_SIDE::TOP:
    case SHEET_SIDE::BOTTOM:
        SetTextX( std::clamp( aPos.x, leftSide, rightSide ) );
        break;

    default:
        break;
    }
}


void SCH_MODULE_PIN::MirrorVertically( int aCenter )
{
    int p = GetTextPos().y - aCenter;
    SetTextY( aCenter - p );

    switch( m_side )
    {
    case SHEET_SIDE::TOP:    SetSide( SHEET_SIDE::BOTTOM ); break;
    case SHEET_SIDE::BOTTOM: SetSide( SHEET_SIDE::TOP );    break;
    default: break;
    }
}


void SCH_MODULE_PIN::MirrorHorizontally( int aCenter )
{
    int p = GetTextPos().x - aCenter;
    SetTextX( aCenter - p );

    switch( m_side )
    {
    case SHEET_SIDE::LEFT:  SetSide( SHEET_SIDE::RIGHT ); break;
    case SHEET_SIDE::RIGHT: SetSide( SHEET_SIDE::LEFT );  break;
    default: break;
    }
}


void SCH_MODULE_PIN::Rotate( const VECTOR2I& aCenter, bool aRotateCCW )
{
    VECTOR2I pt = GetTextPos();
    RotatePoint( pt, aCenter, aRotateCCW ? ANGLE_90 : ANGLE_270 );
    ConstrainOnEdge( pt, true );
}


void SCH_MODULE_PIN::CreateGraphicShape( const RENDER_SETTINGS* aSettings,
                                         std::vector<VECTOR2I>& aPoints,
                                         const VECTOR2I& aPos ) const
{
    // Invert INPUT <-> OUTPUT so the arrow points OUT of the block instead of
    // INTO it (same trick as SCH_SHEET_PIN::CreateGraphicShape).
    LABEL_FLAG_SHAPE shape = m_shape;

    switch( shape )
    {
    case LABEL_FLAG_SHAPE::L_INPUT:  shape = LABEL_FLAG_SHAPE::L_OUTPUT; break;
    case LABEL_FLAG_SHAPE::L_OUTPUT: shape = LABEL_FLAG_SHAPE::L_INPUT;  break;
    default: break;
    }

    SCH_HIERLABEL::CreateGraphicShape( aSettings, aPoints, aPos, shape );
}


void SCH_MODULE_PIN::GetEndPoints( std::vector<DANGLING_END_ITEM>& aItemList )
{
    aItemList.emplace_back( SHEET_LABEL_END, this, GetTextPos() );
}


bool SCH_MODULE_PIN::HasConnectivityChanges( const SCH_ITEM* aItem,
                                             const SCH_SHEET_PATH* aInstance ) const
{
    if( aItem == this )
        return false;

    const SCH_MODULE_PIN* pin = dynamic_cast<const SCH_MODULE_PIN*>( aItem );

    wxCHECK( pin, false );

    if( GetPosition() != pin->GetPosition() )
        return true;

    return GetText() != pin->GetText();
}


wxString SCH_MODULE_PIN::GetItemDescription( UNITS_PROVIDER* aUnitsProvider, bool aFull ) const
{
    return wxString::Format( _( "Module Block Pin '%s'" ),
                             aFull ? GetShownText( false )
                                   : KIUI::EllipsizeMenuText( GetText() ) );
}


BITMAPS SCH_MODULE_PIN::GetMenuImage() const
{
    return BITMAPS::add_hierar_pin;
}


bool SCH_MODULE_PIN::HitTest( const VECTOR2I& aPoint, int aAccuracy ) const
{
    BOX2I rect = GetBoundingBox();
    rect.Inflate( aAccuracy );
    return rect.Contains( aPoint );
}


bool SCH_MODULE_PIN::operator==( const SCH_ITEM& aOther ) const
{
    if( aOther.Type() != Type() )
        return false;

    const SCH_MODULE_PIN* other = static_cast<const SCH_MODULE_PIN*>( &aOther );

    return m_side == other->m_side
           && m_componentRef == other->m_componentRef
           && m_pinNumber == other->m_pinNumber
           && m_electricalType == other->m_electricalType
           && SCH_HIERLABEL::operator==( aOther );
}


double SCH_MODULE_PIN::Similarity( const SCH_ITEM& aOther ) const
{
    if( aOther.Type() != Type() )
        return 0.0;

    const SCH_MODULE_PIN* other = static_cast<const SCH_MODULE_PIN*>( &aOther );

    double similarity = 1.0;

    if( m_side != other->m_side )
        similarity *= 0.9;

    if( m_pinNumber != other->m_pinNumber )
        similarity *= 0.9;

    similarity *= SCH_HIERLABEL::Similarity( aOther );

    return similarity;
}


void SCH_MODULE_PIN::swapData( SCH_ITEM* aItem )
{
    SCH_HIERLABEL::swapData( aItem );

    wxCHECK_RET( aItem->Type() == SCH_MODULE_PIN_T,
                 wxT( "SCH_MODULE_PIN::swapData: wrong item type" ) );

    SCH_MODULE_PIN* other = static_cast<SCH_MODULE_PIN*>( aItem );

    std::swap( m_side, other->m_side );
    std::swap( m_pinUuid, other->m_pinUuid );
    std::swap( m_componentRef, other->m_componentRef );
    std::swap( m_pinNumber, other->m_pinNumber );
    std::swap( m_electricalType, other->m_electricalType );
}
