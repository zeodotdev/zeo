
/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2014 SoftPLC Corporation, Dick Hollenbeck <dick@softplc.com>
 * Copyright The KiCad Developers, see AUTHORS.TXT for contributors.
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
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#include <core/ignore.h>
#include <kiway.h>
#include <kiway_holder.h>
#include <project.h>

#if defined(DEBUG)
 #include <typeinfo>
#endif


PROJECT& KIWAY_HOLDER::Prj() const
{
    if( m_projectOverride )
        return *m_projectOverride;

    return Kiway().Prj();
}


void KIWAY_HOLDER::SetPrjOverride( PROJECT* aProject )
{
    if( m_projectOverride == aProject )
        return;

    if( m_projectOverride )
        m_projectOverride->RemoveDestroyHook( this );

    m_projectOverride = aProject;

    // If the overridden PROJECT is unloaded while this holder is still
    // alive, the destroy hook nulls our override — Prj() then falls
    // back to Kiway().Prj() (or the caller's next SetPrjOverride) rather
    // than dereferencing freed memory.
    if( m_projectOverride )
    {
        m_projectOverride->AddDestroyHook(
                this, [this]() { m_projectOverride = nullptr; } );
    }
}


KIWAY_HOLDER::~KIWAY_HOLDER()
{
    if( m_projectOverride )
        m_projectOverride->RemoveDestroyHook( this );
}


// this is not speed critical, hide it out of line.
void KIWAY_HOLDER::SetKiway( wxWindow* aDest, KIWAY* aKiway )
{
#if defined(DEBUG)
    // offer a trap point for debugging most any window
    wxASSERT( aDest );
    if( !strcmp( typeid(aDest).name(), "DIALOG_EDIT_LIBENTRY_FIELDS_IN_LIB" ) )
    {
        int breakhere=1;
        ignore_unused( breakhere );
    }
#endif

    ignore_unused( aDest );

    m_kiway = aKiway;
}
