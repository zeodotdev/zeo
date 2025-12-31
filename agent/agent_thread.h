#ifndef AGENT_THREAD_H
#define AGENT_THREAD_H

#include <wx/thread.h>
#include <wx/event.h>
#include <string>
#include <nlohmann/json.hpp>
#include "agent_llm_client.h"

// Define custom events for streaming updates and completion
wxDECLARE_EVENT( wxEVT_AGENT_UPDATE, wxCommandEvent );
wxDECLARE_EVENT( wxEVT_AGENT_COMPLETE, wxCommandEvent );

class AGENT_FRAME; // Forward declaration

class AGENT_THREAD : public wxThread
{
public:
    AGENT_THREAD( AGENT_FRAME* aFrame, const nlohmann::json& aMessages, const std::string& aSystem,
                  const std::string& aContext, const std::string& aModelName );
    virtual ~AGENT_THREAD();

    // Thread entry point
    virtual void* Entry() override;

private:
    AGENT_FRAME*     m_frame;
    nlohmann::json   m_messages; // Full history
    std::string      m_system;
    std::string      m_context;
    std::string      m_modelName;
    AGENT_LLM_CLIENT m_client;
};

#endif // AGENT_THREAD_H
