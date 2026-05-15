#include <dialogs/dialog_settings_diff.h>
#include <wx/sizer.h>
#include <wx/button.h>
#include <wx/stattext.h>

enum
{
    ID_APPROVE = 10001,
    ID_DENY
};

DIALOG_SETTINGS_DIFF::DIALOG_SETTINGS_DIFF( wxWindow* aParent, const std::vector<SETTING_CHANGE>& aChanges ) :
        wxDialog( aParent, wxID_ANY, "Review Agent Settings Changes", wxDefaultPosition, wxSize( 600, 400 ),
                  wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER )
{
    m_approved = false;

    wxBoxSizer* mainSizer = new wxBoxSizer( wxVERTICAL );

    wxStaticText* header = new wxStaticText( this, wxID_ANY, "The AI Agent proposes the following settings changes:" );
    mainSizer->Add( header, 0, wxALL, 10 );

    m_list =
            new wxListCtrl( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_HRULES | wxLC_VRULES );
    m_list->InsertColumn( 0, "Setting", wxLIST_FORMAT_LEFT, 200 );
    m_list->InsertColumn( 1, "Old Value", wxLIST_FORMAT_LEFT, 150 );
    m_list->InsertColumn( 2, "New Value", wxLIST_FORMAT_LEFT, 150 );

    for( size_t i = 0; i < aChanges.size(); ++i )
    {
        const auto& change = aChanges[i];
        long        idx = m_list->InsertItem( i, change.m_settingKey );
        m_list->SetItem( idx, 1, change.m_oldValue );
        m_list->SetItem( idx, 2, change.m_newValue );
    }

    mainSizer->Add( m_list, 1, wxEXPAND | wxALL, 10 );

    wxBoxSizer* btnSizer = new wxBoxSizer( wxHORIZONTAL );

    wxButton* btnApprove = new wxButton( this, ID_APPROVE, "Approve Changes" );
    wxButton* btnDeny = new wxButton( this, ID_DENY, "Deny" );

    btnSizer->Add( btnDeny, 0, wxALL, 5 );
    btnSizer->Add( btnApprove, 0, wxALL, 5 );

    mainSizer->Add( btnSizer, 0, wxALIGN_RIGHT | wxALL, 10 );

    SetSizer( mainSizer );
    Layout();
    Centre();

    Bind( wxEVT_BUTTON, &DIALOG_SETTINGS_DIFF::OnApprove, this, ID_APPROVE );
    Bind( wxEVT_BUTTON, &DIALOG_SETTINGS_DIFF::OnDeny, this, ID_DENY );
}

DIALOG_SETTINGS_DIFF::~DIALOG_SETTINGS_DIFF()
{
}

void DIALOG_SETTINGS_DIFF::OnApprove( wxCommandEvent& aEvent )
{
    m_approved = true;
    EndModal( wxID_OK );
}

void DIALOG_SETTINGS_DIFF::OnDeny( wxCommandEvent& aEvent )
{
    m_approved = false;
    EndModal( wxID_CANCEL );
}
