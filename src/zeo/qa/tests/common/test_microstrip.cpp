/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2026, Zeo <team@zeo.dev>
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

#include <boost/test/unit_test.hpp>

#include <transline_calculations/microstrip.h>

#include <cmath>

/*
 * Coverage for the microstrip loss-result contract relied on by IMPEDANCE_CALCULATOR.
 *
 * The MICROSTRIP model reports conductor / dielectric loss under ATTEN_COND / ATTEN_DILECTRIC
 * (NOT LOSS_CONDUCTOR / LOSS_DIELECTRIC, which is what STRIPLINE / COPLANAR use).  The impedance
 * calculator's analyseMicrostrip() must read the ATTEN_* keys, otherwise the per-track insertion
 * loss silently reads zero for every outer-layer (microstrip) trace.  These guard that contract.
 */

using TCP = TRANSLINE_PARAMETERS;

namespace
{
struct MS_OUT
{
    double z0, cond, diel;
};


MS_OUT analyse( double w, double h, double t, double er, double f, double sigma, double tand,
                double rough )
{
    MICROSTRIP ms;
    ms.SetParameter( TCP::PHYS_WIDTH, w );
    ms.SetParameter( TCP::T, t );
    ms.SetParameter( TCP::H, h );
    ms.SetParameter( TCP::H_T, 1e6 );  // free air above; effectively infinite
    ms.SetParameter( TCP::EPSILONR, er );
    ms.SetParameter( TCP::MUR, 1.0 );
    ms.SetParameter( TCP::MURC, 1.0 );
    ms.SetParameter( TCP::FREQUENCY, f );
    ms.SetParameter( TCP::TAND, tand );
    ms.SetParameter( TCP::ROUGH, rough );
    ms.SetParameter( TCP::SIGMA, sigma );
    ms.SetParameter( TCP::PHYS_LEN, 1.0 );  // loss results are therefore dB per metre
    ms.SetParameter( TCP::ANG_L, 1.0 );
    ms.Analyse();

    const auto& r = ms.GetAnalysisResults();
    auto        v = [&]( TCP k ) -> double
    {
        auto it = r.find( k );
        return it != r.end() ? it->second.first : NAN;
    };

    return { v( TCP::Z0 ), v( TCP::ATTEN_COND ), v( TCP::ATTEN_DILECTRIC ) };
}

// Representative ~50 Ω outer-layer microstrip (metres).
constexpr double W = 0.36e-3, H = 0.2e-3, T = 0.035e-3, ER = 4.2;
constexpr double F = 1.0e9, SIGMA = 1.0 / 1.72e-8, TAND = 0.02;
} // namespace


BOOST_AUTO_TEST_SUITE( Microstrip )


// Conductor + dielectric loss must be reported (and under ATTEN_COND / ATTEN_DILECTRIC).
// Regression guard: a zero here means analyseMicrostrip() is reading the wrong result keys and
// the per-track insertion loss has silently dropped to "—" for microstrip traces.
BOOST_AUTO_TEST_CASE( EmitsConductorAndDielectricLoss )
{
    const MS_OUT r = analyse( W, H, T, ER, F, SIGMA, TAND, 0.0 );

    BOOST_CHECK_GT( r.z0, 0.0 );
    BOOST_CHECK_GT( r.cond, 0.0 );
    BOOST_CHECK_GT( r.diel, 0.0 );
}


// Surface roughness raises conductor loss (applied to R_s inside the model) and leaves the
// dielectric loss untouched.
BOOST_AUTO_TEST_CASE( RoughnessRaisesConductorLoss )
{
    const MS_OUT smooth = analyse( W, H, T, ER, F, SIGMA, TAND, 0.0 );
    const MS_OUT rough  = analyse( W, H, T, ER, F, SIGMA, TAND, 1.0e-6 );

    BOOST_CHECK_GT( rough.cond, smooth.cond );
    BOOST_CHECK_CLOSE( rough.diel, smooth.diel, 1e-6 );
}


BOOST_AUTO_TEST_SUITE_END()
