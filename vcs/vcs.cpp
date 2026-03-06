#include <kiface_base.h>
#include <pgm_base.h>
#include <kiway.h>
#include "vcs_frame.h"
#include <cstdio>
#include <wx/app.h>

// The KIFACE implementation for the Version Control app
class KIFACE_VCS : public KIFACE_BASE
{
public:
    KIFACE_VCS( const char* aName, KIWAY::FACE_T aId ) :
            KIFACE_BASE( aName, aId )
    {
    }

    bool OnKifaceStart( PGM_BASE* aProgram, int aCtlBits, KIWAY* aKiway ) override
    {
        return start_common( aCtlBits );
    }

    void OnKifaceEnd() override { end_common(); }

    wxWindow* CreateKiWindow( wxWindow* aParent, int aClassId, KIWAY* aKiway,
                              int aCtlBits = 0 ) override
    {
        switch( aClassId )
        {
        case FRAME_VCS:
        {
            VCS_FRAME* frame = new VCS_FRAME( aKiway, aParent );
            return frame;
        }
        default: return nullptr;
        }
    }

    void* IfaceOrAddress( int aDataId ) override { return nullptr; }

    static KIFACE_BASE& Kiface() { return kiface; }

private:
    static KIFACE_VCS kiface;
};

KIFACE_VCS KIFACE_VCS::kiface( "vcs", KIWAY::FACE_VCS );

KIFACE_BASE& Kiface()
{
    return KIFACE_VCS::Kiface();
}

extern "C"
{
    KIFACE* KIFACE_GETTER( int* aKifaceVersion,
                           int aKiwayVersion,
                           PGM_BASE* aProgram )
    {
        *aKifaceVersion = KIFACE_VERSION;
        return &Kiface();
    }
}
