#include <kiface_base.h>
#include <pgm_base.h>
#include <kiway.h>
#include "terminal_frame.h"
#include <cstdio>
#include <wx/app.h>

// The KIFACE implementation
class KIFACE_TERMINAL : public KIFACE_BASE
{
public:
    KIFACE_TERMINAL( const char* aName, KIWAY::FACE_T aId ) :
            KIFACE_BASE( aName, aId )
    {
    }

    bool OnKifaceStart( PGM_BASE* aProgram, int aCtlBits, KIWAY* aKiway ) override { return start_common( aCtlBits ); }

    void OnKifaceEnd() override { end_common(); }

    wxWindow* CreateKiWindow( wxWindow* aParent, int aClassId, KIWAY* aKiway, int aCtlBits = 0 ) override
    {
        switch( aClassId )
        {
        case FRAME_TERMINAL:
        {
            TERMINAL_FRAME* frame = new TERMINAL_FRAME( aKiway, aParent );
            return frame;
        }
        default: return nullptr;
        }
    }

    void* IfaceOrAddress( int aDataId ) override { return nullptr; }

    static KIFACE_BASE& Kiface() { return kiface; }

private:
    static KIFACE_TERMINAL kiface;
};

KIFACE_TERMINAL KIFACE_TERMINAL::kiface( "terminal", KIWAY::FACE_TERMINAL );

KIFACE_BASE& Kiface()
{
    return KIFACE_TERMINAL::Kiface();
}

extern "C"
{
    KIFACE* KIFACE_GETTER( int* aKifaceVersion, int aKiwayVersion, PGM_BASE* aProgram )
    {
        *aKifaceVersion = KIFACE_VERSION;
        return &Kiface();
    }
}
