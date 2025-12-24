#include "agent_thread.h"
#include <wx/log.h>
#include <wx/utils.h> // for wxMilliSleep if needed

// Define the event types
wxDEFINE_EVENT( wxEVT_AGENT_UPDATE, wxCommandEvent );
wxDEFINE_EVENT( wxEVT_AGENT_COMPLETE, wxCommandEvent );

AGENT_THREAD::AGENT_THREAD( AGENT_FRAME* aFrame, const std::string& aPrompt, const std::string& aSystem,
                            const std::string& aContext ) :
        wxThread( wxTHREAD_JOINABLE ),
        m_frame( aFrame ),
        m_prompt( aPrompt ),
        m_system( aSystem ),
        m_context( aContext )
{
}

AGENT_THREAD::~AGENT_THREAD()
{
}

void* AGENT_THREAD::Entry()
{
    if( !m_frame )
    {
        return nullptr;
    }

    // Callback lambda for streaming updates
    auto callback = [this]( const std::string& content )
    {
        // Check for thread cancellation
        if( TestDestroy() )
            return;

        // Post update event to main thread
        wxCommandEvent* event = new wxCommandEvent( wxEVT_AGENT_UPDATE );
        event->SetString( content ); // Implicit conversion from std::string to wxString
        wxQueueEvent( (wxEvtHandler*) m_frame, event );
    };

    // Execute the request
    bool success = m_client.AskStream( m_prompt, m_system, m_context, callback );

    // Post completion event
    if( !TestDestroy() )
    {
        wxCommandEvent* event = new wxCommandEvent( wxEVT_AGENT_COMPLETE );
        event->SetInt( success ? 1 : 0 ); // 1 for success, 0 for failure
        wxQueueEvent( (wxEvtHandler*) m_frame, event );
    }

    return nullptr;
}
