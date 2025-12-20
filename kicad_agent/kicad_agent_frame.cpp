#include "kicad_agent_frame.h"
#include <dialogs/dialog_text_entry.h>
#include <wx/button.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/msgdlg.h>
#include <pgm_base.h>
#include <base_units.h>
#include <settings/settings_manager.h>
#include <id.h>

BEGIN_EVENT_TABLE( KICAD_AGENT_FRAME, KIWAY_PLAYER )
EVT_CLOSE( KICAD_AGENT_FRAME::OnCloseWindow )
EVT_BUTTON( wxID_OK, KICAD_AGENT_FRAME::onRunQuery )
EVT_BUTTON( wxID_CANCEL, KICAD_AGENT_FRAME::onCancel )
EVT_MENU( wxID_EXIT, KICAD_AGENT_FRAME::OnExit )
END_EVENT_TABLE()

KICAD_AGENT_FRAME::KICAD_AGENT_FRAME( KIWAY* aKiway, wxWindow* aParent ) :
        KIWAY_PLAYER( aKiway, aParent, FRAME_AGENT, wxT( "KiCad Agent" ), wxDefaultPosition, wxDefaultSize,
                      wxDEFAULT_FRAME_STYLE | wxWANTS_CHARS, wxT( "KicadAgentFrame" ), EDA_IU_SCALE( 1000000.0 ) )
{
    wxBoxSizer* mainSizer = new wxBoxSizer( wxVERTICAL );

    wxStaticText* label = new wxStaticText( this, wxID_ANY, wxT( "Ask the AI Assistant:" ) );
    mainSizer->Add( label, 0, wxALL, 5 );

    m_promptText = new wxTextCtrl( this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize( 400, 200 ),
                                   wxTE_MULTILINE | wxTE_PROCESS_ENTER );
    mainSizer->Add( m_promptText, 1, wxEXPAND | wxALL, 5 );

    wxBoxSizer* btnSizer = new wxBoxSizer( wxHORIZONTAL );

    m_btnRun = new wxButton( this, wxID_OK, wxT( "Run" ) );
    m_btnCancel = new wxButton( this, wxID_CANCEL, wxT( "Cancel" ) );

    btnSizer->Add( m_btnCancel, 0, wxALL, 5 );
    btnSizer->Add( m_btnRun, 0, wxALL, 5 );

    mainSizer->Add( btnSizer, 0, wxALIGN_RIGHT | wxALL, 5 );

    SetSizer( mainSizer );
    Layout();
    SetSize( 500, 400 ); // Default size
}

KICAD_AGENT_FRAME::~KICAD_AGENT_FRAME()
{
}

void KICAD_AGENT_FRAME::onRunQuery( wxCommandEvent& aEvent )
{
    // Placeholder logic for now
    wxMessageBox( wxT( "Query: " ) + m_promptText->GetValue(), wxT( "AI Agent" ) );
}

void KICAD_AGENT_FRAME::onCancel( wxCommandEvent& aEvent )
{
    Close( true );
}

void KICAD_AGENT_FRAME::OnExit( wxCommandEvent& aEvent )
{
    Close( true );
}

void KICAD_AGENT_FRAME::LoadSettings( APP_SETTINGS_BASE* aCfg )
{
    KIWAY_PLAYER::LoadSettings( aCfg );
    // Load specific settings here if/when needed
}

void KICAD_AGENT_FRAME::SaveSettings( APP_SETTINGS_BASE* aCfg )
{
    KIWAY_PLAYER::SaveSettings( aCfg );
    // Save specific settings here if/when needed
}

void KICAD_AGENT_FRAME::CommonSettingsChanged( int aFlags )
{
    KIWAY_PLAYER::CommonSettingsChanged( aFlags );
    Layout();
}

void KICAD_AGENT_FRAME::ProjectFileChanged()
{
    // Handle project file changes if necessary
}

void KICAD_AGENT_FRAME::ShowChangedLanguage()
{
    KIWAY_PLAYER::ShowChangedLanguage();
    // Update localized strings if any
}

bool KICAD_AGENT_FRAME::OpenProjectFiles( const std::vector<wxString>& aFileSet, int aCtl )
{
    // Agent probably doesn't open files in the traditional sense, but we can implement this
    // if we want it to parse project context when opened with a file argument.
    return true;
}

void KICAD_AGENT_FRAME::doReCreateMenuBar()
{
    wxMenuBar* menuBar = new wxMenuBar();

    wxMenu* fileMenu = new wxMenu;
    fileMenu->Append( wxID_EXIT, wxT( "E&xit\tCtrl+Q" ) );

    menuBar->Append( fileMenu, wxT( "&File" ) );
    SetMenuBar( menuBar );
}
