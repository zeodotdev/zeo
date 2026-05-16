/*
 * Copyright (C) 2025, Zeo <team@zeo.dev>
 */

#include <kiface_base.h>
#include <pgm_base.h>
#include <kiway.h>
#include "agent_frame.h"
#include <cstdio>
#include <wx/app.h>
#include <wx/weakref.h>

// The KIFACE implementation
class KIFACE_AGENT : public KIFACE_BASE
{
public:
    KIFACE_AGENT( const char* aName, KIWAY::FACE_T aId ) :
            KIFACE_BASE( aName, aId )
    {
    }

    bool OnKifaceStart( PGM_BASE* aProgram, int aCtlBits, KIWAY* aKiway ) override
    {
        return start_common( aCtlBits );
    }

    void OnKifaceEnd() override { end_common(); }

    wxWindow* CreateKiWindow( wxWindow* aParent, int aClassId, KIWAY* aKiway, int aCtlBits = 0 ) override
    {
        switch( aClassId )
        {
        case FRAME_AGENT:
        {
            // Singleton guard: reuse the existing agent frame if it's still alive.
            // wxWeakRef auto-clears when the underlying window is destroyed, so
            // this no longer dereferences a dangling pointer after the previous
            // frame went away (the cause of an EXC_BAD_ACCESS in IsBeingDeleted
            // when the launcher's OnIdle re-issued Player(FRAME_AGENT, true)
            // after a project switch).
            if( m_agentFrame && !m_agentFrame->IsBeingDeleted() )
                return m_agentFrame;

            AGENT_FRAME* frame = new AGENT_FRAME( aKiway, aParent );
            m_agentFrame = frame;
            return frame;
        }
        default: return nullptr;
        }
    }

    void* IfaceOrAddress( int aDataId ) override { return nullptr; }

    // Accessor for the KIFACE
    static KIFACE_BASE& Kiface() { return kiface; }

private:
    static KIFACE_AGENT     kiface;
    wxWeakRef<AGENT_FRAME>  m_agentFrame;
};

KIFACE_AGENT KIFACE_AGENT::kiface( "agent", KIWAY::FACE_AGENT );

// Use a static accessor to avoid RTLD_GLOBAL symbol interposition.
// See terminal.cpp for the full explanation.
static KIFACE_BASE& AgentKiface()
{
    return KIFACE_AGENT::Kiface();
}

KIFACE_BASE& Kiface()
{
    return AgentKiface();
}

// The C access to the KIFACE
extern "C"
{
    KIFACE* KIFACE_GETTER( int* aKifaceVersion, int aKiwayVersion, PGM_BASE* aProgram )
    {
        *aKifaceVersion = KIFACE_VERSION;
        return &AgentKiface();
    }

} // extern "C"
