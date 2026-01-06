#include "terminal_panel.h"
#include "terminal_frame.h"
#include <wx/sizer.h>
#include <wx/settings.h>
#include <wx/log.h>
#include <wx/utils.h>
#include <wx/process.h>
#include <wx/txtstrm.h>
#include <wx/msgdlg.h>
#include <wx/dir.h>
#include <project.h>
#include <wildcards_and_files_ext.h>
#include <python_scripting.h>

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
        m_pid( -1 )
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
}

TERMINAL_PANEL::~TERMINAL_PANEL()
{
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
            ProcessSystemCommand( aCmd );
        else
            RunLocalPython( aCmd );
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

    PyLOCK      lock;
    std::string code = aCmd.ToStdString();

    std::string wrapper = "import sys\n"
                          "from io import StringIO\n"
                          "_term_capture = StringIO()\n"
                          "_term_restore_out = sys.stdout\n"
                          "_term_restore_err = sys.stderr\n"
                          "sys.stdout = _term_capture\n"
                          "sys.stderr = _term_capture\n"
                          "try:\n"
                          "    exec(\"\"\""
                          + code
                          + "\"\"\")\n"
                            "except Exception as e:\n"
                            "    import traceback\n"
                            "    traceback.print_exc()\n"
                            "finally:\n"
                            "    sys.stdout = _term_restore_out\n"
                            "    sys.stderr = _term_restore_err\n"
                            "_term_result = _term_capture.getvalue()\n";

    PyRun_SimpleString( wrapper.c_str() );

    std::string resultStr;
    PyObject*   main_module = PyImport_AddModule( "__main__" );
    PyObject*   main_dict = PyModule_GetDict( main_module );
    PyObject*   res_obj = PyDict_GetItemString( main_dict, "_term_result" );
    if( res_obj )
    {
        const char* res_str = PyUnicode_AsUTF8( res_obj );
        if( res_str )
        {
            resultStr = res_str;
            m_outputCtrl->AppendText( wxString::FromUTF8( res_str ) );
        }
    }
    return resultStr;
#else
    return "Error: Scripting not supported";
#endif
}
