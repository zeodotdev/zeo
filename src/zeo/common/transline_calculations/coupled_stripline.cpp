/*
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
 * along with this package; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street - Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/*
 * This implements the calculations described in:
 *
 * [1] S. B. Cohn, "Characteristic Impedance of the Shielded-Strip Transmission Line," in Transactions of the IRE
 *    Professional Group on Microwave Theory and Techniques, vol. 2, no. 2, pp. 52-57, July 1954
 * [2] S. B. Cohn, "Shielded Coupled-Strip Transmission Line," in IRE Transactions on Microwave Theory and Techniques,
 *    vol. 3, no. 5, pp. 29-38, October 1955
 */

#include <transline_calculations/coupled_stripline.h>
#include <transline_calculations/units.h>
#include <transline_calculations/units_scales.h>


namespace TC = TRANSLINE_CALCULATIONS;
using TCP = TRANSLINE_PARAMETERS;


void COUPLED_STRIPLINE::Analyse()
{
    // Calculate skin depth
    SetParameter( TCP::SKIN_DEPTH, SkinDepth() );

    // Get analysis parameters
    const double w  = GetParameter( TCP::PHYS_WIDTH );
    const double t  = GetParameter( TCP::T );
    const double s  = GetParameter( TCP::PHYS_S );
    const double H  = GetParameter( TCP::H );
    const double er = GetParameter( TCP::EPSILONR );

    // STRIPLINE_A is the gap from one reference plane to the strip.  When unset (0) or out of
    // range the strip is treated as centred between the planes — reproducing the symmetric
    // Cohn result of the original model exactly.
    double     a      = GetParameter( TCP::STRIPLINE_A );
    const bool offset = ( a > 0.0 && ( a + t ) < H );

    if( !offset )
        a = ( H - t ) / 2.0;

    if( offset )
    {
        // Image / partial-capacitance method: an offset stripline is the parallel combination
        // of two symmetric striplines, each formed by mirroring the strip about one plane
        // (ground-plane separations 2a+t for the near plane and 2(H-a)-t for the far plane).
        // Applied independently to the even- and odd-mode admittances.  Reduces exactly to the
        // symmetric Cohn result when a = (H-t)/2 (h1 = h2 = H).
        const double h1 = 2.0 * a + t;
        const double h2 = 2.0 * ( H - a ) - t;

        double z0e1, z0o1, z0e2, z0o2;
        calcSymmetricCoupled( h1, w, s, t, er, z0e1, z0o1 );
        calcSymmetricCoupled( h2, w, s, t, er, z0e2, z0o2 );

        const double z0e = 2.0 / ( 1.0 / z0e1 + 1.0 / z0e2 );
        const double z0o = 2.0 / ( 1.0 / z0o1 + 1.0 / z0o2 );

        SetParameter( TCP::Z0_E, z0e );
        SetParameter( TCP::Z0_O, z0o );
        SetParameter( TCP::Z_DIFF, 2.0 * z0o );
    }
    else
    {
        double z0e, z0o;
        calcSymmetricCoupled( H, w, s, t, er, z0e, z0o );

        SetParameter( TCP::Z0_E, z0e );
        SetParameter( TCP::Z0_O, z0o );
        SetParameter( TCP::Z_DIFF, 2.0 * z0o );
    }

    calcLosses( a );
    calcDielectrics();
}


void COUPLED_STRIPLINE::calcSymmetricCoupled( const double h, const double w, const double s,
                                              const double t, const double er, double& aZ0e,
                                              double& aZ0o )
{
    calcZeroThicknessCoupledImpedances( h, w, s, er );

    // Infinitely thin line — the zero-thickness coupled impedances are the answer directly.
    if( t == 0.0 )
    {
        aZ0e = Z0_e_w_h_0_s_h;
        aZ0o = Z0_o_w_h_0_s_h;
        return;
    }

    calcSingleStripImpedances( h );
    calcFringeCapacitances( h, t, er );
    aZ0e = calcZ0EvenMode();
    aZ0o = calcZ0OddMode( t, s );
}


bool COUPLED_STRIPLINE::Synthesize( const SYNTHESIZE_OPTS aOpts )
{
    if( aOpts == SYNTHESIZE_OPTS::FIX_WIDTH )
        return MinimiseZ0Error1D( TCP::PHYS_S, TCP::Z0_O, false );

    if( aOpts == SYNTHESIZE_OPTS::FIX_SPACING )
        return MinimiseZ0Error1D( TCP::PHYS_WIDTH, TCP::Z0_O, false );

    // This synthesis approach is modified from wcalc, which is released under GPL version 2
    // Copyright (C) 1999, 2000, 2001, 2002, 2003, 2004, 2006 Dan McMahill
    // All rights reserved

    double ze0 = 0;
    double zo0 = 0;

    const double h = GetParameter( TCP::H );
    const double er = GetParameter( TCP::EPSILONR );

    const double z0e_target = GetParameter( TCP::Z0_E );
    const double z0o_target = GetParameter( TCP::Z0_O );
    // Calculate Z0 and coupling, k
    const double z0 = sqrt( z0e_target * z0o_target );
    const double k = ( z0e_target - z0o_target ) / ( z0e_target + z0o_target );

    int maxiters = 50;

    // Initial guess at a solution. Note that this is an initial guess for coupled microstrip, not coupled stripline...
    static constexpr double ai[] = { 1, -0.301, 3.209, -27.282, 56.609, -37.746 };
    static constexpr double bi[] = { 0.020, -0.623, 17.192, -68.946, 104.740, -16.148 };
    static constexpr double ci[] = { 0.002, -0.347, 7.171, -36.910, 76.132, -51.616 };

    const double AW = exp( z0 * sqrt( er + 1.0 ) / 42.4 ) - 1.0;
    const double F1 = 8.0 * sqrt( AW * ( 7.0 + 4.0 / er ) / 11.0 + ( 1.0 + 1.0 / er ) / 0.81 ) / AW;

    double F2 = 0.0, F3 = 0.0;
    ;

    for( int i = 0; i <= 5; i++ )
        F2 = F2 + ai[i] * pow( k, i );

    for( int i = 0; i <= 5; i++ )
        F3 = F3 + ( bi[i] - ci[i] * ( 9.6 - er ) ) * pow( ( 0.6 - k ), static_cast<double>( i ) );

    double w = h * fabs( F1 * F2 );
    double s = h * fabs( F1 * F3 );

    int    iters = 0;
    bool   done = false;
    double delta = 0.0;

    delta = TC::UNIT_MIL * 1e-5;

    const double cval = 1e-12 * z0e_target * z0o_target;

    while( !done && iters < maxiters )
    {
        iters++;

        // Compute impedances with initial solution guess
        SetParameter( TCP::PHYS_WIDTH, w );
        SetParameter( TCP::PHYS_S, s );
        Analyse();

        // Check for convergence
        ze0 = GetParameter( TCP::Z0_E );
        zo0 = GetParameter( TCP::Z0_O );
        const double err = pow( ( ze0 - z0e_target ), 2.0 ) + pow( ( zo0 - z0o_target ), 2.0 );

        if( err < cval )
        {
            done = true;
        }
        else
        {
            // Approximate the first Jacobian
            SetParameter( TCP::PHYS_WIDTH, w + delta );
            SetParameter( TCP::PHYS_S, s );
            Analyse();

            const double ze1 = GetParameter( TCP::Z0_E );
            const double zo1 = GetParameter( TCP::Z0_O );

            SetParameter( TCP::PHYS_WIDTH, w );
            SetParameter( TCP::PHYS_S, s + delta );
            Analyse();

            const double ze2 = GetParameter( TCP::Z0_E );
            const double zo2 = GetParameter( TCP::Z0_O );

            const double dedw = ( ze1 - ze0 ) / delta;
            const double dodw = ( zo1 - zo0 ) / delta;
            const double deds = ( ze2 - ze0 ) / delta;
            const double dods = ( zo2 - zo0 ) / delta;

            // Find the determinate
            const double d = dedw * dods - deds * dodw;

            // Estimate the new solution, but don't change by more than 10% at a time to avoid convergence problems
            double dw = -1.0 * ( ( ze0 - z0e_target ) * dods - ( zo0 - z0o_target ) * deds ) / d;

            if( fabs( dw ) > 0.1 * w )
            {
                if( dw > 0.0 )
                    dw = 0.1 * w;
                else
                    dw = -0.1 * w;
            }

            w = fabs( w + dw );

            double ds = ( ( ze0 - z0e_target ) * dodw - ( zo0 - z0o_target ) * dedw ) / d;

            if( fabs( ds ) > 0.1 * s )
            {
                if( ds > 0.0 )
                    ds = 0.1 * s;
                else
                    ds = -0.1 * s;
            }

            s = fabs( s + ds );
        }
    }

    if( !done )
        return false;

    // Recompute with the final parameters
    SetParameter( TCP::PHYS_WIDTH, w );
    SetParameter( TCP::PHYS_S, s );
    Analyse();

    // Reset the impedances
    SetParameter( TCP::Z0_E, z0e_target );
    SetParameter( TCP::Z0_O, z0o_target );

    return true;
}


void COUPLED_STRIPLINE::SetAnalysisResults()
{
    SetAnalysisResult( TCP::EPSILON_EFF_EVEN, e_eff_e );
    SetAnalysisResult( TCP::EPSILON_EFF_ODD, e_eff_o );
    SetAnalysisResult( TCP::UNIT_PROP_DELAY_EVEN, unit_prop_delay_e );
    SetAnalysisResult( TCP::UNIT_PROP_DELAY_ODD, unit_prop_delay_o );
    SetAnalysisResult( TCP::SKIN_DEPTH, GetParameter( TCP::SKIN_DEPTH ) );
    SetAnalysisResult( TCP::ATTEN_COND_EVEN, atten_cond_e );
    SetAnalysisResult( TCP::ATTEN_COND_ODD, atten_cond_o );
    SetAnalysisResult( TCP::ATTEN_DILECTRIC_EVEN, atten_diel_e );
    SetAnalysisResult( TCP::ATTEN_DILECTRIC_ODD, atten_diel_o );

    const double Z0_E = GetParameter( TCP::Z0_E );
    const double Z0_O = GetParameter( TCP::Z0_O );
    const double Z_DIFF = GetParameter( TCP::Z_DIFF );
    const double W = GetParameter( TCP::PHYS_WIDTH );
    const double L = GetParameter( TCP::PHYS_LEN );
    const double S = GetParameter( TCP::PHYS_S );

    const bool Z0_E_invalid = !std::isfinite( Z0_E ) || Z0_E <= 0;
    const bool Z0_O_invalid = !std::isfinite( Z0_O ) || Z0_O <= 0;
    const bool Z_DIFF_invalid = !std::isfinite( Z_DIFF ) || Z_DIFF <= 0;
    const bool ANG_L_invalid = !std::isfinite( ang_l ) || ang_l < 0;
    const bool W_invalid = !std::isfinite( W ) || W <= 0;
    const bool L_invalid = !std::isfinite( L ) || L < 0;
    const bool S_invalid = !std::isfinite( S ) || S <= 0;

    SetAnalysisResult( TCP::Z0_E, Z0_E, Z0_E_invalid ? TRANSLINE_STATUS::TS_ERROR : TRANSLINE_STATUS::OK );
    SetAnalysisResult( TCP::Z0_O, Z0_O, Z0_O_invalid ? TRANSLINE_STATUS::TS_ERROR : TRANSLINE_STATUS::OK );
    SetAnalysisResult( TCP::Z_DIFF, Z_DIFF, Z_DIFF_invalid ? TRANSLINE_STATUS::TS_ERROR : TRANSLINE_STATUS::OK );
    SetAnalysisResult( TCP::ANG_L, ang_l, ANG_L_invalid ? TRANSLINE_STATUS::TS_ERROR : TRANSLINE_STATUS::OK );
    SetAnalysisResult( TCP::PHYS_WIDTH, W, W_invalid ? TRANSLINE_STATUS::WARNING : TRANSLINE_STATUS::OK );
    SetAnalysisResult( TCP::PHYS_LEN, L, L_invalid ? TRANSLINE_STATUS::WARNING : TRANSLINE_STATUS::OK );
    SetAnalysisResult( TCP::PHYS_S, S, S_invalid ? TRANSLINE_STATUS::WARNING : TRANSLINE_STATUS::OK );
}


void COUPLED_STRIPLINE::SetSynthesisResults()
{
    SetSynthesisResult( TCP::EPSILON_EFF_EVEN, e_eff_e );
    SetSynthesisResult( TCP::EPSILON_EFF_ODD, e_eff_o );
    SetSynthesisResult( TCP::UNIT_PROP_DELAY_EVEN, unit_prop_delay_e );
    SetSynthesisResult( TCP::UNIT_PROP_DELAY_ODD, unit_prop_delay_o );
    SetSynthesisResult( TCP::SKIN_DEPTH, GetParameter( TCP::SKIN_DEPTH ) );

    const double Z0_E = GetParameter( TCP::Z0_E );
    const double Z0_O = GetParameter( TCP::Z0_O );
    const double Z_DIFF = GetParameter( TCP::Z_DIFF );
    const double W = GetParameter( TCP::PHYS_WIDTH );
    const double L = GetParameter( TCP::PHYS_LEN );
    const double S = GetParameter( TCP::PHYS_S );

    const bool Z0_E_invalid = !std::isfinite( Z0_E ) || Z0_E <= 0;
    const bool Z0_O_invalid = !std::isfinite( Z0_O ) || Z0_O <= 0;
    const bool Z_DIFF_invalid = !std::isfinite( Z_DIFF ) || Z_DIFF <= 0;
    const bool ANG_L_invalid = !std::isfinite( ang_l ) || ang_l < 0;
    const bool W_invalid = !std::isfinite( W ) || W <= 0;
    const bool L_invalid = !std::isfinite( L ) || L < 0;
    const bool S_invalid = !std::isfinite( S ) || S <= 0;

    SetSynthesisResult( TCP::Z0_E, Z0_E, Z0_E_invalid ? TRANSLINE_STATUS::WARNING : TRANSLINE_STATUS::OK );
    SetSynthesisResult( TCP::Z0_O, Z0_O, Z0_O_invalid ? TRANSLINE_STATUS::WARNING : TRANSLINE_STATUS::OK );
    SetSynthesisResult( TCP::Z_DIFF, Z_DIFF, Z_DIFF_invalid ? TRANSLINE_STATUS::WARNING : TRANSLINE_STATUS::OK );
    SetSynthesisResult( TCP::ANG_L, ang_l, ANG_L_invalid ? TRANSLINE_STATUS::WARNING : TRANSLINE_STATUS::OK );
    SetSynthesisResult( TCP::PHYS_WIDTH, W, W_invalid ? TRANSLINE_STATUS::TS_ERROR : TRANSLINE_STATUS::OK );
    SetSynthesisResult( TCP::PHYS_LEN, L, L_invalid ? TRANSLINE_STATUS::TS_ERROR : TRANSLINE_STATUS::OK );
    SetSynthesisResult( TCP::PHYS_S, S, S_invalid ? TRANSLINE_STATUS::TS_ERROR : TRANSLINE_STATUS::OK );
}


void COUPLED_STRIPLINE::calcFringeCapacitances( const double h, const double t, const double er )
{
    // Reference [1], Eq. 2
    C_f_t_h = ( TC::E0 * er / M_PI )
              * ( ( 2.0 / ( 1.0 - t / h ) ) * log( ( 1.0 / ( 1.0 - t / h ) ) + 1.0 )
                  - ( 1.0 / ( 1.0 - t / h ) - 1.0 ) * log( ( 1.0 / pow( 1.0 - t / h, 2.0 ) ) - 1.0 ) );

    // Reference [2], Eq. 13
    C_f_0 = ( TC::E0 * er / M_PI ) * 2.0 * log( 2.0 );
}


void COUPLED_STRIPLINE::calcZeroThicknessCoupledImpedances( const double h, const double w, const double s,
                                                            const double er )
{
    // Reference [2], Eqs. 2 - 7
    const double k_e = tanh( M_PI * w / ( 2.0 * h ) ) * tanh( M_PI * ( w + s ) / ( 2.0 * h ) );
    const double k_o = tanh( M_PI * w / ( 2.0 * h ) ) * coth( M_PI * ( w + s ) / ( 2.0 * h ) );
    const double k_e_p = std::sqrt( 1 - std::pow( k_e, 2 ) );
    const double k_o_p = std::sqrt( 1 - std::pow( k_o, 2 ) );
    Z0_e_w_h_0_s_h = ( TC::ZF0 / ( 4.0 * std::sqrt( er ) ) )
                     * ( EllipticIntegral( k_e_p ).first / EllipticIntegral( k_e ).first );
    Z0_o_w_h_0_s_h = ( TC::ZF0 / ( 4.0 * std::sqrt( er ) ) )
                     * ( EllipticIntegral( k_o_p ).first / EllipticIntegral( k_o ).first );
}


void COUPLED_STRIPLINE::calcSingleStripImpedances( const double h )
{
    const double er = GetParameter( TCP::EPSILONR );
    const double w = GetParameter( TCP::PHYS_WIDTH );

    // Finite-thickness single strip impedance (strip centred in separation h)
    Z0_w_h_t_h = calcZ0SymmetricStripline( h );

    // Zero-thickness single strip impedance
    // Reference [1], Eqs. 5 - 6 (corrected for sqrt(e_r))
    const double k = sech( M_PI * w / ( 2.0 * h ) );
    const double k_p = tanh( M_PI * w / ( 2.0 * h ) );
    Z0_w_h_0 =
            ( TC::ZF0 / ( 4.0 * std::sqrt( er ) ) ) * ( EllipticIntegral( k ).first / EllipticIntegral( k_p ).first );
}


double COUPLED_STRIPLINE::calcZ0EvenMode()
{
    // Reference [2], Eq. 18
    return 1.0 / ( ( 1.0 / Z0_w_h_t_h ) - ( C_f_t_h / C_f_0 ) * ( ( 1.0 / Z0_w_h_0 ) - ( 1.0 / Z0_e_w_h_0_s_h ) ) );
}


double COUPLED_STRIPLINE::calcZ0OddMode( const double t, const double s )
{
    // Reference [2], Eq. 20
    const double Z_o_1 =
            1.0 / ( ( 1.0 / Z0_w_h_t_h ) + ( C_f_t_h / C_f_0 ) * ( ( 1.0 / Z0_o_w_h_0_s_h ) - ( 1.0 / Z0_w_h_0 ) ) );

    // Reference [2], Eq. 22
    const double Z_o_2 =
            1.0
            / ( ( 1.0 / Z0_o_w_h_0_s_h ) + ( ( 1.0 / Z0_w_h_t_h ) - ( 1.0 / Z0_w_h_0 ) )
                - ( 2.0 / TC::ZF0 ) * ( C_f_t_h / TC::E0 - C_f_0 / TC::E0 ) + ( 2.0 * t ) / ( TC::ZF0 * s ) );

    return s / t >= 5.0 ? Z_o_1 : Z_o_2;
}


void COUPLED_STRIPLINE::calcLosses( const double a )
{
    const double er    = GetParameter( TCP::EPSILONR );
    const double t     = GetParameter( TCP::T );
    const double w     = GetParameter( TCP::PHYS_WIDTH );
    const double H     = GetParameter( TCP::H );
    const double f     = GetParameter( TCP::FREQUENCY );
    const double L     = GetParameter( TCP::PHYS_LEN );
    const double tand  = GetParameter( TCP::TAND );
    const double rough = GetParameter( TCP::ROUGH );
    const double sigma = GetParameter( TCP::SIGMA );

    // Dielectric loss (homogeneous medium → identical for even and odd modes), matching the
    // single-stripline formulation.  dB over PHYS_LEN.
    atten_diel_e = atten_diel_o = ( tand > 0.0 && f > 0.0 )
                                          ? TC::LOG2DB * L * ( M_PI / TC::C0 ) * f * std::sqrt( er ) * tand
                                          : 0.0;

    // Conductor loss: an isolated single stripline at the actual offset gives the baseline
    // attenuation (it already sums the loss to both reference planes); scale per mode by the
    // impedance ratio (alpha_c ∝ 1/Z0 for a fixed current distribution).  Roughness is applied
    // via the Hammerstad-Jensen correction.
    atten_cond_e = atten_cond_o = 0.0;

    if( sigma <= 0.0 || f <= 0.0 )
        return;

    m_striplineCalc.SetParameter( TCP::EPSILONR, er );
    m_striplineCalc.SetParameter( TCP::T, t );
    m_striplineCalc.SetParameter( TCP::STRIPLINE_A, a );
    m_striplineCalc.SetParameter( TCP::H, H );
    m_striplineCalc.SetParameter( TCP::PHYS_WIDTH, w );
    m_striplineCalc.SetParameter( TCP::PHYS_LEN, L );
    m_striplineCalc.SetParameter( TCP::FREQUENCY, f );
    m_striplineCalc.SetParameter( TCP::TAND, 0.0 );  // dielectric loss handled above
    m_striplineCalc.SetParameter( TCP::SIGMA, sigma );
    m_striplineCalc.SetParameter( TCP::MURC, GetParameter( TCP::MURC ) );
    m_striplineCalc.SetParameter( TCP::ANG_L, 0.0 );
    m_striplineCalc.Analyse();

    double       condSingle = m_striplineCalc.GetParameter( TCP::LOSS_CONDUCTOR );
    const double zSingle    = m_striplineCalc.GetParameter( TCP::Z0 );

    if( !std::isfinite( condSingle ) || condSingle < 0.0 || zSingle <= 0.0 )
        return;

    const double skin = GetParameter( TCP::SKIN_DEPTH );

    if( rough > 0.0 && skin > 0.0 )
    {
        const double r = rough / skin;
        condSingle *= 1.0 + ( 2.0 / M_PI ) * std::atan( 1.4 * r * r );
    }

    const double zE = GetParameter( TCP::Z0_E );
    const double zO = GetParameter( TCP::Z0_O );

    atten_cond_e = ( zE > 0.0 ) ? condSingle * ( zSingle / zE ) : condSingle;
    atten_cond_o = ( zO > 0.0 ) ? condSingle * ( zSingle / zO ) : condSingle;
}


void COUPLED_STRIPLINE::calcDielectrics()
{
    // We assume here that the dielectric is homogenous surrounding the strips - in this case, the odd or even modes
    // don't change the effective dielectric constant of the transmission mode. This would not be the case if the
    // dielectric were inhomogenous, as there is more electric field permeating the dielectric between the traces in
    // the odd mode compared to the even mode.
    const double e_r = GetParameter( TCP::EPSILONR );
    e_eff_e = e_r;
    e_eff_o = e_r;

    // Both modes have the same propagation delay
    const double unitPropDelay = UnitPropagationDelay( e_r );
    unit_prop_delay_e = unitPropDelay;
    unit_prop_delay_o = unitPropDelay;

    // Electrical length (in radians)
    const double v = TC::C0 / sqrt( e_r );
    const double lambda_g = v / GetParameter( TCP::FREQUENCY );
    ang_l = 2.0 * M_PI * GetParameter( TCP::PHYS_LEN ) / lambda_g;
}


double COUPLED_STRIPLINE::calcZ0SymmetricStripline( const double h )
{
    // Single strip centred in a cavity of ground-plane separation h (STRIPLINE_A = h/2).
    m_striplineCalc.SetParameter( TRANSLINE_PARAMETERS::EPSILONR, GetParameter( TCP::EPSILONR ) );
    m_striplineCalc.SetParameter( TRANSLINE_PARAMETERS::T, GetParameter( TCP::T ) );
    m_striplineCalc.SetParameter( TRANSLINE_PARAMETERS::STRIPLINE_A, h / 2.0 );
    m_striplineCalc.SetParameter( TRANSLINE_PARAMETERS::H, h );
    m_striplineCalc.SetParameter( TRANSLINE_PARAMETERS::PHYS_LEN, GetParameter( TCP::PHYS_LEN ) );
    m_striplineCalc.SetParameter( TRANSLINE_PARAMETERS::FREQUENCY, GetParameter( TCP::FREQUENCY ) );
    m_striplineCalc.SetParameter( TRANSLINE_PARAMETERS::TAND, 0.0 );
    m_striplineCalc.SetParameter( TRANSLINE_PARAMETERS::PHYS_WIDTH, GetParameter( TCP::PHYS_WIDTH ) );
    m_striplineCalc.SetParameter( TRANSLINE_PARAMETERS::ANG_L, 0 );
    m_striplineCalc.SetParameter( TRANSLINE_PARAMETERS::SIGMA, GetParameter( TCP::SIGMA ) );
    m_striplineCalc.SetParameter( TRANSLINE_PARAMETERS::MURC, GetParameter( TCP::MURC ) );
    m_striplineCalc.Analyse();

    return m_striplineCalc.GetParameter( TCP::Z0 );
}
