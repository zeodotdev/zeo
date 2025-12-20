
#include <kiface_base.h>
#include <pgm_base.h>
#include <kiway.h>
#include "kicad_agent_frame.h"

// The generic KIFACE for kicad_agent
class IFACE : public KIFACE_BASE
{
public:
    IFACE( const char* aName, KIWAY::FACE_T aFaceId ) :
            KIFACE_BASE( aName, aFaceId )
    {
    }

    bool OnKifaceStart( PGM_BASE* aProgram, int aCtlBits, KIWAY* aKiway ) override { return start_common( aCtlBits ); }

    void OnKifaceEnd() override { end_common(); }

    wxWindow* CreateKiWindow( wxWindow* aParent, int aClassId, KIWAY* aKiway, int aCtlBits = 0 ) override
    {
        switch( aClassId )
        {
        case FRAME_AGENT: return new KICAD_AGENT_FRAME( aKiway, aParent );
        default: return nullptr;
        }
    }

    void* IfaceOrAddress( int aDataId ) override { return nullptr; }
};

static IFACE
        kiface( "kicad_agent",
                KIWAY::FACE_PCB ); // Use FACE_PCB for now as placeholder or define FACE_AGENT later if needed in KIWAY

KIFACE_BASE& Kiface()
{
    return kiface;
}

KIFACE_API KIFACE* KIFACE_GETTER( int* aKIFACEversion, int aKiwayVersion, PGM_BASE* aProgram )
{
    return &kiface;
}
