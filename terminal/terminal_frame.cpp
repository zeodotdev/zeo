#include "terminal_frame.h"
#include <kiway_express.h>
#include <mail_type.h>
#include <wx/sizer.h>
#include <wx/settings.h>
#include <id.h>
#include <kiway.h>

#include <base_units.h>

BEGIN_EVENT_TABLE( TERMINAL_FRAME, KIWAY_PLAYER )
EVT_MENU( wxID_EXIT, TERMINAL_FRAME::OnExit )
END_EVENT_TABLE()

TERMINAL_FRAME::TERMINAL_FRAME( KIWAY* aKiway, wxWindow* aParent ) :
        KIWAY_PLAYER( aKiway, aParent, FRAME_TERMINAL, "Terminal", wxDefaultPosition, wxDefaultSize,
                      wxDEFAULT_FRAME_STYLE, "terminal_frame_name", schIUScale )
{
    wxBoxSizer* mainSizer = new wxBoxSizer( wxVERTICAL );

    // Output Area (ReadOnly)
    m_outputCtrl = new wxTextCtrl( this, wxID_ANY, "KiCad Internal Terminal\nType commands below.\n", wxDefaultPosition,
                                   wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2 );

    // Set Monospace Font
    wxFont font = wxSystemSettings::GetFont( wxSYS_ANSI_FIXED_FONT );
    font.SetPointSize( 12 );
    m_outputCtrl->SetFont( font );

    mainSizer->Add( m_outputCtrl, 1, wxEXPAND | wxALL, 0 );

    // Input Area
    m_inputCtrl = new wxTextCtrl( this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER );
    m_inputCtrl->SetFont( font );

    mainSizer->Add( m_inputCtrl, 0, wxEXPAND | wxALL, 5 );

    SetSizer( mainSizer );
    Layout();
    SetSize( 600, 400 );

    // Bind Events
    m_inputCtrl->Bind( wxEVT_TEXT_ENTER, &TERMINAL_FRAME::OnTextEnter, this );
}

TERMINAL_FRAME::~TERMINAL_FRAME()
{
}

void TERMINAL_FRAME::OnExit( wxCommandEvent& event )
{
    Close( true );
}

void TERMINAL_FRAME::OnTextEnter( wxCommandEvent& aEvent )
{
    wxString cmd = m_inputCtrl->GetValue();
    if( cmd.IsEmpty() )
        return;

    m_outputCtrl->AppendText( "\n> " + cmd + "\n" );
    m_inputCtrl->Clear();

    // Determine Destination
    FRAME_T dest = FRAME_SCH; // Default
    if( cmd.Contains( "_pcb_" ) || cmd.Contains( "_component_" ) || cmd.Contains( "_net_" )
        || cmd.Contains( "_board_" ) )
    {
        dest = FRAME_PCB_EDITOR;
    }

    // Send Request
    std::string payload = cmd.ToStdString();
    Kiway().ExpressMail( dest, MAIL_AGENT_REQUEST, payload, this );
}

void TERMINAL_FRAME::KiwayMailIn( KIWAY_EXPRESS& aEvent )
{
    if( aEvent.Command() == MAIL_AGENT_RESPONSE )
    {
        std::string payload = aEvent.GetPayload();
        m_outputCtrl->AppendText( payload + "\n" );
    }
}
