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

#ifndef AUTOROUTE_H
#define AUTOROUTE_H

#include "autoroute_control.h"
#include "autoroute_engine.h"
#include <string>

// Forward declarations
class BOARD;


/**
 * Main autorouter class - provides the public API for autorouting.
 *
 * Usage:
 *   AUTOROUTER router;
 *   router.SetBoard(board);
 *   router.SetControl(control);
 *   AUTOROUTE_RESULT result = router.RouteAll();
 *   std::string pythonCode = router.GetRoutingCode();
 */
class AUTOROUTER
{
public:
    AUTOROUTER();
    ~AUTOROUTER() = default;

    /**
     * Set the board to route.
     */
    void SetBoard( BOARD* aBoard );

    /**
     * Set routing control parameters.
     */
    void SetControl( const AUTOROUTE_CONTROL& aControl );

    /**
     * Route all unconnected nets.
     *
     * @return Result of the routing operation
     */
    AUTOROUTE_RESULT RouteAll();

    /**
     * Route specific nets.
     *
     * @param aNetNames Names of nets to route
     * @return Result of the routing operation
     */
    AUTOROUTE_RESULT RouteNets( const std::set<std::string>& aNetNames );

    /**
     * Get the Python/kipy code to insert the routed traces.
     * Call after RouteAll() or RouteNets().
     *
     * @return Python code string to execute via IPC
     */
    std::string GetRoutingCode() const { return m_routingCode; }

    /**
     * Get the last routing result.
     */
    AUTOROUTE_RESULT GetResult() const { return m_engine.GetResult(); }

private:
    AUTOROUTE_ENGINE  m_engine;
    AUTOROUTE_CONTROL m_control;
    std::string       m_routingCode;
};


#endif // AUTOROUTE_H
