#ifndef PYTHON_EXEC_THREAD_H
#define PYTHON_EXEC_THREAD_H

#include <wx/thread.h>
#include <wx/event.h>
#include <string>
#include <atomic>
#include <Python.h>

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

    /**
     * Get the Python thread identifier (set after GIL acquisition in Entry()).
     * Returns 0 if the thread hasn't started or hasn't acquired the GIL yet.
     */
    unsigned long GetPythonThreadId() const { return m_pythonThreadId.load(); }

    /**
     * Raise an asynchronous exception in the running Python code.
     * Uses PyThreadState_SetAsyncExc to inject KeyboardInterrupt at the next
     * bytecode instruction boundary. Returns true if the exception was set.
     */
    bool InterruptPython();

private:
    wxEvtHandler*              m_handler;
    std::string                m_code;
    std::atomic<bool>          m_stopRequested;
    std::atomic<unsigned long> m_pythonThreadId;
};

#endif // PYTHON_EXEC_THREAD_H
