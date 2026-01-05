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
    m_outputCtrl->SetBackgroundColour( wxColour( 30, 30, 30 ) );    // Dark Grey
    m_outputCtrl->SetForegroundColour( wxColour( 255, 255, 255 ) ); // White
    m_outputCtrl->SetDefaultStyle( wxTextAttr( wxColour( 255, 255, 255 ), wxColour( 30, 30, 30 ) ) );

    // Initial Message
    m_outputCtrl->AppendText( "KiCad Dev Terminal\n" );
    m_outputCtrl->AppendText( "Type 'pcb' to enter PCB Scripting Mode.\n" );
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

#include <wx/msgdlg.h>
#include <wx/dir.h>
#include <python_scripting.h>
#include <project.h>                 // For Prj()
#include <wildcards_and_files_ext.h> // For PcbFileExtension

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
        if( m_mode != MODE_SYSTEM )
        {
            m_mode = MODE_SYSTEM;
            m_outputCtrl->AppendText( "Exited to System Shell.\n" );
        }
        else
        {
            Close( true );
            return; // Close destroys window
        }
    }
    else if( aCmd.StartsWith( "pcb" ) )
    {
        if( EnsurePython() )
        {
            m_mode = MODE_PCB;
            m_outputCtrl->AppendText( "Entering PCB Mode (Standard Python with auto-loaded PCB).\n" );

            wxString pcbFile;
            wxString arg = aCmd.Mid( 3 ).Trim( false ).Trim(); // Get rest of line

            if( !arg.IsEmpty() )
            {
                pcbFile = arg;
                if( !wxFileName::FileExists( pcbFile ) )
                {
                    // Try resolving relative to CWD
                    wxFileName fn( pcbFile );
                    if( fn.MakeAbsolute( wxGetCwd() ) && fn.FileExists() )
                        pcbFile = fn.GetFullPath();
                }
            }

            // Auto-load PCB attempts
            if( pcbFile.IsEmpty() )
            {
                // 1. Try Project Manager
                pcbFile = Prj().GetProjectFullName();

                if( !pcbFile.IsEmpty() )
                {
                    wxFileName fn( pcbFile );
                    if( fn.GetExt() == "kicad_pro" )
                    {
                        fn.SetExt( FILEEXT::KiCadPcbFileExtension );
                        pcbFile = fn.GetFullPath();
                    }
                }

                // 2. Try CWD for .kicad_pcb
                if( pcbFile.IsEmpty() || !wxFileExists( pcbFile ) )
                {
                    wxDir    dir( wxGetCwd() );
                    wxString filename;
                    if( dir.GetFirst( &filename, "*.kicad_pcb", wxDIR_FILES ) )
                    {
                        pcbFile = wxGetCwd() + wxFileName::GetPathSeparator() + filename;
                    }
                }
            }

            // Correction for missing extension
            if( !pcbFile.IsEmpty() && !pcbFile.EndsWith( FILEEXT::KiCadPcbFileExtension ) )
            {
                // Only append if file doesn't exist as is? Or assume user forgot extension?
                // Let's assume user forgot if it doesn't exist.
                if( !wxFileExists( pcbFile ) )
                    pcbFile += FILEEXT::KiCadPcbFileExtension;
            }

            if( !pcbFile.IsEmpty() && wxFileExists( pcbFile ) )
            {
                m_outputCtrl->AppendText( "Loading PCB: " + pcbFile + "\n" );
                wxString loadCmd = wxString::Format( "import pcbnew\n"
                                                     "try:\n"
                                                     "    board = pcbnew.LoadBoard(\"%s\")\n"
                                                     "    p = board\n"
                                                     "    print(\"PCB loaded. Access via 'board' or 'p'.\")\n"
                                                     "except Exception as e:\n"
                                                     "    print(f\"Failed to load board: {e}\")\n",
                                                     pcbFile );
                RunLocalPython( loadCmd );
            }
            else
            {
                m_outputCtrl->AppendText( "Warning: No PCB file found to auto-load.\n" );
                m_outputCtrl->AppendText( "Current Directory: " + wxGetCwd() + "\n" );
                if( !arg.IsEmpty() )
                    m_outputCtrl->AppendText( "File not found: " + arg + "\n" );

                m_outputCtrl->AppendText(
                        "Use 'cd <path>' to navigate to your project or 'pcb <filename.kicad_pcb>'.\n" );
                m_outputCtrl->AppendText( "Running standard python.\n" );
                RunLocalPython( "import pcbnew\n" );
            }
        }
    }
    else if( aCmd == "python" )
    {
        if( EnsurePython() )
        {
            m_mode = MODE_PYTHON;
            m_outputCtrl->AppendText( "Entering Standard Python Mode.\n" );
            // Maybe clear context?
            // For now, share the same interpreter session.
        }
    }
    else if( aCmd == "clear" )
    {
        m_outputCtrl->Clear();
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
            RunLocalPython( aCmd );
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
    long          ret = wxExecute( aCmd, output, errors, wxEXEC_SYNC );

    for( const wxString& line : output )
        m_outputCtrl->AppendText( line + "\n" );

    for( const wxString& line : errors )
        m_outputCtrl->AppendText( line + "\n" );
}

void TERMINAL_FRAME::ProcessAgentCommand( const wxString& aCmd )
{
    // Deprecated / Unused logic for remote execution
    RunLocalPython( aCmd );
}

bool TERMINAL_FRAME::EnsurePython()
{
#ifdef KICAD_SCRIPTING
    if( m_pythonInitialized )
        return true;

    if( !SCRIPTING::IsWxAvailable() )
    {
        // Initialize Python Scripting
        wxString stockPath = SCRIPTING::PyScriptingPath( SCRIPTING::STOCK );
        wxString userPath = SCRIPTING::PyScriptingPath( SCRIPTING::USER );

        if( !InitPythonScripting( stockPath.ToUTF8(), userPath.ToUTF8() ) )
        {
            m_outputCtrl->AppendText( "Error: Failed to initialize Python Scripting environment.\n" );
            return false;
        }
    }

    // We just return true if compiled with scripting.
    // Real initialization might happen on first run if not already done.
    m_pythonInitialized = true;
    return true;
#else
    m_outputCtrl->AppendText( "Error: This build of Terminal does not support Python.\n" );
    return false;
#endif
}

void TERMINAL_FRAME::RunLocalPython( const wxString& aCmd )
{
#ifdef KICAD_SCRIPTING
    if( !EnsurePython() )
        return;

    // Acquire GIL
    PyLOCK lock;

    // Capture stdout/stderr
    // Assuming simple string execution for now

    std::string code = aCmd.ToStdString();

    // Redirect logic similar to cross-probing.cpp
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

    // Extract result
    PyObject* main_module = PyImport_AddModule( "__main__" );
    PyObject* main_dict = PyModule_GetDict( main_module );
    PyObject* res_obj = PyDict_GetItemString( main_dict, "_term_result" );
    if( res_obj )
    {
        const char* res_str = PyUnicode_AsUTF8( res_obj );
        if( res_str )
            m_outputCtrl->AppendText( wxString::FromUTF8( res_str ) );
    }
#endif
}

void TERMINAL_FRAME::OnTextEnter( wxCommandEvent& aEvent )
{
    // Unused
}

void TERMINAL_FRAME::KiwayMailIn( KIWAY_EXPRESS& aEvent )
{
    // Handle incoming mail if needed (e.g. from PCB editor if we still want that)
    // For now, ignore since we are running local python.
}
