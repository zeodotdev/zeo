#include "terminal_frame.h"
#include <kiway_express.h>
#include <mail_type.h>
#include <wx/sizer.h>
#include <wx/settings.h>
#include <id.h>
#include <kiway.h>
#include <wx/log.h>
#include <wx/utils.h>
#include <wx/process.h>
#include <wx/txtstrm.h>

#include <base_units.h>

BEGIN_EVENT_TABLE( TERMINAL_FRAME, KIWAY_PLAYER )
EVT_MENU( wxID_EXIT, TERMINAL_FRAME::OnExit )
END_EVENT_TABLE()

TERMINAL_FRAME::TERMINAL_FRAME( KIWAY* aKiway, wxWindow* aParent ) :
        KIWAY_PLAYER( aKiway, aParent, FRAME_TERMINAL, "Terminal", wxDefaultPosition, wxDefaultSize,
                      wxDEFAULT_FRAME_STYLE, "terminal_frame_name", schIUScale ),
        m_historyIndex( 0 ),
        m_mode( MODE_SYSTEM ),
        m_lastPromptPos( 0 )
{
    wxBoxSizer* mainSizer = new wxBoxSizer( wxVERTICAL );

    // Unified Output/Input Area
    // wxTE_PROCESS_ENTER needed to catch Enter key in OnKeyDown on some platforms/configs
    m_outputCtrl = new wxTextCtrl( this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
                                   wxTE_MULTILINE | wxTE_RICH2 | wxTE_PROCESS_ENTER | wxTE_NOHIDESEL );

    // Set Monospace Font & Colors
    wxFont font = wxSystemSettings::GetFont( wxSYS_ANSI_FIXED_FONT );
    font.SetPointSize( 12 );
    m_outputCtrl->SetFont( font );
    m_outputCtrl->SetBackgroundColour( wxColour( 30, 30, 30 ) ); // Dark Grey
    m_outputCtrl->SetForegroundColour( wxColour( 0, 255, 0 ) );  // Green

    // Initial Message
    m_outputCtrl->AppendText( "KiCad Hybrid Terminal\n" );
    m_outputCtrl->AppendText( "Type 'pcb' or 'python' to enter PCB Scripting Mode.\n" );
    m_outputCtrl->AppendText( "Type 'exit' to switch back to System Shell.\n\n" );
    m_outputCtrl->AppendText( GetPrompt() );

    m_lastPromptPos = m_outputCtrl->GetLastPosition();

    mainSizer->Add( m_outputCtrl, 1, wxEXPAND | wxALL, 0 );

    SetSizer( mainSizer );
    Layout();
    SetSize( 800, 600 ); // Larger default size

    // Bind Events
    m_outputCtrl->Bind( wxEVT_KEY_DOWN, &TERMINAL_FRAME::OnKeyDown, this );
    m_outputCtrl->Bind( wxEVT_CHAR, &TERMINAL_FRAME::OnChar, this );
}

TERMINAL_FRAME::~TERMINAL_FRAME()
{
}

void TERMINAL_FRAME::OnExit( wxCommandEvent& event )
{
    Close( true );
}

void TERMINAL_FRAME::OnKeyDown( wxKeyEvent& aEvent )
{
    int key = aEvent.GetKeyCode();

    if( key == WXK_RETURN || key == WXK_NUMPAD_ENTER )
    {
        long currentPos = m_outputCtrl->GetLastPosition();
        // If cursor is before prompt, move it to end? For now, assume user types at end.

        wxString fullText = m_outputCtrl->GetValue();
        if( m_lastPromptPos < fullText.Length() )
        {
            wxString cmd = fullText.Mid( m_lastPromptPos );
            // Remove newlines if any
            cmd.Trim().Trim( false );

            m_outputCtrl->AppendText( "\n" ); // Move to next line
            ExecuteCommand( cmd );

            // Prompt is added by ExecuteCommand or its sub-methods
        }
        else
        {
            // Empty command
            m_outputCtrl->AppendText( "\n" + GetPrompt() );
            m_lastPromptPos = m_outputCtrl->GetLastPosition();
        }

        return;
    }
    else if( key == WXK_UP )
    {
        if( m_history.empty() )
            return;

        if( m_historyIndex > 0 )
            m_historyIndex--;

        // Replace current input with history
        if( m_historyIndex >= 0 && m_historyIndex < (int) m_history.size() )
        {
            m_outputCtrl->Remove( m_lastPromptPos, m_outputCtrl->GetLastPosition() );
            m_outputCtrl->AppendText( m_history[m_historyIndex] );
        }
        return;
    }
    else if( key == WXK_DOWN )
    {
        if( m_history.empty() )
            return;

        if( m_historyIndex < (int) m_history.size() )
            m_historyIndex++;

        m_outputCtrl->Remove( m_lastPromptPos, m_outputCtrl->GetLastPosition() );

        if( m_historyIndex < (int) m_history.size() )
        {
            m_outputCtrl->AppendText( m_history[m_historyIndex] );
        }
        return;
    }
    else if( key == WXK_BACK || key == WXK_LEFT )
    {
        // Prevent moving/deleting before prompt
        long pos = m_outputCtrl->GetInsertionPoint();
        if( pos <= m_lastPromptPos )
        {
            // Allow Left if we are strictly greater than prompt pos?
            // Actually, simplest is just block if at boundary and trying to go back
            if( key == WXK_LEFT && pos == m_lastPromptPos )
                return;
            if( key == WXK_BACK && pos == m_lastPromptPos )
                return;
        }
    }
    else if( key == WXK_HOME )
    {
        m_outputCtrl->SetInsertionPoint( m_lastPromptPos );
        return;
    }

    aEvent.Skip();
}

void TERMINAL_FRAME::OnChar( wxKeyEvent& aEvent )
{
    // Prevent typing before prompt
    if( m_outputCtrl->GetInsertionPoint() < m_lastPromptPos )
    {
        m_outputCtrl->SetInsertionPointEnd();
    }
    aEvent.Skip();
}

void TERMINAL_FRAME::ExecuteCommand( const wxString& aCmd )
{
    // Add to history
    if( aCmd.Length() > 0 )
    {
        m_history.push_back( aCmd );
        m_historyIndex = m_history.size(); // Reset to end
    }

    // Check for Internal Commands
    if( aCmd == "exit" )
    {
        if( m_mode == MODE_PCB_PYTHON )
        {
            m_mode = MODE_SYSTEM;
            m_outputCtrl->AppendText( "Exited Python Mode.\n" );
        }
        else
        {
            Close( true );
            return;
        }
    }
    else if( aCmd == "pcb" || aCmd == "python" )
    {
        m_mode = MODE_PCB_PYTHON;
        m_outputCtrl->AppendText( "Entering PCB Python Mode.\n" );
    }
    else if( aCmd == "clear" )
    {
        m_outputCtrl->Clear();
        // m_lastPromptPos reset handled below
    }
    else if( aCmd.StartsWith( "cd " ) && m_mode == MODE_SYSTEM )
    {
        wxString path = aCmd.Mid( 3 ).Trim().Trim( false );
        if( wxSetWorkingDirectory( path ) )
        {
            // Success
        }
        else
        {
            m_outputCtrl->AppendText( "cd: no such file or directory: " + path + "\n" );
        }
    }
    else
    {
        if( m_mode == MODE_SYSTEM )
            ProcessSystemCommand( aCmd );
        else
            ProcessAgentCommand( aCmd );
    }

    // Ready for next command
    m_outputCtrl->AppendText( GetPrompt() );
    m_lastPromptPos = m_outputCtrl->GetLastPosition();
    m_outputCtrl->SetInsertionPointEnd();
}

void TERMINAL_FRAME::ProcessSystemCommand( const wxString& aCmd )
{
    if( aCmd.IsEmpty() )
        return;

    wxArrayString output, errors;
    // Redirect stderr to stdout to capture everything
    // Use wxExecute with wxEXEC_SYNC to simplify for now, though it blocks UI
    int code = wxExecute( aCmd, output, errors, wxEXEC_SYNC );

    for( const wxString& line : output )
        m_outputCtrl->AppendText( line + "\n" );

    for( const wxString& line : errors )
        m_outputCtrl->AppendText( line + "\n" );
}

void TERMINAL_FRAME::ProcessAgentCommand( const wxString& aCmd )
{
    if( aCmd.IsEmpty() )
        return;

    // Route to PCB Editor
    FRAME_T dest = FRAME_PCB_EDITOR;

    // Construct Payload
    // If it starts with '.', treat as magic/agent command?
    // For now, treat EVERYTHING as a "python" type request if in Python mode

    // But wait, "test_diff" etc are "agent requests" not python commands.
    // Let's support legacy agent commands too.

    std::string payloadStr;

    if( aCmd == "test_diff" || aCmd == "propose_settings" || aCmd.StartsWith( "get_" ) )
    {
        // Legacy Agent JSON command
        // Construct JSON manually for now to avoid dependency here?
        // Actually, let's just send the string. logic in cross-probing handles parsing.
        // IF we send raw string "test_diff", it fails JSON parse.
        // user previously typed "test_diff" and it worked? Ah no, previous implementation used string contains.
        // BUT cross-probing.cpp expects JSON now?
        // "nlohmann::json j_in = nlohmann::json::parse( SafePayload )"

        // So "test_diff" is NOT valid JSON.
        // We should ideally wrap it.
        // Or the user is expected to type JSON? "test_diff" was a hack in `terminal_frame.cpp` previously?
        // Looking at previous code:
        // "Kiway().ExpressMail( dest, MAIL_AGENT_REQUEST, payload...)"
        // And `cross-probing.cpp` parses it.

        // If the user types "test_diff", that's not JSON.
        // I should fix this to send properly formatted requests.

        if( aCmd == "test_diff" )
        {
            payloadStr = "{ \"type\": \"test_diff\" }";
        }
        else if( aCmd == "propose_settings" )
        {
            payloadStr = "{ \"type\": \"propose_settings\" }";
        }
        else
        {
            // Assume it's a python command
            // Escape quotes?
            // Simple JSON construction:
            wxString escaped = aCmd;
            escaped.Replace( "\\", "\\\\" );
            escaped.Replace( "\"", "\\\"" );
            escaped.Replace( "\n", "\\n" );

            payloadStr = "{ \"type\": \"python\", \"code\": \"" + escaped.ToStdString() + "\" }";
        }
    }
    else
    {
        // Default Python Wrapper
        wxString escaped = aCmd;
        escaped.Replace( "\\", "\\\\" );
        escaped.Replace( "\"", "\\\"" );
        escaped.Replace( "\n", "\\n" );

        payloadStr = "{ \"type\": \"python\", \"code\": \"" + escaped.ToStdString() + "\" }";
    }

    KIWAY_PLAYER* player = Kiway().Player( dest, false );
    if( player )
    {
        Kiway().ExpressMail( dest, MAIL_AGENT_REQUEST, payloadStr, this );
    }
    else
    {
        m_outputCtrl->AppendText( "Error: PCB Editor not open.\n" );
    }
}

void TERMINAL_FRAME::OnTextEnter( wxCommandEvent& aEvent )
{
    // Legacy handler, unused
}

void TERMINAL_FRAME::KiwayMailIn( KIWAY_EXPRESS& aEvent )
{
    if( aEvent.Command() == MAIL_AGENT_RESPONSE )
    {
        std::string payload = aEvent.GetPayload();
        // Parse JSON? Or just print result
        // Agent response is usually JSON.
        // If python, we might change the response format.
        // For now, direct dump.
        m_outputCtrl->AppendText( payload + "\n" );

        // Force scroll to bottom?
        m_outputCtrl->ShowPosition( m_outputCtrl->GetLastPosition() );
    }
}
