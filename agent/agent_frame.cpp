#include "agent_frame.h"
#include <wx/button.h>
#include <wx/sizer.h>
#include <wx/textctrl.h>
#include <wx/msgdlg.h>
#include <bitmaps.h>
#include <id.h>

BEGIN_EVENT_TABLE( AGENT_FRAME, KIWAY_PLAYER )
EVT_BUTTON( wxID_OK, AGENT_FRAME::onRunQuery )
EVT_BUTTON( wxID_CANCEL, AGENT_FRAME::onCancel )
EVT_MENU( wxID_EXIT, AGENT_FRAME::OnExit )
END_EVENT_TABLE()

AGENT_FRAME::AGENT_FRAME( KIWAY* aKiway, wxWindow* aParent ) :
        KIWAY_PLAYER( aKiway, aParent, FRAME_AGENT, "Agent", wxDefaultPosition, wxDefaultSize, wxDEFAULT_FRAME_STYLE,
                      "agent_frame_name", schIUScale )
{
    SetTitle( "KiCad Agent" );
    wxIcon icon;
    icon.CopyFromBitmap( KiBitmap( BITMAPS::icon_kicad ) );
    SetIcon( icon );

    wxBoxSizer* mainSizer = new wxBoxSizer( wxVERTICAL );

    m_textCtrl = new wxTextCtrl( this, wxID_ANY, "", wxDefaultPosition, wxSize( 400, 300 ), wxTE_MULTILINE );
    mainSizer->Add( m_textCtrl, 1, wxEXPAND | wxALL, 5 );

    wxBoxSizer* btnSizer = new wxBoxSizer( wxHORIZONTAL );
    m_btnRun = new wxButton( this, wxID_OK, "Run" );
    m_btnCancel = new wxButton( this, wxID_CANCEL, "Cancel" );

    btnSizer->Add( m_btnRun, 0, wxALL, 5 );
    btnSizer->Add( m_btnCancel, 0, wxALL, 5 );

    mainSizer->Add( btnSizer, 0, wxALIGN_CENTER );

    SetSizer( mainSizer );
    Layout();
    SetSize( 500, 400 );
}

AGENT_FRAME::~AGENT_FRAME()
{
}

void AGENT_FRAME::ShowChangedLanguage()
{
    KIWAY_PLAYER::ShowChangedLanguage();
}

void AGENT_FRAME::KiwayMailIn( KIWAY_EXPRESS& aEvent )
{
    // Handle messages from KiCad
}

void AGENT_FRAME::onRunQuery( wxCommandEvent& aEvent )
{
    wxMessageBox( "Query running..." );
}

void AGENT_FRAME::onCancel( wxCommandEvent& aEvent )
{
    Close();
}

void AGENT_FRAME::OnExit( wxCommandEvent& event )
{
    Close();
}
