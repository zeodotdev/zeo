#include "agent_frame.h"
#include <wx/button.h>
#include <wx/sizer.h>
#include <wx/textctrl.h>
#include <wx/msgdlg.h>
#include <bitmaps.h>
#include <id.h>

BEGIN_EVENT_TABLE( AGENT_FRAME, KIWAY_PLAYER )
EVT_MENU( wxID_EXIT, AGENT_FRAME::OnExit )
END_EVENT_TABLE()

AGENT_FRAME::AGENT_FRAME( KIWAY* aKiway, wxWindow* aParent ) :
        KIWAY_PLAYER( aKiway, aParent, FRAME_AGENT, "Agent", wxDefaultPosition, wxDefaultSize, wxDEFAULT_FRAME_STYLE,
                      "agent_frame_name", schIUScale )
{
    // --- UI Layout ---
    // Top: Chat History (Expandable)
    // Bottom: Input Container (Unified)

    wxBoxSizer* mainSizer = new wxBoxSizer( wxVERTICAL );

    // 1. Chat History Area
    m_chatWindow = new wxHtmlWindow( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxHW_SCROLLBAR_AUTO );
    // Set a default page content or styling if needed
    m_chatWindow->SetPage( "<html><body><p>Welcome to KiCad Agent.</p></body></html>" );
    mainSizer->Add( m_chatWindow, 1, wxEXPAND | wxALL, 5 );

    // 2. Input Container (Unified Look)
    // Vertical Sizer: Text Area Top, Controls Bottom
    wxBoxSizer* inputContainerSizer = new wxBoxSizer( wxVERTICAL );

    // 2a. Text Input (Top)
    m_inputCtrl = new wxTextCtrl( this, wxID_ANY, "", wxDefaultPosition, wxSize( -1, 80 ),
                                  wxTE_MULTILINE | wxTE_PROCESS_ENTER );
    // m_inputCtrl->SetHint( "Ask anything" ); // Requires newer wxWidgets, might be ignored on old
    inputContainerSizer->Add( m_inputCtrl, 1, wxEXPAND | wxALL, 0 );

    // 2b. Control Row (Bottom)
    wxBoxSizer* controlsSizer = new wxBoxSizer( wxHORIZONTAL );

    // Plus Button
    m_plusButton = new wxButton( this, wxID_ANY, "+", wxDefaultPosition, wxSize( 30, -1 ) );
    controlsSizer->Add( m_plusButton, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5 );

    // Mode Selection
    wxString modeChoices[] = { "Planning", "Execution" };
    m_modeChoice = new wxChoice( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, 2, modeChoices );
    m_modeChoice->SetSelection( 0 );
    controlsSizer->Add( m_modeChoice, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5 );

    // Model Selection
    wxString modelChoices[] = { "Gemini 1.5 Pro", "Gemini 1.5 Flash" };
    m_modelChoice = new wxChoice( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, 2, modelChoices );
    m_modelChoice->SetSelection( 0 );
    controlsSizer->Add( m_modelChoice, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5 );

    // Spacer
    controlsSizer->AddStretchSpacer();

    // Send Button
    m_actionButton = new wxButton( this, wxID_ANY, "->" ); // Arrow icon would be better
    controlsSizer->Add( m_actionButton, 0, wxALIGN_CENTER_VERTICAL );

    inputContainerSizer->Add( controlsSizer, 0, wxEXPAND | wxTOP | wxBOTTOM, 5 );

    // Add Input Container to Main Sizer
    mainSizer->Add( inputContainerSizer, 0, wxEXPAND | wxALL, 10 );

    SetSizer( mainSizer );
    Layout();
    SetSize( 500, 600 ); // Slightly taller for chat

    // Bind Events
    m_actionButton->Bind( wxEVT_BUTTON, &AGENT_FRAME::OnSend, this );
    m_inputCtrl->Bind( wxEVT_TEXT_ENTER, &AGENT_FRAME::OnTextEnter, this );
    // m_modelChoice->Bind( wxEVT_CHOICE, &AGENT_FRAME::OnModelSelection, this );
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

void AGENT_FRAME::OnSend( wxCommandEvent& aEvent )
{
    wxString text = m_inputCtrl->GetValue();
    if( text.IsEmpty() )
        return;

    // Echo user message to chat
    wxString currentHtml = m_chatWindow->ToText(); // Simple way to get content? No, ToText is plain text.
    // For now, let's just append. HTML window doesn't have a simple "Append".
    // We usually reconstruct the page or use obscure internal methods.
    // Let's verify what we have available.
    // For this mock-up, let's just replace the page with accumulated content concept (simulation).
    // In reality we would manage a message list and render it.

    wxString msgHtml = wxString::Format( "<p><b>User:</b> %s</p>", text );
    m_chatWindow->AppendToPage( msgHtml );

    // Clear Input
    m_inputCtrl->Clear();

    // Simulate thinking state
    m_actionButton->SetLabel( "Stop" );

    // In a real app, we'd start a thread/process here.
}

void AGENT_FRAME::OnStop( wxCommandEvent& aEvent )
{
    // Stop generation
    m_actionButton->SetLabel( "Send" );
}

void AGENT_FRAME::OnTextEnter( wxCommandEvent& aEvent )
{
    if( wxGetKeyState( WXK_SHIFT ) )
    {
        m_inputCtrl->WriteText( "\n" );
    }
    else
    {
        OnSend( aEvent );
    }
}

void AGENT_FRAME::OnModelSelection( wxCommandEvent& aEvent )
{
    // Handle model change
}

void AGENT_FRAME::OnExit( wxCommandEvent& event )
{
    Close();
}
