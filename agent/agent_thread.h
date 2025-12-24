#ifndef AGENT_THREAD_H
#define AGENT_THREAD_H

#include <wx/thread.h>
#include <wx/event.h>
#include <string>
#include "agent_openai_client_temp.h"

// Define custom events for streaming updates and completion
wxDECLARE_EVENT( wxEVT_AGENT_UPDATE, wxCommandEvent );
wxDECLARE_EVENT( wxEVT_AGENT_COMPLETE, wxCommandEvent );

class AGENT_FRAME; // Forward declaration

class AGENT_THREAD : public wxThread
{
public:
    AGENT_THREAD( AGENT_FRAME* aFrame, const std::string& aPrompt, const std::string& aSystem,
                  const std::string& aContext );
    virtual ~AGENT_THREAD();

    // Thread entry point
    virtual void* Entry() override;

private:
    AGENT_FRAME*       m_frame;
    std::string        m_prompt;
    std::string        m_system;
    std::string        m_context;
    OPENAI_CLIENT_TEMP m_client;
};

#endif // AGENT_THREAD_H
