/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2026, Zeo <team@zeo.dev>
 * Zeo modifications licensed AGPL-3.0-or-later (see /LICENSE.README).
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

#include <transline_calculations/coplanar.h>

#include <cmath>

/*
 * Coverage for the coplanar-waveguide model ported into the shared transline framework
 * (MOON-1456 / MOON-1459).  Verifies the physically-expected behaviour and, importantly, that
 * surface roughness now affects conductor loss *natively* (the calculator no longer scales it).
 */

using TCP = TRANSLINE_PARAMETERS;

namespace
{
struct CPW_OUT
{
    double z0, cond, diel;
};


CPW_OUT analyse( bool grounded, double w, double s, double h, double er, double f, double sigma,
                 double tand, double rough )
{
    COPLANAR cp( grounded );
    cp.SetParameter( TCP::PHYS_WIDTH, w );
    cp.SetParameter( TCP::PHYS_S, s );
    cp.SetParameter( TCP::T, 0.035e-3 );
    cp.SetParameter( TCP::H, h );
    cp.SetParameter( TCP::EPSILONR, er );
    cp.SetParameter( TCP::TAND, tand );
    cp.SetParameter( TCP::MURC, 1.0 );
    cp.SetParameter( TCP::ROUGH, rough );
    cp.SetParameter( TCP::FREQUENCY, f );
    cp.SetParameter( TCP::SIGMA, sigma );
    cp.SetParameter( TCP::PHYS_LEN, 1.0 );  // loss results are dB per metre
    cp.SetParameter( TCP::ANG_L, 1.0 );
    cp.Analyse();

    const auto& r = cp.GetAnalysisResults();
    auto        v = [&]( TCP k ) -> double
    {
        auto it = r.find( k );
        return it != r.end() ? it->second.first : NAN;
    };

    return { v( TCP::Z0 ), v( TCP::LOSS_CONDUCTOR ), v( TCP::LOSS_DIELECTRIC ) };
}

constexpr double W = 0.3e-3, S = 0.2e-3, HH = 0.2e-3, ER = 4.2;
constexpr double F = 1.0e9, SIGMA = 1.0 / 1.72e-8, TAND = 0.02;
} // namespace


BOOST_AUTO_TEST_SUITE( Coplanar )


// A back reference plane (grounded coplanar / CPWG) adds capacitance and lowers Z0 relative to
// a coplanar waveguide with no back plane.
BOOST_AUTO_TEST_CASE( GroundedLowerThanUngrounded )
{
    const CPW_OUT cpwg = analyse( true, W, S, HH, ER, F, SIGMA, TAND, 0.0 );
    const CPW_OUT cpw  = analyse( false, W, S, HH, ER, F, SIGMA, TAND, 0.0 );

    BOOST_CHECK_GT( cpwg.z0, 0.0 );
    BOOST_CHECK_GT( cpw.z0, cpwg.z0 );
}


// Widening the side gap reduces coupling to the coplanar grounds and raises Z0.
BOOST_AUTO_TEST_CASE( WiderGapRaisesImpedance )
{
    const CPW_OUT narrow = analyse( true, W, S, HH, ER, F, SIGMA, TAND, 0.0 );
    const CPW_OUT wide   = analyse( true, W, S * 3.0, HH, ER, F, SIGMA, TAND, 0.0 );

    BOOST_CHECK_GT( wide.z0, narrow.z0 );
}


// Surface roughness is applied inside the model: it raises conductor loss but leaves the
// dielectric loss untouched.  (Regression guard for the native-roughness change in MOON-1459.)
BOOST_AUTO_TEST_CASE( NativeRoughnessRaisesConductorLoss )
{
    const CPW_OUT smooth = analyse( true, W, S, HH, ER, F, SIGMA, TAND, 0.0 );
    const CPW_OUT rough  = analyse( true, W, S, HH, ER, F, SIGMA, TAND, 1.0e-6 );

    BOOST_CHECK_GT( smooth.cond, 0.0 );
    BOOST_CHECK_GT( smooth.diel, 0.0 );
    BOOST_CHECK_GT( rough.cond, smooth.cond );
    BOOST_CHECK_CLOSE( rough.diel, smooth.diel, 1e-6 );
}


BOOST_AUTO_TEST_SUITE_END()
