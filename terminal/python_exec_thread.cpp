/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "python_exec_thread.h"
#include <python_scripting.h>
#include <unistd.h>    // for write(), close()
#include <stdlib.h>    // for mkstemps()

// Define the custom events
wxDEFINE_EVENT( wxEVT_PYTHON_OUTPUT, wxThreadEvent );
wxDEFINE_EVENT( wxEVT_PYTHON_COMPLETE, wxThreadEvent );


PYTHON_EXEC_THREAD::PYTHON_EXEC_THREAD( wxEvtHandler* aHandler, const std::string& aCode ) :
        wxThread( wxTHREAD_JOINABLE ),
        m_handler( aHandler ),
        m_code( aCode ),
        m_stopRequested( false )
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

    std::string resultStr;
    bool        success = true;

    try
    {
        // Write code to a temp file to avoid string escaping issues with exec()
        // This handles \n in f-strings, multi-line strings, and other edge cases
        std::string tempFilePath;
        {
            // Create temp file with Python code
            char tempTemplate[] = "/tmp/kicad_agent_XXXXXX.py";
            int fd = mkstemps( tempTemplate, 3 );  // .py suffix
            if( fd != -1 )
            {
                tempFilePath = tempTemplate;
                write( fd, m_code.c_str(), m_code.length() );
                close( fd );
            }
        }

        std::string wrapper;
        if( !tempFilePath.empty() )
        {
            // Use temp file approach - more robust
            wrapper = "import sys\n"
                      "from io import StringIO\n"
                      "_term_capture = StringIO()\n"
                      "_term_restore_out = sys.stdout\n"
                      "_term_restore_err = sys.stderr\n"
                      "sys.stdout = _term_capture\n"
                      "sys.stderr = _term_capture\n"
                      "_term_code_file = '" + tempFilePath + "'\n"
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

    // Release GIL before posting events
    PyGILState_Release( gilState );

    // Post completion event with result (only if not stopped and handler still valid)
    if( !TestDestroy() && !m_stopRequested.load() )
    {
        wxThreadEvent* event = new wxThreadEvent( wxEVT_PYTHON_COMPLETE );
        event->SetString( wxString::FromUTF8( resultStr.c_str() ) );
        event->SetInt( success ? 1 : 0 );
        wxQueueEvent( m_handler, event );
    }

    return nullptr;
}
