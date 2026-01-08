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

#ifndef PYTHON_EXEC_THREAD_H
#define PYTHON_EXEC_THREAD_H

#include <wx/thread.h>
#include <wx/event.h>
#include <string>
#include <atomic>

// Custom events for Python execution
wxDECLARE_EVENT( wxEVT_PYTHON_OUTPUT, wxThreadEvent );
wxDECLARE_EVENT( wxEVT_PYTHON_COMPLETE, wxThreadEvent );

/**
 * Background thread for executing Python code.
 *
 * This thread allows Python code to run without blocking the main wx event loop,
 * which is critical for IPC shell operations that call back into the KiCad API.
 * The API server processes requests via wx events, so the event loop must remain
 * responsive during Python execution.
 */
class PYTHON_EXEC_THREAD : public wxThread
{
public:
    /**
     * Create a new Python execution thread.
     *
     * @param aHandler Event handler to receive output and completion events
     * @param aCode Python code to execute
     */
    PYTHON_EXEC_THREAD( wxEvtHandler* aHandler, const std::string& aCode );

    virtual ~PYTHON_EXEC_THREAD();

    /**
     * Thread entry point - executes the Python code.
     */
    virtual void* Entry() override;

    /**
     * Request the thread to stop execution.
     */
    void RequestStop() { m_stopRequested.store( true ); }

private:
    wxEvtHandler*     m_handler;
    std::string       m_code;
    std::atomic<bool> m_stopRequested;
};

#endif // PYTHON_EXEC_THREAD_H
