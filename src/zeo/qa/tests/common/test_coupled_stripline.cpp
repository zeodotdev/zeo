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

#include <transline_calculations/coupled_stripline.h>

#include <cmath>
#include <optional>

/*
 * Coverage for the offset-capable coupled-stripline model (MOON-1458).
 *
 * The model generalises Cohn's symmetric shielded coupled-strip formulation to a strip that is
 * offset between its two reference planes, using the image / partial-capacitance method (the
 * same decomposition the single STRIPLINE model already uses).  It must:
 *   - reduce *exactly* to the symmetric result when the strip is centred (regression);
 *   - move the impedance the right way when the strip is offset toward a plane;
 *   - emit native odd/even conductor + dielectric loss, roughness-corrected.
 */

using TCP = TRANSLINE_PARAMETERS;

namespace
{
struct CSTRIP_OUT
{
    double z0e, z0o, zdiff;
    double condE, condO, dielE, dielO;
};


/// Configure + analyse a coupled stripline.  @p aOffset > 0 sets STRIPLINE_A (offset from the
/// near plane); std::nullopt leaves it unset (model treats the strip as centred).
CSTRIP_OUT analyse( double w, double s, double t, double H, double er, double f, double sigma,
                    double tand, double rough, std::optional<double> aOffset )
{
    COUPLED_STRIPLINE cs;
    cs.SetParameter( TCP::PHYS_WIDTH, w );
    cs.SetParameter( TCP::PHYS_S, s );
    cs.SetParameter( TCP::T, t );
    cs.SetParameter( TCP::H, H );
    cs.SetParameter( TCP::EPSILONR, er );
    cs.SetParameter( TCP::FREQUENCY, f );
    cs.SetParameter( TCP::SIGMA, sigma );
    cs.SetParameter( TCP::MURC, 1.0 );
    cs.SetParameter( TCP::TAND, tand );
    cs.SetParameter( TCP::ROUGH, rough );
    cs.SetParameter( TCP::PHYS_LEN, 1.0 );  // loss results are therefore dB per metre
    cs.SetParameter( TCP::ANG_L, 1.0 );

    if( aOffset )
        cs.SetParameter( TCP::STRIPLINE_A, *aOffset );

    cs.Analyse();

    const auto& r = cs.GetAnalysisResults();
    auto        v = [&]( TCP k ) -> double
    {
        auto it = r.find( k );
        return it != r.end() ? it->second.first : NAN;
    };

    return { v( TCP::Z0_E ),           v( TCP::Z0_O ),           v( TCP::Z_DIFF ),
             v( TCP::ATTEN_COND_EVEN ), v( TCP::ATTEN_COND_ODD ), v( TCP::ATTEN_DILECTRIC_EVEN ),
             v( TCP::ATTEN_DILECTRIC_ODD ) };
}

// A representative tightly-coupled inner-layer differential pair (metres).
constexpr double W = 0.2e-3, S = 0.2e-3, T = 0.035e-3, H = 0.4e-3, ER = 4.2;
constexpr double F = 1.0e9, SIGMA = 1.0 / 1.72e-8, TAND = 0.02;
} // namespace


BOOST_AUTO_TEST_SUITE( CoupledStripline )


// Explicitly centring the strip (STRIPLINE_A = (H-t)/2) must reproduce the unset/symmetric
// result exactly — the offset code path collapses to the original Cohn model.
BOOST_AUTO_TEST_CASE( OffsetReducesToSymmetricWhenCentred )
{
    const CSTRIP_OUT sym = analyse( W, S, T, H, ER, F, SIGMA, TAND, 0.0, std::nullopt );
    const CSTRIP_OUT ctr = analyse( W, S, T, H, ER, F, SIGMA, TAND, 0.0, ( H - T ) / 2.0 );

    BOOST_CHECK_CLOSE( ctr.z0e, sym.z0e, 1e-4 );
    BOOST_CHECK_CLOSE( ctr.z0o, sym.z0o, 1e-4 );

    // Sanity: positive, finite, and odd-mode below even-mode for an edge-coupled pair.
    BOOST_CHECK_GT( sym.z0o, 0.0 );
    BOOST_CHECK_LT( sym.z0o, sym.z0e );
    BOOST_CHECK_CLOSE( sym.zdiff, 2.0 * sym.z0o, 1e-9 );
}


// Moving the strip toward one plane increases its capacitance and so lowers both mode
// impedances — and the result must differ measurably from the centred case.
BOOST_AUTO_TEST_CASE( OffsetTowardPlaneLowersImpedance )
{
    const CSTRIP_OUT ctr = analyse( W, S, T, H, ER, F, SIGMA, TAND, 0.0, std::nullopt );
    const CSTRIP_OUT off = analyse( W, S, T, H, ER, F, SIGMA, TAND, 0.0, 0.05e-3 );

    BOOST_CHECK_GT( off.z0o, 0.0 );
    BOOST_CHECK_LT( off.z0o, ctr.z0o );
    BOOST_CHECK_LT( off.z0e, ctr.z0e );

    // "Measurably" different — well beyond rounding.
    BOOST_CHECK_GT( ( ctr.z0o - off.z0o ) / ctr.z0o, 0.05 );
    BOOST_CHECK_CLOSE( off.zdiff, 2.0 * off.z0o, 1e-9 );
}


// Native loss: conductor + dielectric loss are emitted, roughness raises conductor loss, and
// the lossier odd mode (lower Z0) exceeds the even mode.
BOOST_AUTO_TEST_CASE( NativeLossAndRoughness )
{
    const CSTRIP_OUT smooth = analyse( W, S, T, H, ER, F, SIGMA, TAND, 0.0, std::nullopt );
    const CSTRIP_OUT rough  = analyse( W, S, T, H, ER, F, SIGMA, TAND, 1.0e-6, std::nullopt );

    BOOST_CHECK_GT( smooth.condO, 0.0 );
    BOOST_CHECK_GT( smooth.dielO, 0.0 );
    BOOST_CHECK_GT( smooth.condE, 0.0 );

    // Odd-mode conductor loss exceeds even-mode (alpha_c ~ 1/Z0, Z0_odd < Z0_even).
    BOOST_CHECK_GT( smooth.condO, smooth.condE );

    // Surface roughness increases conductor loss; dielectric loss is unchanged by it.
    BOOST_CHECK_GT( rough.condO, smooth.condO );
    BOOST_CHECK_CLOSE( rough.dielO, smooth.dielO, 1e-6 );

    // Dielectric loss is the same for both modes (homogeneous medium).
    BOOST_CHECK_CLOSE( smooth.dielO, smooth.dielE, 1e-6 );
}


// With a lossless dielectric and infinite conductivity the model still produces a valid,
// positive impedance (loss terms simply vanish).
BOOST_AUTO_TEST_CASE( LosslessIsWellFormed )
{
    const CSTRIP_OUT r = analyse( W, S, T, H, ER, F, SIGMA, 0.0, 0.0, std::nullopt );

    BOOST_CHECK_GT( r.z0o, 0.0 );
    BOOST_CHECK_SMALL( r.dielO, 1e-12 );
}


BOOST_AUTO_TEST_SUITE_END()
