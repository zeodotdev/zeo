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
    // Bottom: Controls (Fixed)

    wxBoxSizer* mainSizer = new wxBoxSizer( wxVERTICAL );

    // 1. Chat History Area
    m_chatWindow = new wxHtmlWindow( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxHW_SCROLLBAR_AUTO );
    // Set a default page content or styling if needed
    m_chatWindow->SetPage( "<html><body><p>Welcome to KiCad Agent.</p></body></html>" );
    mainSizer->Add( m_chatWindow, 1, wxEXPAND | wxALL, 5 );

    // 2. Input & Controls Area
    // We want: Text Input at bottom. Send button at bottom right of text input. Model dropdown to the left.
    // Let's us a FlexGridSizer or just nested BoxSizers.

    wxBoxSizer* bottomSizer = new wxBoxSizer( wxHORIZONTAL );

    // Left: Model Dropdown (Vertical alignment top or center? changing to bottom as per request "other side")
    // "Model dropdown on other side of text input area" implies left side if button is right.
    wxBoxSizer* leftControlSizer = new wxBoxSizer( wxVERTICAL );
    wxString    choices[] = { "Gemini 1.5 Pro", "Gemini 1.5 Flash" };
    m_modelChoice = new wxChoice( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, 2, choices );
    m_modelChoice->SetSelection( 0 );
    leftControlSizer->Add( m_modelChoice, 0, wxALIGN_BOTTOM | wxBOTTOM,
                           5 ); // Align to bottom to match text input bottom?
    // Actually standard UI usually aligns top of controls. Let's try aligning to bottom to match the requested "bottom" theme.

    bottomSizer->Add( leftControlSizer, 0, wxEXPAND | wxRIGHT, 5 );

    // Center: Text Input (Expandable)
    m_inputCtrl = new wxTextCtrl( this, wxID_ANY, "", wxDefaultPosition, wxSize( -1, 60 ),
                                  wxTE_MULTILINE | wxTE_PROCESS_ENTER );
    bottomSizer->Add( m_inputCtrl, 1, wxEXPAND | wxRIGHT, 5 );

    // Right: Send Button (Aligned to bottom)
    wxBoxSizer* rightControlSizer = new wxBoxSizer( wxVERTICAL );
    rightControlSizer->AddStretchSpacer(); // Push button to bottom
    m_actionButton = new wxButton( this, wxID_ANY, "Send" );
    rightControlSizer->Add( m_actionButton, 0, wxALIGN_BOTTOM );

    bottomSizer->Add( rightControlSizer, 0, wxEXPAND );

    mainSizer->Add( bottomSizer, 0, wxEXPAND | wxALL, 10 );

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
