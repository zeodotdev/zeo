#include <kiface_base.h>
#include <pgm_base.h>
#include <kiway.h>
#include "agent_frame.h"
#include <cstdio>
#include <wx/app.h>

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
        printf( "AGENT: OnKifaceStart called\n" );
        if( wxTheApp )
            printf( "AGENT: wxTheApp is %p\n", (void*) wxTheApp );
        else
            printf( "AGENT: wxTheApp is NULL\n" );

        return start_common( aCtlBits );
    }

    void OnKifaceEnd() override { end_common(); }

    wxWindow* CreateKiWindow( wxWindow* aParent, int aClassId, KIWAY* aKiway, int aCtlBits = 0 ) override
    {
        switch( aClassId )
        {
        case FRAME_AGENT:
        {
            AGENT_FRAME* frame = new AGENT_FRAME( aKiway, aParent );
            printf( "AGENT: CreateKiWindow AGENT_FRAME created: %p\n", frame );
            return frame;
        }
        default: return nullptr;
        }
    }

    void* IfaceOrAddress( int aDataId ) override { return nullptr; }

    // Accessor for the KIFACE
    static KIFACE_BASE& Kiface() { return kiface; }

private:
    static KIFACE_AGENT kiface;
};

KIFACE_AGENT KIFACE_AGENT::kiface( "agent", KIWAY::FACE_AGENT );

KIFACE_BASE& Kiface()
{
    return KIFACE_AGENT::Kiface();
}

// The C access to the KIFACE
extern "C"
{
    KIFACE* KIFACE_GETTER( int* aKifaceVersion, int aKiwayVersion, PGM_BASE* aProgram )
    {
        *aKifaceVersion = KIFACE_VERSION;
        return &Kiface();
    }

} // extern "C"
