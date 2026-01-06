#include "terminal_panel_agent.h"
#include <wx/settings.h>

AGENT_TERMINAL_PANEL::AGENT_TERMINAL_PANEL( wxWindow* aParent, TERMINAL_MODE aMode ) :
        TERMINAL_PANEL( aParent, aMode )
{
    // Customize Appearance
    // Green text for Agent - User requested removal (keep white)
    // m_outputCtrl->SetForegroundColour( wxColour( 0, 255, 0 ) ); // Bright Green
    // m_outputCtrl->SetDefaultStyle( wxTextAttr( wxColour( 0, 255, 0 ), wxColour( 30, 30, 30 ) ) );

    // Clear and set custom welcome message
    // Note: TERMINAL_PANEL constructor already printed its welcome message.
    // We should clear it or append ours.
    // To replace it cleanly, we can clear the control.
    m_outputCtrl->Clear();

    m_outputCtrl->AppendText( "Agent Terminal\n" );
    m_outputCtrl->AppendText( "Authorized Agent Access Only.\n" );
    m_outputCtrl->AppendText( "Type 'exit' to close session.\n\n" );
    m_outputCtrl->AppendText( GetPrompt() );

    // Update m_lastPromptPos because we cleared and reprinted
    m_lastPromptPos = m_outputCtrl->GetLastPosition();
}

AGENT_TERMINAL_PANEL::~AGENT_TERMINAL_PANEL()
{
}

wxString AGENT_TERMINAL_PANEL::GetTitle() const
{
    return "Agent: " + TERMINAL_PANEL::GetTitle();
}
