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
        // Wrap code with output capture
        // This captures both stdout and stderr to a StringIO buffer
        std::string wrapper = "import sys\n"
                              "from io import StringIO\n"
                              "_term_capture = StringIO()\n"
                              "_term_restore_out = sys.stdout\n"
                              "_term_restore_err = sys.stderr\n"
                              "sys.stdout = _term_capture\n"
                              "sys.stderr = _term_capture\n"
                              "try:\n"
                              "    exec(\"\"\"" + m_code + "\"\"\")\n"
                              "except Exception as e:\n"
                              "    import traceback\n"
                              "    traceback.print_exc()\n"
                              "finally:\n"
                              "    sys.stdout = _term_restore_out\n"
                              "    sys.stderr = _term_restore_err\n"
                              "_term_result = _term_capture.getvalue()\n";

        int retCode = PyRun_SimpleString( wrapper.c_str() );

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
