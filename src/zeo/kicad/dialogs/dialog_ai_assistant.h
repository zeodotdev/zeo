#ifndef DIALOG_AI_ASSISTANT_H
#define DIALOG_AI_ASSISTANT_H

#include <dialog_shim.h>
#include <wx/textctrl.h>
#include <wx/button.h>

class DIALOG_AI_ASSISTANT : public DIALOG_SHIM
{
public:
    DIALOG_AI_ASSISTANT( wxWindow* aParent );
    ~DIALOG_AI_ASSISTANT();

private:
    void onRunQuery( wxCommandEvent& aEvent );

    wxTextCtrl* m_promptText;
    wxButton*   m_btnRun;
    wxButton*   m_btnCancel;
};

#endif // DIALOG_AI_ASSISTANT_H
