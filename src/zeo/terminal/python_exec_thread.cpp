#include "python_exec_thread.h"
#include <python_scripting.h>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#else
#include <unistd.h>    // for write(), close()
#endif
#include <stdlib.h>    // for mkstemps()
#include <wx/log.h>

// Define the custom events
wxDEFINE_EVENT( wxEVT_PYTHON_OUTPUT, wxThreadEvent );
wxDEFINE_EVENT( wxEVT_PYTHON_COMPLETE, wxThreadEvent );


PYTHON_EXEC_THREAD::PYTHON_EXEC_THREAD( wxEvtHandler* aHandler, const std::string& aCode ) :
        wxThread( wxTHREAD_JOINABLE ),
        m_handler( aHandler ),
        m_code( aCode ),
        m_stopRequested( false ),
        m_pythonThreadId( 0 )
{
}


PYTHON_EXEC_THREAD::~PYTHON_EXEC_THREAD()
{
}


void* PYTHON_EXEC_THREAD::Entry()
{
    if( !m_handler )
        return nullptr;

    // Acquire GIL for this thread
    PyGILState_STATE gilState = PyGILState_Ensure();

    // Store Python thread ID so InterruptPython() can target this thread
    m_pythonThreadId.store( PyThread_get_thread_ident() );
    wxLogInfo( "HEADLESS_EXEC: Python thread acquired GIL, thread_id=%lu", m_pythonThreadId.load() );

    std::string resultStr;
    bool        success = true;

    try
    {
        // Write code to a temp file to avoid string escaping issues with exec()
        // This handles \n in f-strings, multi-line strings, and other edge cases
        std::string tempFilePath;
        {
#ifdef _WIN32
            wchar_t tempDir[MAX_PATH];
            GetTempPathW( MAX_PATH, tempDir );
            wchar_t tempFile[MAX_PATH];
            GetTempFileNameW( tempDir, L"kca", 0, tempFile );
            // Rename with .py extension
            std::wstring pyFile = std::wstring( tempFile ) + L".py";
            _wrename( tempFile, pyFile.c_str() );
            int fd = _wopen( pyFile.c_str(), _O_WRONLY | _O_BINARY | _O_CREAT | _O_TRUNC, 0600 );
            if( fd != -1 )
            {
                char narrowPath[MAX_PATH];
                wcstombs( narrowPath, pyFile.c_str(), MAX_PATH );
                tempFilePath = narrowPath;
                _write( fd, m_code.c_str(), (unsigned int) m_code.length() );
                _close( fd );
            }
#else
            // Create temp file with Python code
            char tempTemplate[] = "/tmp/kicad_agent_XXXXXX.py";
            int fd = mkstemps( tempTemplate, 3 );  // .py suffix
            if( fd != -1 )
            {
                tempFilePath = tempTemplate;
                write( fd, m_code.c_str(), m_code.length() );
                close( fd );
            }
#endif
        }

        std::string wrapper;
        if( !tempFilePath.empty() )
        {
            // Use temp file approach - more robust
            // Escape backslashes in path for Python string literal
            std::string escapedPath = tempFilePath;
            for( size_t pos = 0; ( pos = escapedPath.find( '\\', pos ) ) != std::string::npos; pos += 2 )
                escapedPath.insert( pos, "\\" );

            wrapper = "import sys\n"
                      "from io import StringIO\n"
                      "_term_capture = StringIO()\n"
                      "_term_restore_out = sys.stdout\n"
                      "_term_restore_err = sys.stderr\n"
                      "sys.stdout = _term_capture\n"
                      "sys.stderr = _term_capture\n"
                      "_term_code_file = '" + escapedPath + "'\n"
                      "try:\n"
                      "    with open(_term_code_file, 'r') as f:\n"
                      "        _term_code = f.read()\n"
                      "    exec(compile(_term_code, _term_code_file, 'exec'))\n"
                      "except SystemExit as e:\n"
                      "    print(f'Script exited (code={e.code})')\n"
                      "except KeyboardInterrupt:\n"
                      "    print('KeyboardInterrupt')\n"
                      "except Exception as e:\n"
                      "    import traceback\n"
                      "    traceback.print_exc()\n"
                      "finally:\n"
                      "    sys.stdout = _term_restore_out\n"
                      "    sys.stderr = _term_restore_err\n"
                      "    import os\n"
                      "    try:\n"
                      "        os.remove(_term_code_file)\n"
                      "    except:\n"
                      "        pass\n"
                      "_term_result = _term_capture.getvalue()\n";
        }
        else
        {
            // Fallback to original approach if temp file creation fails
            // Note: This may have issues with escape sequences in code
            wrapper = "import sys\n"
                      "from io import StringIO\n"
                      "_term_capture = StringIO()\n"
                      "_term_restore_out = sys.stdout\n"
                      "_term_restore_err = sys.stderr\n"
                      "sys.stdout = _term_capture\n"
                      "sys.stderr = _term_capture\n"
                      "try:\n"
                      "    exec(\"\"\"" + m_code + "\"\"\")\n"
                      "except SystemExit as e:\n"
                      "    print(f'Script exited (code={e.code})')\n"
                      "except KeyboardInterrupt:\n"
                      "    print('KeyboardInterrupt')\n"
                      "except Exception as e:\n"
                      "    import traceback\n"
                      "    traceback.print_exc()\n"
                      "finally:\n"
                      "    sys.stdout = _term_restore_out\n"
                      "    sys.stderr = _term_restore_err\n"
                      "_term_result = _term_capture.getvalue()\n";
        }

        // Use PyRun_StringFlags instead of PyRun_SimpleString so we can
        // intercept SystemExit BEFORE PyErr_Print() calls Py_Exit().
        // PyRun_SimpleString calls PyErr_Print internally which handles
        // SystemExit by calling Py_Exit() - terminating the process.
        PyObject* main_module = PyImport_AddModule( "__main__" );
        PyObject* main_dict = main_module ? PyModule_GetDict( main_module ) : nullptr;
        PyObject* pyResult = nullptr;
        int retCode = -1;

        if( main_dict )
        {
            pyResult = PyRun_StringFlags( wrapper.c_str(), Py_file_input,
                                          main_dict, main_dict, nullptr );
        }

        if( pyResult )
        {
            Py_DECREF( pyResult );
            retCode = 0;
        }
        else if( PyErr_Occurred() )
        {
            if( PyErr_ExceptionMatches( PyExc_SystemExit ) )
            {
                // SystemExit escaped the Python wrapper - suppress it to
                // prevent Py_Exit() from terminating the process
                PyErr_Clear();
            }
            else
            {
                // Print non-SystemExit errors normally
                PyErr_Print();
            }
        }

        if( retCode == 0 )
        {
            // Retrieve captured output from Python
            PyObject* main_module = PyImport_AddModule( "__main__" );
            PyObject* main_dict = PyModule_GetDict( main_module );
            PyObject* res_obj = PyDict_GetItemString( main_dict, "_term_result" );

            if( res_obj )
            {
                const char* res_str = PyUnicode_AsUTF8( res_obj );
                if( res_str )
                    resultStr = res_str;
            }
        }
        else
        {
            success = false;
            resultStr = "Python execution error\n";
        }
    }
    catch( ... )
    {
        success = false;
        resultStr = "Exception during Python execution\n";
    }

    // Clear thread ID before releasing GIL
    m_pythonThreadId.store( 0 );

    // Release GIL before posting events
    PyGILState_Release( gilState );

    // Post completion event with result (only if not stopped and handler still valid)
    if( !TestDestroy() && !m_stopRequested.load() )
    {
        wxLogInfo( "HEADLESS_EXEC: Python thread completed, posting wxEVT_PYTHON_COMPLETE event" );
        wxThreadEvent* event = new wxThreadEvent( wxEVT_PYTHON_COMPLETE );
        event->SetString( wxString::FromUTF8( resultStr.c_str() ) );
        event->SetInt( success ? 1 : 0 );
        wxQueueEvent( m_handler, event );
        wxLogInfo( "HEADLESS_EXEC: wxQueueEvent called, event posted to handler %p", m_handler );
    }
    else
    {
        wxLogInfo( "HEADLESS_EXEC: Python thread completed but NOT posting event "
                   "(TestDestroy=%d, stopRequested=%d)",
                   TestDestroy(), m_stopRequested.load() );
    }

    return nullptr;
}


bool PYTHON_EXEC_THREAD::InterruptPython()
{
    unsigned long threadId = m_pythonThreadId.load();

    if( threadId == 0 )
    {
        wxLogInfo( "HEADLESS_EXEC: InterruptPython called but no Python thread ID — "
                   "thread may not have started yet" );
        return false;
    }

    wxLogInfo( "HEADLESS_EXEC: InterruptPython — setting stop flag for thread %lu", threadId );

    // We intentionally do NOT call PyThreadState_SetAsyncExc here.
    // That function requires the GIL, but acquiring the GIL from the main thread
    // can deadlock: the Python thread may be blocked in an IPC call waiting for
    // the main thread to process it via wxYield, while the main thread blocks
    // waiting for the GIL.
    //
    // Instead, we set m_stopRequested so the thread knows it was cancelled.
    // The Python thread will finish its current operation naturally.
    // The caller (CancelExecution) handles force-completing the execution state
    // so the app doesn't hang waiting for the thread.
    m_stopRequested.store( true );

    wxLogInfo( "HEADLESS_EXEC: InterruptPython — stop requested" );
    return true;
}
