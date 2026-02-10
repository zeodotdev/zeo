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

#include "autoroute.h"


AUTOROUTER::AUTOROUTER()
{
}


void AUTOROUTER::SetBoard( BOARD* aBoard )
{
    m_engine.Initialize( aBoard, m_control );
}


void AUTOROUTER::SetControl( const AUTOROUTE_CONTROL& aControl )
{
    m_control = aControl;
}


AUTOROUTE_RESULT AUTOROUTER::RouteAll()
{
    m_routingCode = m_engine.RouteAll();
    return m_engine.GetResult();
}


AUTOROUTE_RESULT AUTOROUTER::RouteNets( const std::set<std::string>& aNetNames )
{
    AUTOROUTE_CONTROL control = m_control;
    control.nets_to_route = aNetNames;

    m_engine.Initialize( nullptr, control );  // Will use existing board
    m_routingCode = m_engine.RouteAll();

    return m_engine.GetResult();
}
