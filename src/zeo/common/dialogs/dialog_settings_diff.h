#ifndef DIALOG_SETTINGS_DIFF_H
#define DIALOG_SETTINGS_DIFF_H

#include <wx/dialog.h>
#include <wx/listctrl.h>
#include <vector>

struct SETTING_CHANGE
{
    wxString m_settingKey;
    wxString m_oldValue;
    wxString m_newValue;
};

class DIALOG_SETTINGS_DIFF : public wxDialog
{
public:
    DIALOG_SETTINGS_DIFF( wxWindow* aParent, const std::vector<SETTING_CHANGE>& aChanges );
    ~DIALOG_SETTINGS_DIFF();

    bool IsApproved() const { return m_approved; }

private:
    void OnApprove( wxCommandEvent& aEvent );
    void OnDeny( wxCommandEvent& aEvent );

    bool        m_approved;
    wxListCtrl* m_list;
};

#endif // DIALOG_SETTINGS_DIFF_H
