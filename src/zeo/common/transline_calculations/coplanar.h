/*
 * Coplanar waveguide calculation, ported into the shared transline-calculation framework.
 *
 * Original quasi-static / dispersion model:
 *   Copyright (C) 2008 Michael Margraf <michael.margraf@alumni.tu-berlin.de>
 *   Copyright (C) 2005, 2006 Stefan Jahn <stefan@lkcc.org>
 * Modified for KiCad: 2011 Jean-Pierre Charras
 * Copyright (C) 2026, Zeo <team@zeo.dev>
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

#ifndef TRANSLINE_CALCULATIONS_COPLANAR_H
#define TRANSLINE_CALCULATIONS_COPLANAR_H

#include <transline_calculations/transline_calculation_base.h>


/**
 * Coplanar waveguide.
 *
 * A signal trace of width PHYS_WIDTH with coplanar ground copper on both sides separated
 * by a gap PHYS_S, on a substrate of height H / permittivity EPSILONR.  When constructed
 * with aBackMetal = true this models a grounded coplanar waveguide (CPWG / GCPW): an
 * additional reference plane on the far side of the substrate.  Both flavours share the
 * same quasi-static + dispersion formulation.
 */
class COPLANAR : public TRANSLINE_CALCULATION_BASE
{
    using TCP = TRANSLINE_PARAMETERS;

public:
    /// @param aBackMetal true for grounded coplanar (reference plane on the substrate back).
    explicit COPLANAR( bool aBackMetal );

    /// Analyse track geometry parameters to output Z0 and Ang_L
    void Analyse() override;

    /// Synthesise track geometry parameters to match a given Z0
    bool Synthesize( SYNTHESIZE_OPTS aOpts ) override;

private:
    void SetAnalysisResults() override;
    void SetSynthesisResults() override;

    bool   m_backMetal;            ///< true → grounded coplanar (CPWG)
    double m_unitPropDelay = 0.0;  ///< ps/cm, cached from the last Analyse()
};


#endif  // TRANSLINE_CALCULATIONS_COPLANAR_H
