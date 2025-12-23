#include "agent_frame.h"
#include <kiway_express.h>
#include <mail_type.h>
#include <wx/log.h>
#include <sstream>
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

    // Status Pill (Selection Info)
    m_selectionPill = new wxButton( this, wxID_ANY, "No Selection", wxDefaultPosition, wxDefaultSize );
    m_selectionPill->Hide(); // Hide on load
    inputContainerSizer->Add( m_selectionPill, 0, wxALIGN_LEFT | wxALL, 2 );

    // 2a. Text Input (Top)
    m_inputCtrl = new wxTextCtrl( this, wxID_ANY, "", wxDefaultPosition, wxSize( -1, 80 ),
                                  wxTE_MULTILINE | wxTE_PROCESS_ENTER | wxTE_RICH2 );
    // m_inputCtrl->SetHint( "Ask anything" ); // Requires newer wxWidgets, might be ignored on old
    inputContainerSizer->Add( m_inputCtrl, 1, wxEXPAND | wxALL, 0 );

    // 2b. Control Row (Bottom)
    wxBoxSizer* controlsSizer = new wxBoxSizer( wxHORIZONTAL );

    // Plus Button
    m_plusButton = new wxButton( this, wxID_ANY, "+", wxDefaultPosition, wxSize( 30, -1 ) );
    controlsSizer->Add( m_plusButton, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5 );

    // Mode Selection
    // wxString modeChoices[] = { "Planning", "Execution" };
    // m_modeChoice = new wxChoice( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, 2, modeChoices );
    // m_modeChoice->SetSelection( 0 );
    // controlsSizer->Add( m_modeChoice, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5 );

    // Model Selection
    // wxString modelChoices[] = { "Gemini 1.5 Pro", "Gemini 1.5 Flash" };
    // m_modelChoice = new wxChoice( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, 2, modelChoices );
    // m_modelChoice->SetSelection( 0 );
    // controlsSizer->Add( m_modelChoice, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5 );

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
    m_selectionPill->Bind( wxEVT_BUTTON, &AGENT_FRAME::OnSelectionPillClick, this );
    m_inputCtrl->Bind( wxEVT_KEY_DOWN, &AGENT_FRAME::OnInputKeyDown, this );
    m_inputCtrl->Bind( wxEVT_TEXT, &AGENT_FRAME::OnInputText, this );
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
    if( aEvent.Command() == MAIL_SELECTION )
    {
        std::string payload = aEvent.GetPayload();
        printf( "AGENT_FRAME::KiwayMailIn: Received payload '%s'\n", payload.c_str() );
        // Basic parsing for now: just display it
        // Expected "SELECTION|<APP>|<DESC>"

        // Find second pipe to get description
        size_t firstPipe = payload.find( '|' );
        if( payload.empty() || payload == "CLEARED" )
        {
            m_selectionPill->Show( false );
        }
        else
        {
            // Expected format: APP|DESCRIPTION
            size_t firstPipe = payload.find( '|' );
            if( firstPipe != std::string::npos )
            {
                std::string desc = payload.substr( firstPipe + 1 );
                m_selectionPill->SetLabel( desc );
                m_selectionPill->Show( true );
            }
            else
            {
                m_selectionPill->SetLabel( payload );
                m_selectionPill->Show( true );
            }
        }
        Layout(); // Important to refresh layout after showing/hiding
    }
}

void AGENT_FRAME::OnSelectionPillClick( wxCommandEvent& aEvent )
{
    wxString label = m_selectionPill->GetLabel();
    if( !label.IsEmpty() )
    {
        // Append @{Label} to input
        wxString currentText = m_inputCtrl->GetValue();
        if( !currentText.IsEmpty() && !currentText.EndsWith( " " ) )
            m_inputCtrl->AppendText( " " );

        // Insert text (formatted by OnInputText)
        m_inputCtrl->AppendText( "@{" + label + "} " );
        m_inputCtrl->SetFocus();
    }
}

void AGENT_FRAME::OnInputText( wxCommandEvent& aEvent )
{
    // Dynamic Highlighting: Bold valid @{...} tags
    long     currentPos = m_inputCtrl->GetInsertionPoint();
    wxString text = m_inputCtrl->GetValue();

    // 1. Reset all to normal
    wxTextAttr normalStyle;
    normalStyle.SetFontWeight( wxFONTWEIGHT_NORMAL );
    m_inputCtrl->SetStyle( 0, text.Length(), normalStyle );

    // 2. Scan for @{...} pairs
    size_t start = 0;
    while( ( start = text.find( "@{", start ) ) != wxString::npos )
    {
        size_t end = text.find( "}", start );
        if( end != wxString::npos )
        {
            // Apply Bold to @{...}
            wxTextAttr boldStyle;
            boldStyle.SetFontWeight( wxFONTWEIGHT_BOLD );
            m_inputCtrl->SetStyle( start, end + 1, boldStyle );
            start = end + 1;
        }
        else
        {
            break; // No more closed tags
        }
    }

    // Restore insertion point/selection (SetStyle might affect it?)
    // Actually, SetStyle preserves insertion point usually, but let's be safe if needed.
    // In wxWidgets, SetStyle shouldn't move cursor.
}

void AGENT_FRAME::OnInputKeyDown( wxKeyEvent& aEvent )
{
    int key = aEvent.GetKeyCode();

    if( key == WXK_BACK || key == WXK_DELETE )
    {
        long     pos = m_inputCtrl->GetInsertionPoint();
        wxString text = m_inputCtrl->GetValue();

        if( pos > 0 )
        {
            // Atomic deletion for @{...}
            // Check if we are deleting a '}'
            if( pos <= text.Length() && text[pos - 1] == '}' )
            {
                // Verify matching @{
                long openBrace = text.rfind( "@{", pos - 1 );
                if( openBrace != wxString::npos )
                {
                    // Ensure no other '}' in between (simple nesting check)
                    wxString content = text.SubString( openBrace, pos - 1 );
                    // content is like @{tag}
                    if( content.find( '}' ) == content.Length() - 1 ) // Last char is the only closing brace
                    {
                        m_inputCtrl->Remove( openBrace, pos );
                        return;
                    }
                }
            }
        }
    }
    aEvent.Skip(); // Default processing
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
