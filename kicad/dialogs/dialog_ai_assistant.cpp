#include "dialog_ai_assistant.h"
#include <wx/sizer.h>
#include <wx/stattext.h>

DIALOG_AI_ASSISTANT::DIALOG_AI_ASSISTANT( wxWindow* aParent ) :
        DIALOG_SHIM( aParent, wxID_ANY, "AI Assistant", wxDefaultPosition, wxSize( 500, 400 ),
                     wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER )
{
    wxBoxSizer* mainSizer = new wxBoxSizer( wxVERTICAL );
    SetSizer( mainSizer );

    // Description text
    wxStaticText* label = new wxStaticText( this, wxID_ANY, "Enter your query for the AI Agent:" );
    mainSizer->Add( label, 0, wxALL, 10 );

    // Text input area
    m_promptText = new wxTextCtrl( this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                                   wxTE_MULTILINE | wxTE_PROCESS_ENTER );
    mainSizer->Add( m_promptText, 1, wxEXPAND | wxALL, 10 );

    // Buttons
    wxBoxSizer* btnSizer = new wxBoxSizer( wxHORIZONTAL );
    m_btnRun = new wxButton( this, wxID_OK, "Run" );
    m_btnCancel = new wxButton( this, wxID_CANCEL, "Cancel" );

    btnSizer->Add( m_btnCancel, 0, wxALL, 5 );
    btnSizer->Add( m_btnRun, 0, wxALL, 5 );

    mainSizer->Add( btnSizer, 0, wxALIGN_RIGHT | wxALL, 5 );

    // Bind "Run" button to dummy handler for now
    m_btnRun->Bind( wxEVT_BUTTON, &DIALOG_AI_ASSISTANT::onRunQuery, this );

    Layout();
    Center();
}

DIALOG_AI_ASSISTANT::~DIALOG_AI_ASSISTANT()
{
}

void DIALOG_AI_ASSISTANT::onRunQuery( wxCommandEvent& aEvent )
{
    // Placeholder for future AI logic
    EndModal( wxID_OK );
}
