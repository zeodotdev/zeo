#include "terminal_panel.h"
#include "terminal_frame.h"
#include "python_exec_thread.h"
#include <wx/sizer.h>
#include <wx/settings.h>
#include <wx/log.h>
#include <wx/utils.h>
#include <wx/process.h>
#include <wx/txtstrm.h>
#include <wx/msgdlg.h>
#include <wx/dir.h>
#include <wx/app.h>
#include <project.h>
#include <wildcards_and_files_ext.h>
#include <python_scripting.h>
#include <api/api_handler_editor.h>

BEGIN_EVENT_TABLE( TERMINAL_PANEL, wxPanel )
END_EVENT_TABLE()

TERMINAL_PANEL::TERMINAL_PANEL( wxWindow* aParent, TERMINAL_MODE aMode ) :
        wxPanel( aParent, wxID_ANY ),
        m_historyIndex( 0 ),
        m_mode( aMode ),
        m_lastPromptPos( 0 ),
        m_pythonInitialized( false ),
        m_process( nullptr ),
        m_shellStdin( nullptr ),
        m_shellStdout( nullptr ),
        m_shellStderr( nullptr ),
        m_pid( -1 ),
        m_pythonThread( nullptr ),
        m_pythonRunning( false ),
        m_pythonTimedOut( false )
{
    wxBoxSizer* mainSizer = new wxBoxSizer( wxVERTICAL );

    // Unified Output/Input Area
    m_outputCtrl = new wxTextCtrl( this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
                                   wxTE_MULTILINE | wxTE_RICH2 | wxTE_PROCESS_ENTER | wxTE_NOHIDESEL );

    // Set Monospace Font & Colors
    wxFont font = wxSystemSettings::GetFont( wxSYS_ANSI_FIXED_FONT );
    font.SetPointSize( 12 );
    m_outputCtrl->SetFont( font );
    m_outputCtrl->SetBackgroundColour( wxColour( 30, 30, 30 ) );    // Dark Grey
    m_outputCtrl->SetForegroundColour( wxColour( 255, 255, 255 ) ); // White
    m_outputCtrl->SetDefaultStyle( wxTextAttr( wxColour( 255, 255, 255 ), wxColour( 30, 30, 30 ) ) );

    // Initial Message
    m_outputCtrl->AppendText( "KiCad Dev Terminal\n" );
    m_outputCtrl->AppendText( "Type 'ipc' to enter IPC Python Shell.\n" );
    m_outputCtrl->AppendText( "Type 'exit' to switch back to System Shell.\n\n" );
    m_outputCtrl->AppendText( GetPrompt() );

    m_lastPromptPos = m_outputCtrl->GetLastPosition();

    mainSizer->Add( m_outputCtrl, 1, wxEXPAND | wxALL, 0 );

    SetSizer( mainSizer );
    Layout();

    // Bind Events
    m_outputCtrl->Bind( wxEVT_KEY_DOWN, &TERMINAL_PANEL::OnKeyDown, this );
    m_outputCtrl->Bind( wxEVT_CHAR, &TERMINAL_PANEL::OnChar, this );

    // Bind Python execution events
    Bind( wxEVT_PYTHON_OUTPUT, &TERMINAL_PANEL::OnPythonOutput, this );
    Bind( wxEVT_PYTHON_COMPLETE, &TERMINAL_PANEL::OnPythonComplete, this );

    // Set up timer for non-blocking Python execution polling
    // The timer needs an owner to send events to
    m_pythonPollTimer.SetOwner( this );
    Bind( wxEVT_TIMER, &TERMINAL_PANEL::OnPythonPollTimer, this, m_pythonPollTimer.GetId() );
}

TERMINAL_PANEL::~TERMINAL_PANEL()
{
    // Stop poll timer
    m_pythonPollTimer.Stop();

    // Stop any running Python thread
    if( m_pythonThread )
    {
        m_pythonThread->RequestStop();
        m_pythonThread->Wait();
        delete m_pythonThread;
        m_pythonThread = nullptr;
    }

    // Clear IPC shell blocking flag if we were running Python
    if( m_pythonRunning.load() )
    {
        m_pythonRunning.store( false );
        SetIPCShellBlocking( false );
    }

    CleanupShell();
}

void TERMINAL_PANEL::SetMode( TERMINAL_MODE aMode )
{
    m_mode = aMode;
}

wxString TERMINAL_PANEL::GetTitle() const
{
    switch( m_mode )
    {
    case MODE_SYSTEM: return "System";
    case MODE_PYTHON: return "Python";
    case MODE_IPC: return "IPC Shell";
    default: return "Terminal";
    }
}

wxString TERMINAL_PANEL::GetPrompt() const
{
    switch( m_mode )
    {
    case MODE_PYTHON: return PROMPT_PYTHON;
    case MODE_IPC: return PROMPT_IPC;
    default: return PROMPT_SYSTEM;
    }
}

void TERMINAL_PANEL::OnKeyDown( wxKeyEvent& aEvent )
{
    int key = aEvent.GetKeyCode();

    if( key == WXK_RETURN || key == WXK_NUMPAD_ENTER )
    {
        long     currentPos = m_outputCtrl->GetLastPosition();
        wxString fullText = m_outputCtrl->GetValue();

        if( m_lastPromptPos < fullText.Length() )
        {
            wxString cmd = fullText.Mid( m_lastPromptPos );
            cmd.Trim().Trim( false );

            m_outputCtrl->AppendText( "\n" );
            ExecuteCommand( cmd );
        }
        else
        {
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
        long pos = m_outputCtrl->GetInsertionPoint();
        if( pos <= m_lastPromptPos )
        {
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

void TERMINAL_PANEL::OnChar( wxKeyEvent& aEvent )
{
    if( m_outputCtrl->GetInsertionPoint() < m_lastPromptPos )
    {
        m_outputCtrl->SetInsertionPointEnd();
    }
    aEvent.Skip();
}

void TERMINAL_PANEL::ExecuteCommand( const wxString& aCmd )
{
    if( aCmd.Length() > 0 )
    {
        m_history.push_back( aCmd );
        m_historyIndex = m_history.size();
    }

    if( aCmd == "exit" )
    {
        if( m_mode != MODE_SYSTEM )
        {
            m_mode = MODE_SYSTEM;
            m_outputCtrl->AppendText( "Exited to System Shell.\n" );
        }
        // If system mode, exit is handled by frame or ignored here?
        // Let's just print exited. We can't close the tab easily from here without events.
        else
        {
            m_outputCtrl->AppendText( "Use the close tab button to close this terminal.\n" );
        }
    }
    else if( aCmd == "clear" )
    {
        m_outputCtrl->Clear();
    }
    else if( aCmd == "ipc" )
    {
        if( EnsurePython() )
        {
            m_mode = MODE_IPC;
            m_outputCtrl->AppendText( "Entering IPC Python Shell.\n" );
            m_outputCtrl->AppendText( "Usage:\n" );
            m_outputCtrl->AppendText( "  import kipy\n" );
            m_outputCtrl->AppendText( "  kicad = kipy.KiCad()\n" );
        }
    }
    else if( aCmd == "python" )
    {
        if( EnsurePython() )
        {
            m_mode = MODE_PYTHON;
            m_outputCtrl->AppendText( "Entering Python Mode.\n" );
        }
    }
    else
    {
        if( m_mode == MODE_SYSTEM )
        {
            ProcessSystemCommand( aCmd );
        }
        else
        {
            // RunLocalPython now returns immediately and uses a timer for completion.
            // The prompt will be shown by FinishPythonExecution() when Python completes.
            RunLocalPython( aCmd );
            m_outputCtrl->SetInsertionPointEnd();
            return; // Don't show prompt yet - timer will handle it
        }
    }

    m_outputCtrl->AppendText( GetPrompt() );
    m_lastPromptPos = m_outputCtrl->GetLastPosition();
    m_outputCtrl->SetInsertionPointEnd();
}

void TERMINAL_PANEL::InitShell()
{
    if( m_process )
        return;

    m_process = new wxProcess( this );
    m_process->Redirect();

    // Use zsh or bash
    wxString shell = "/bin/zsh"; // Mac default usually
    if( !wxFileExists( shell ) )
        shell = "/bin/bash";

    // Launch persistent shell
    // Interactive mode might be needed for some things (-i), but -s is safer for piping
    m_pid = wxExecute( shell + " -s", wxEXEC_ASYNC, m_process );

    if( m_pid > 0 )
    {
        m_shellStdin = m_process->GetOutputStream();
        m_shellStdout = m_process->GetInputStream();
        m_shellStderr = m_process->GetErrorStream();
    }
    else
    {
        m_outputCtrl->AppendText( "Error: Failed to launch system shell.\n" );
        delete m_process;
        m_process = nullptr;
    }
}

void TERMINAL_PANEL::CleanupShell()
{
    if( m_process )
    {
        // Unhook from this panel to prevent event callbacks
        m_process->Detach();

        if( m_process->GetOutputStream() )
        {
            // Try clean exit
            wxTextOutputStream os( *m_process->GetOutputStream() );
            os << "exit\n";
            m_process->CloseOutput();
        }

        if( m_pid > 0 && wxProcess::Exists( m_pid ) )
        {
            // Force Kill
            wxProcess::Kill( m_pid, wxSIGKILL );
        }

        // CRITICAL FIX: Do NOT delete m_process.
        // wxWidgets maintains a reference in wxExecuteData that triggers a crash (Use-After-Free)
        // when the process signal is handled asynchronously if this object is deleted.
        // The leak is minimal and necessary to prevent SIGSEGV on close.
        m_process = nullptr;
        m_pid = -1;
    }
}

std::string TERMINAL_PANEL::ProcessSystemCommand( const wxString& aCmd )
{
    InitShell();
    if( !m_process || !m_shellStdin )
        return "Error: Shell not running.\n";

    // Write Command + Sentinel
    // Use a unique sentinel that is unlikely to appear in output
    wxString sentinel = "__KICAD_AGENT_CMD_END__";
    wxString cmdLine = aCmd + "\n";
    cmdLine += "echo \"" + sentinel + "\"\n";

    // Clear any pending output first? (Flush)
    // Actually, hard to flush without blocking. We assume sync flow.

    // Write
    m_shellStdin->Write( cmdLine.c_str(), cmdLine.Length() );

    // Read Loop
    std::string fullOutput;
    bool        sentinelFound = false;

    // Safety timeout?
    wxLongLong startTime = wxGetLocalTimeMillis();
    long       timeoutMs = 15000; // 15 seconds timeout

    while( !sentinelFound )
    {
        // Check streams
        bool hasData = false;

        // STDOUT
        if( m_shellStdout && m_shellStdout->CanRead() )
        {
            char buf[1024];
            m_shellStdout->Read( buf, sizeof( buf ) - 1 );
            size_t count = m_shellStdout->LastRead();
            if( count > 0 )
            {
                buf[count] = 0;
                wxString chunk = wxString::FromUTF8( buf );

                // Check marker in chunk
                int pos = chunk.Find( sentinel );
                if( pos != wxNOT_FOUND )
                {
                    sentinelFound = true;
                    chunk = chunk.Left( pos ); // Keep only before marker
                    // Remove trailing newline from echo if possible?
                    chunk.Trim();
                }

                m_outputCtrl->AppendText( chunk );
                fullOutput += chunk.ToStdString();
                hasData = true;
            }
        }

        // STDERR
        if( m_shellStderr && m_shellStderr->CanRead() )
        {
            char buf[1024];
            m_shellStderr->Read( buf, sizeof( buf ) - 1 );
            size_t count = m_shellStderr->LastRead();
            if( count > 0 )
            {
                buf[count] = 0;
                wxString chunk = wxString::FromUTF8( buf );
                m_outputCtrl->AppendText( chunk );
                fullOutput += chunk.ToStdString();
                hasData = true;
            }
        }

        if( !hasData )
        {
            wxYield(); // Process GUI events
            wxMilliSleep( 10 );
        }

        if( wxGetLocalTimeMillis() - startTime > timeoutMs )
        {
            m_outputCtrl->AppendText( "\n[Timeout waiting for shell response]\n" );
            break;
        }
    }

    // Newline for visual separation
    m_outputCtrl->AppendText( "\n" );

    return fullOutput;
}

bool TERMINAL_PANEL::EnsurePython()
{
#ifdef KICAD_SCRIPTING
    if( m_pythonInitialized )
        return true;

    if( !SCRIPTING::IsWxAvailable() )
    {
        wxString stockPath = SCRIPTING::PyScriptingPath( SCRIPTING::STOCK );
        wxString userPath = SCRIPTING::PyScriptingPath( SCRIPTING::USER );

        if( !InitPythonScripting( stockPath.ToUTF8(), userPath.ToUTF8() ) )
        {
            m_outputCtrl->AppendText( "Error: Failed to initialize Python Scripting environment.\n" );
            return false;
        }
    }
    m_pythonInitialized = true;
    return true;
#else
    m_outputCtrl->AppendText( "Error: This build does not support Python.\n" );
    return false;
#endif
}

std::string TERMINAL_PANEL::RunLocalPython( const wxString& aCmd )
{
#ifdef KICAD_SCRIPTING
    if( !EnsurePython() )
        return "Error: Python not initialized";

    // If Python is already running, return error
    if( m_pythonRunning.load() )
        return "Error: Python execution already in progress";

    // Enable IPC shell blocking - this causes API modifications to be deferred
    // instead of executed immediately, to avoid macOS event loop corruption
    SetIPCShellBlocking( true );

    // Create and start the Python execution thread
    // The background thread will handle GIL acquisition/release
    m_pythonRunning.store( true );
    m_lastPythonResult.clear();
    m_pythonThread = new PYTHON_EXEC_THREAD( this, aCmd.ToStdString() );

    if( m_pythonThread->Run() != wxTHREAD_NO_ERROR )
    {
        delete m_pythonThread;
        m_pythonThread = nullptr;
        m_pythonRunning.store( false );
        SetIPCShellBlocking( false );  // Clear blocking flag on failure
        return "Error: Failed to start Python execution thread";
    }

    // Record start time for timeout checking
    m_pythonStartTime = wxGetLocalTimeMillis();

    // Start timer to poll for completion - this allows API events to be
    // processed through the NORMAL event loop, avoiding nested event
    // processing which causes memory corruption on macOS.
    m_pythonPollTimer.Start( 50 ); // Poll every 50ms

    // Return empty string - the result will be shown when Python completes
    // via the timer callback
    return "";
#else
    return "Error: Scripting not supported";
#endif
}


void TERMINAL_PANEL::OnPythonPollTimer( wxTimerEvent& aEvent )
{
    // Check if Python has completed
    if( !m_pythonRunning.load() )
    {
        FinishPythonExecution();
        return;
    }

    // Check for timeout (60 seconds)
    long timeoutMs = 60000;
    if( wxGetLocalTimeMillis() - m_pythonStartTime > timeoutMs )
    {
        m_pythonPollTimer.Stop();

        if( m_pythonThread )
        {
            m_pythonThread->RequestStop();
            m_pythonThread->Wait();
            delete m_pythonThread;
            m_pythonThread = nullptr;
        }
        m_pythonRunning.store( false );

        // Set timeout flag before finishing (callback will check this)
        m_pythonTimedOut = true;

        m_outputCtrl->AppendText( "[Timeout waiting for Python execution]\n" );

        // Use FinishPythonExecution to handle callback and cleanup consistently
        FinishPythonExecution();
    }
}


void TERMINAL_PANEL::FinishPythonExecution()
{
    m_pythonPollTimer.Stop();

    // Clean up thread
    if( m_pythonThread )
    {
        m_pythonThread->Wait();
        delete m_pythonThread;
        m_pythonThread = nullptr;
    }

    // Clear IPC shell blocking flag - Python execution is complete
    // Deferred operations were already scheduled via CallAfter by each handler,
    // they will execute now that we're clearing the blocking flag
    SetIPCShellBlocking( false );

    // Invoke completion callback if set (for async agent requests)
    if( m_pythonCompletionCallback )
    {
        bool success = !m_pythonTimedOut;
        std::string result = m_pythonTimedOut
            ? "Error: Python execution timed out"
            : ( m_lastPythonResult.empty() ? "(no output)" : m_lastPythonResult );

        // Call the callback (this will typically send ExpressMail back to agent)
        m_pythonCompletionCallback( result, success );

        // Clear callback after invoking (single-shot)
        m_pythonCompletionCallback = nullptr;
    }

    // Reset timeout flag
    m_pythonTimedOut = false;

    // Show the prompt for next command
    m_outputCtrl->AppendText( GetPrompt() );
    m_lastPromptPos = m_outputCtrl->GetLastPosition();
}


void TERMINAL_PANEL::OnPythonOutput( wxThreadEvent& aEvent )
{
    wxString output = aEvent.GetString();
    if( !output.IsEmpty() )
    {
        m_outputCtrl->AppendText( output );
    }
}


void TERMINAL_PANEL::OnPythonComplete( wxThreadEvent& aEvent )
{
    wxString result = aEvent.GetString();

    if( !result.IsEmpty() )
    {
        m_outputCtrl->AppendText( result );
    }

    m_lastPythonResult = result.ToStdString();
    m_pythonRunning.store( false );
}


void TERMINAL_PANEL::SetPythonCompletionCallback( PythonCompletionCallback aCallback )
{
    m_pythonCompletionCallback = aCallback;
}


void TERMINAL_PANEL::ClearPythonCompletionCallback()
{
    m_pythonCompletionCallback = nullptr;
}


void TERMINAL_PANEL::DisplayAgentCommand( const wxString& aCmd, const wxString& aMode )
{
    // Display the command being executed by the agent
    // Format: [agent:mode] >>> <command>
    wxString header;
    if( aMode == "sch" )
        header = "[agent:schematic] ";
    else if( aMode == "pcb" )
        header = "[agent:pcb] ";
    else if( aMode == "shell" )
        header = "[agent:shell] $ ";
    else
        header = "[agent] ";

    // Add a newline before if we're not at the start of a line
    long lastPos = m_outputCtrl->GetLastPosition();
    if( lastPos > 0 )
    {
        wxString lastChar = m_outputCtrl->GetRange( lastPos - 1, lastPos );
        if( lastChar != "\n" )
            m_outputCtrl->AppendText( "\n" );
    }

    m_outputCtrl->AppendText( header );

    // For multi-line commands, indent continuation lines
    wxString cmd = aCmd;
    cmd.Replace( "\n", "\n    " );
    m_outputCtrl->AppendText( cmd );
    m_outputCtrl->AppendText( "\n" );

    // Scroll to show the command
    m_outputCtrl->ShowPosition( m_outputCtrl->GetLastPosition() );
}
