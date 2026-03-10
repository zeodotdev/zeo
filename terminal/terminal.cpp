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

// Use a static accessor to avoid RTLD_GLOBAL symbol interposition.
// When multiple kiface DSOs are loaded with wxDL_GLOBAL, the global
// Kiface() function from the first-loaded DSO shadows all others.
// This caused the second kiface to return the wrong KIFACE* from
// KIFACE_GETTER, making its CreateKiWindow() fail (wrong switch path).
static KIFACE_BASE& TerminalKiface()
{
    return KIFACE_TERMINAL::Kiface();
}

KIFACE_BASE& Kiface()
{
    return TerminalKiface();
}

extern "C"
{
    KIFACE* KIFACE_GETTER( int* aKifaceVersion, int aKiwayVersion, PGM_BASE* aProgram )
    {
        *aKifaceVersion = KIFACE_VERSION;
        return &TerminalKiface();
    }
}
