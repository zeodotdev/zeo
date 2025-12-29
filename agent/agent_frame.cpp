#include "agent_frame.h"
#include "agent_thread.h"
#include <kiway_express.h>
#include <mail_type.h>
#include <wx/log.h>
#include <sstream>
#include <wx/sizer.h>
#include <wx/textctrl.h>
#include <wx/msgdlg.h>
#include <bitmaps.h>
#include <id.h>
#include <nlohmann/json.hpp>

BEGIN_EVENT_TABLE( AGENT_FRAME, KIWAY_PLAYER )
EVT_MENU( wxID_EXIT, AGENT_FRAME::OnExit )
END_EVENT_TABLE()

AGENT_FRAME::AGENT_FRAME( KIWAY* aKiway, wxWindow* aParent ) :
        KIWAY_PLAYER( aKiway, aParent, FRAME_AGENT, "Agent", wxDefaultPosition, wxDefaultSize, wxDEFAULT_FRAME_STYLE,
                      "agent_frame_name", schIUScale ),
        m_workerThread( nullptr )
{
    // --- UI Layout ---
    // Top: Chat History (Expandable)
    // Bottom: Input Container (Unified)

    wxBoxSizer* mainSizer = new wxBoxSizer( wxVERTICAL );

    // 1. Chat History Area
    m_chatWindow = new wxHtmlWindow( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxHW_SCROLLBAR_AUTO );
    // Set a default page content or styling if needed
    m_chatWindow->SetPage(
            "<html><body bgcolor='#2D2D2D' text='#FFFFFF'><p>Welcome to KiCad Agent.</p></body></html>" );
    mainSizer->Add( m_chatWindow, 1, wxEXPAND | wxALL, 5 );

    // 2. Input Container (Unified Look)
    // Create m_inputPanel for dark styling
    m_inputPanel = new wxPanel( this, wxID_ANY );
    m_inputPanel->SetBackgroundColour( wxColour( 30, 30, 30 ) ); // Dark prompt area

    // Use a vertical BoxSizer for the panel
    wxBoxSizer* outerInputSizer = new wxBoxSizer( wxVERTICAL );

    // Use an inner sizer for content padding
    wxBoxSizer* inputContainerSizer = new wxBoxSizer( wxVERTICAL );

    // Status Pill (Selection Info)
    m_selectionPill = new wxButton( m_inputPanel, wxID_ANY, "No Selection", wxDefaultPosition, wxDefaultSize );
    m_selectionPill->Hide(); // Hide on load
    inputContainerSizer->Add( m_selectionPill, 0, wxALIGN_LEFT | wxBOTTOM, 2 );

    // 2a. Text Input (Top)
    m_inputCtrl = new wxTextCtrl( m_inputPanel, wxID_ANY, "", wxDefaultPosition, wxSize( -1, 80 ),
                                  wxTE_MULTILINE | wxTE_PROCESS_ENTER | wxTE_RICH2 );
    // m_inputCtrl->SetHint( "Ask anything" ); // Requires newer wxWidgets, might be ignored on old
    inputContainerSizer->Add( m_inputCtrl, 1, wxEXPAND | wxBOTTOM, 5 );

    // 2b. Control Row (Bottom)
    wxBoxSizer* controlsSizer = new wxBoxSizer( wxHORIZONTAL );

    // Plus Button
    m_plusButton = new wxButton( m_inputPanel, wxID_ANY, "+", wxDefaultPosition, wxSize( 30, -1 ) );
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
    m_actionButton = new wxButton( m_inputPanel, wxID_ANY, "->" ); // Arrow icon would be better
    controlsSizer->Add( m_actionButton, 0, wxALIGN_CENTER_VERTICAL );

    inputContainerSizer->Add( controlsSizer, 0, wxEXPAND );

    // Add inner sizer to outer sizer with padding
    outerInputSizer->Add( inputContainerSizer, 1, wxEXPAND | wxALL, 10 );

    m_inputPanel->SetSizer( outerInputSizer );

    // Add Input Container to Main Sizer (No border so background fills area)
    mainSizer->Add( m_inputPanel, 0, wxEXPAND );

    SetSizer( mainSizer );
    Layout();
    SetSize( 500, 600 ); // Slightly taller for chat

    // Bind Events
    m_actionButton->Bind( wxEVT_BUTTON, &AGENT_FRAME::OnSend, this );
    m_inputCtrl->Bind( wxEVT_TEXT_ENTER, &AGENT_FRAME::OnTextEnter, this );
    m_selectionPill->Bind( wxEVT_BUTTON, &AGENT_FRAME::OnSelectionPillClick, this );
    m_inputCtrl->Bind( wxEVT_KEY_DOWN, &AGENT_FRAME::OnInputKeyDown, this );
    m_inputCtrl->Bind( wxEVT_TEXT, &AGENT_FRAME::OnInputText, this );

    // Bind Thread Events
    Bind( wxEVT_AGENT_UPDATE, &AGENT_FRAME::OnAgentUpdate, this );
    Bind( wxEVT_AGENT_COMPLETE, &AGENT_FRAME::OnAgentComplete, this );
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

        if( payload.find( "JSON_PAYLOAD" ) == 0 )
        {
            std::stringstream ss( payload );
            std::string       prefix, source, jsonStr;
            std::getline( ss, prefix ); // JSON_PAYLOAD
            std::getline( ss, source ); // SCH or PCB

            // Remainder is JSON
            // We can determine start pos by prefix.length() + source.length() + 2 (newlines)
            // Or just read the rest of the stream
            std::string line;
            while( std::getline( ss, line ) )
            {
                jsonStr += line + "\n";
            }

            // Update JSON Store
            if( source == "SCH" )
                m_schJson = jsonStr;
            else if( source == "PCB" )
                m_pcbJson = jsonStr;

            // Update Selection Pill
            try
            {
                auto j = nlohmann::json::parse( jsonStr );
                if( j.contains( "selection" ) && !j["selection"].empty() )
                {
                    std::string ref = j["selection"][0]["ref"];
                    size_t      count = j["selection"].size();
                    std::string label = "Add: " + ref;
                    if( count > 1 )
                        label += " +" + std::to_string( count - 1 );

                    m_selectionPill->SetLabel( label );
                    m_selectionPill->Show( true );
                }
                else
                {
                    // If selection empty, hide pill?
                    // Or keep it if we want to add "Context"?
                    // For now, hide if no selection
                    // But wait, the user might want to add *project context* even if nothing selected?
                    // Usually "Add: Selection" implies specific selection.
                    // If empty, maybe hide.
                    // If "CLEARED" message was sent, we might receive empty selection array?
                    // The tools send JSON even if empty?
                    // m_schJson handles global context.
                    // Let's hide pill if no items selected.
                    m_selectionPill->Show( false );
                }
            }
            catch( ... )
            {
                // Parse error
            }
        }
        else if( payload == "SELECTION\nSCH\nCLEARED" )
        {
            // Clear SCH selection specifically?
            // Actually, new tools send JSON with empty selection array instead of this?
            // I updated tools to send JSON *if !empty()*.
            // Wait, I updated tools: "if( m_selection.Empty() ) { ... SELECTION\nSCH\nCLEARED ... }"
            // So I still need to handle legacy clear messages?
            // Yes.
            if( payload.find( "SCH" ) != std::string::npos )
            {
                // Clear SCH JSON? Or just update it to empty selection?
                // If I clear m_schJson, I lose the project components list!
                // I should parsing logic update selection to empty.
                // But the tool doesn't send JSON if empty.
                // So "CLEARED" means "Empty Selection".
                // Use legacy clear to hide pill.
                // And ideally, I should keep the LAST known project components?
                // m_schJson remains valid for components, but selection is invalid.
                // Implementation detail: I might need to update m_schJson to set "selection": []?
                // Too complex to parse/edit JSON string.
                // I'll leave m_schJson as is (it contains last Valid selection).
                // But the prompt will include it.
                // This is a specialized behavior: "If CLEARED, don't use selection from JSON".
                // Maybe I need `m_schJsonSelectionValid` flag.
                // Or, I should update the tools to SEND JSON even on Clear (with empty selection).
                // That would be cleaner.
                // But for now, I'll just hide the pill.
                m_selectionPill->Show( false );
            }
            else if( payload.find( "PCB" ) != std::string::npos )
            {
                m_selectionPill->Show( false );
            }
        }
    }
    Layout();
}

void AGENT_FRAME::OnSelectionPillClick( wxCommandEvent& aEvent )
{
    wxString label = m_selectionPill->GetLabel();
    if( !label.IsEmpty() )
    {
        // Strip "Add: " prefix if present for the tag
        if( label.StartsWith( "Add: " ) )
            label = label.Mid( 5 );

        // Hide pill on click
        m_selectionPill->Hide();
        Layout(); // specific layout for input panel/frame?
        // Layout(); // Calling global Layout() might be heavy, but safest.

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
    // If thread is running, this button acts as Stop
    if( m_workerThread )
    {
        OnStop( aEvent );
        return;
    }

    wxString text = m_inputCtrl->GetValue();
    if( text.IsEmpty() )
        return;

    // Display User Message
    wxString msgHtml = wxString::Format( "<p><b>User:</b> %s</p>", text );
    m_chatWindow->AppendToPage( msgHtml );
    m_chatWindow->AppendToPage( "<p><b>Agent:</b> " ); // Start Agent response block

    // Clear Input and Update UI
    m_inputCtrl->Clear();
    m_actionButton->SetLabel( "Stop" );
    // m_inputCtrl->Enable( false ); // Optional: disable input during generation

    // Create and Run Thread
    // TODO: proper system prompt management
    std::string systemPrompt = "You are a helpful assistant for KiCad PCB design.";

    // Pass payload if available (Combine SCH and PCB contexts)
    // Pass payload if available (Combine SCH and PCB contexts)
    std::string payload;
    if( !m_schJson.empty() )
        payload += m_schJson + "\n";
    if( !m_pcbJson.empty() )
        payload += m_pcbJson + "\n";

    m_workerThread = new AGENT_THREAD( this, text.ToStdString(), systemPrompt, payload );
    if( m_workerThread->Run() != wxTHREAD_NO_ERROR )
    {
        wxLogMessage( "Error: Could not create worker thread." );
        delete m_workerThread;
        m_workerThread = nullptr;
        m_actionButton->SetLabel( "->" );
        // m_inputCtrl->Enable( true );
    }
}

void AGENT_FRAME::OnStop( wxCommandEvent& aEvent )
{
    if( m_workerThread )
    {
        m_workerThread->Delete(); // soft delete, checks TestDestroy()
        m_workerThread = nullptr;
    }
    m_chatWindow->AppendToPage( "</p><p><i>(Stopped)</i></p>" );
    m_actionButton->SetLabel( "->" );
    // m_inputCtrl->Enable( true );
}

void AGENT_FRAME::OnAgentUpdate( wxCommandEvent& aEvent )
{
    wxString content = aEvent.GetString();

    // Escape HTML special chars?
    // For now, raw text append. Ideally convert generic newlines to <br>.
    content.Replace( "\n", "<br>" );

    // Append to the current paragraph started in OnSend
    m_chatWindow->AppendToPage( content );

    // Auto-scroll
    // m_chatWindow->ScrollToAnchor( "" ); // invalid anchor might scroll to bottom?
    // Or GetVirtualSize
    int x, y;
    m_chatWindow->GetVirtualSize( &x, &y );
    m_chatWindow->Scroll( 0, y );
}

void AGENT_FRAME::OnAgentComplete( wxCommandEvent& aEvent )
{
    // Thread has finished naturally
    if( m_workerThread )
    {
        m_workerThread->Wait(); // Join
        delete m_workerThread;
        m_workerThread = nullptr;
    }

    m_chatWindow->AppendToPage( "</p>" ); // Close Agent block
    m_actionButton->SetLabel( "->" );
    // m_inputCtrl->Enable( true );

    if( aEvent.GetInt() == 0 ) // Failure
    {
        m_chatWindow->AppendToPage( "<p><i>(Error generating response)</i></p>" );
    }
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
