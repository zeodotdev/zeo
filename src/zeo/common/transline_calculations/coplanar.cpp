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

#include <transline_calculations/coplanar.h>
#include <transline_calculations/units.h>

#include <cmath>


namespace TC = TRANSLINE_CALCULATIONS;
using TCP = TRANSLINE_PARAMETERS;


COPLANAR::COPLANAR( bool aBackMetal ) :
        TRANSLINE_CALCULATION_BASE( { TCP::EPSILONR, TCP::TAND, TCP::H, TCP::T, TCP::PHYS_WIDTH,
                                      TCP::PHYS_S, TCP::FREQUENCY, TCP::SIGMA, TCP::MURC, TCP::ROUGH,
                                      TCP::PHYS_LEN, TCP::ANG_L, TCP::Z0, TCP::EPSILON_EFF,
                                      TCP::LOSS_CONDUCTOR, TCP::LOSS_DIELECTRIC, TCP::SKIN_DEPTH } ),
        m_backMetal( aBackMetal )
{
}


void COPLANAR::Analyse()
{
    SetParameter( TCP::SKIN_DEPTH, SkinDepth() );

    const double W   = GetParameter( TCP::PHYS_WIDTH );
    const double s   = GetParameter( TCP::PHYS_S );
    const double h   = GetParameter( TCP::H );
    const double t   = GetParameter( TCP::T );
    const double er  = GetParameter( TCP::EPSILONR );
    const double f   = GetParameter( TCP::FREQUENCY );
    const double len = GetParameter( TCP::PHYS_LEN );

    // K(k) — complete elliptic integral of the first kind.
    auto ellipk = []( double k ) { return EllipticIntegral( k ).first; };

    // quasi-static constants
    double k1, kk1, kpk1, k2, k3, q1, q2, q3 = 0, qz, er0 = 0;
    double zl_factor;

    k1   = W / ( W + s + s );
    kk1  = ellipk( k1 );
    kpk1 = ellipk( std::sqrt( 1 - k1 * k1 ) );
    q1   = kk1 / kpk1;

    if( m_backMetal )
    {
        // grounded coplanar — reference plane on the substrate back
        k3 = std::tanh( ( M_PI / 4 ) * ( W / h ) )
             / std::tanh( ( M_PI / 4 ) * ( W + s + s ) / h );
        q3        = ellipk( k3 ) / ellipk( std::sqrt( 1 - k3 * k3 ) );
        qz        = 1 / ( q1 + q3 );
        er0       = 1 + q3 * qz * ( er - 1 );
        zl_factor = TC::ZF0 / 2 * qz;
    }
    else
    {
        // backside is air
        k2 = std::sinh( ( M_PI / 4 ) * ( W / h ) )
             / std::sinh( ( M_PI / 4 ) * ( W + s + s ) / h );
        q2        = ellipk( k2 ) / ellipk( std::sqrt( 1 - k2 * k2 ) );
        er0       = 1 + ( er - 1 ) / 2 * q2 / q1;
        zl_factor = TC::ZF0 / 4 / q1;
    }

    // effect of strip thickness
    if( t > 0 )
    {
        double d, se, We, ke, qe;
        d  = ( t * 1.25 / M_PI ) * ( 1 + std::log( 4 * M_PI * W / t ) );
        se = s - d;
        We = W + d;

        ke = We / ( We + se + se );
        qe = ellipk( ke ) / ellipk( std::sqrt( 1 - ke * ke ) );

        if( m_backMetal )
        {
            qz        = 1 / ( qe + q3 );
            er0       = 1 + q3 * qz * ( er - 1 );
            zl_factor = TC::ZF0 / 2 * qz;
        }
        else
        {
            zl_factor = TC::ZF0 / 4 / qe;
        }

        er0 = er0 - ( 0.7 * ( er0 - 1 ) * t / s ) / ( q1 + ( 0.7 * t / s ) );
    }

    const double sr_er  = std::sqrt( er );
    const double sr_er0 = std::sqrt( er0 );

    // cut-off frequency of the TE0 mode + dispersion factor G
    const double fte = ( TC::C0 / 4 ) / ( h * std::sqrt( er - 1 ) );
    const double p   = std::log( W / h );
    const double u   = 0.54 - ( 0.64 - 0.015 * p ) * p;
    const double v   = 0.43 - ( 0.86 - 0.54 * p ) * p;
    const double G   = std::exp( u * std::log( W / s ) + v );

    // conductor-loss factor (Ghione), strip-thickness dependent
    double ac = 0;

    if( t > 0 )
    {
        const double n = ( 1 - k1 ) * 8 * M_PI / ( t * ( 1 + k1 ) );
        const double a = W / 2;
        const double b = a + s;
        ac             = ( M_PI + std::log( n * a ) ) / a + ( M_PI + std::log( n * b ) ) / b;
    }

    const double ac_factor = ac / ( 4 * TC::ZF0 * kk1 * kpk1 * ( 1 - k1 * k1 ) );
    const double ad_factor = ( er / ( er - 1 ) ) * GetParameter( TCP::TAND ) * M_PI / TC::C0;

    // add dispersive effects to the effective permittivity
    double sr_er_f = sr_er0;
    sr_er_f += ( sr_er - sr_er0 ) / ( 1 + G * std::pow( f / fte, -1.8 ) );

    SetParameter( TCP::LOSS_CONDUCTOR, TC::LOG2DB * len * ac_factor * sr_er0
                                               * std::sqrt( M_PI * TC::MU0 * f
                                                            / GetParameter( TCP::SIGMA ) ) );
    SetParameter( TCP::LOSS_DIELECTRIC, TC::LOG2DB * len * ad_factor * f
                                                * ( sr_er_f * sr_er_f - 1 ) / sr_er_f );

    SetParameter( TCP::ANG_L, 2.0 * M_PI * len * sr_er_f * f / TC::C0 );  // radians
    SetParameter( TCP::EPSILON_EFF, sr_er_f * sr_er_f );
    SetParameter( TCP::Z0, zl_factor / sr_er_f );

    m_unitPropDelay = UnitPropagationDelay( GetParameter( TCP::EPSILON_EFF ) );
}


bool COPLANAR::Synthesize( const SYNTHESIZE_OPTS /* aOpts */ )
{
    // Solve for the gap that hits the target Z0 (width fixed).  Not used by the impedance
    // DRC path yet, but implemented for completeness / future width-gap synthesis.
    return MinimiseZ0Error1D( TCP::PHYS_S, TCP::Z0 );
}


void COPLANAR::SetAnalysisResults()
{
    SetAnalysisResult( TCP::EPSILON_EFF, GetParameter( TCP::EPSILON_EFF ) );
    SetAnalysisResult( TCP::UNIT_PROP_DELAY, m_unitPropDelay );
    SetAnalysisResult( TCP::LOSS_CONDUCTOR, GetParameter( TCP::LOSS_CONDUCTOR ) );
    SetAnalysisResult( TCP::LOSS_DIELECTRIC, GetParameter( TCP::LOSS_DIELECTRIC ) );
    SetAnalysisResult( TCP::SKIN_DEPTH, GetParameter( TCP::SKIN_DEPTH ) );

    const double Z0    = GetParameter( TCP::Z0 );
    const double ANG_L = GetParameter( TCP::ANG_L );
    const double S     = GetParameter( TCP::PHYS_S );
    const double W     = GetParameter( TCP::PHYS_WIDTH );

    const bool Z0_invalid = !std::isfinite( Z0 ) || Z0 <= 0;
    const bool S_invalid  = !std::isfinite( S ) || S <= 0;
    const bool W_invalid  = !std::isfinite( W ) || W <= 0;

    SetAnalysisResult( TCP::Z0, Z0, Z0_invalid ? TRANSLINE_STATUS::TS_ERROR : TRANSLINE_STATUS::OK );
    SetAnalysisResult( TCP::ANG_L, ANG_L,
                       !std::isfinite( ANG_L ) ? TRANSLINE_STATUS::TS_ERROR : TRANSLINE_STATUS::OK );
    SetAnalysisResult( TCP::PHYS_S, S, S_invalid ? TRANSLINE_STATUS::WARNING : TRANSLINE_STATUS::OK );
    SetAnalysisResult( TCP::PHYS_WIDTH, W,
                       W_invalid ? TRANSLINE_STATUS::WARNING : TRANSLINE_STATUS::OK );
}


void COPLANAR::SetSynthesisResults()
{
    SetSynthesisResult( TCP::EPSILON_EFF, GetParameter( TCP::EPSILON_EFF ) );
    SetSynthesisResult( TCP::LOSS_CONDUCTOR, GetParameter( TCP::LOSS_CONDUCTOR ) );
    SetSynthesisResult( TCP::LOSS_DIELECTRIC, GetParameter( TCP::LOSS_DIELECTRIC ) );
    SetSynthesisResult( TCP::SKIN_DEPTH, GetParameter( TCP::SKIN_DEPTH ) );
    SetSynthesisResult( TCP::PHYS_S, GetParameter( TCP::PHYS_S ) );
    SetSynthesisResult( TCP::PHYS_WIDTH, GetParameter( TCP::PHYS_WIDTH ) );
    SetSynthesisResult( TCP::Z0, GetParameter( TCP::Z0 ) );
    SetSynthesisResult( TCP::ANG_L, GetParameter( TCP::ANG_L ) );
}
