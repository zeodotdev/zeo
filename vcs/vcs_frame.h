#ifndef VCS_FRAME_H
#define VCS_FRAME_H

#include <kiway_player.h>
#include <widgets/webview_panel.h>
#include <nlohmann/json.hpp>
#include <memory>
#include <string>

#ifdef __APPLE__
#include <CoreServices/CoreServices.h>
#endif

class VCS_IPC_HANDLER;

class VCS_FRAME : public KIWAY_PLAYER
{
public:
    VCS_FRAME( KIWAY* aKiway, wxWindow* aParent );
    ~VCS_FRAME();

    // KIWAY_PLAYER overrides
    bool      OpenProjectFiles( const std::vector<wxString>& aFileSet, int aCtl = 0 ) override;
    void      ShowChangedLanguage() override {}
    void      KiwayMailIn( KIWAY_MAIL_EVENT& aEvent ) override;
    wxWindow* GetToolCanvas() const override { return (wxWindow*) this; }

    // Send a JSON message to the web UI
    void SendToWebView( const wxString& aAction, const nlohmann::json& aData );

    // Notify web UI that the project has changed (git status may have changed)
    void NotifyProjectChanged();

    // File-system watcher: call when project directory changes
    void StartWatching( const wxString& aPath );
    void StopWatching();

    // Event handlers
    void OnExit( wxCommandEvent& aEvent );

    DECLARE_EVENT_TABLE()

private:
    void OnClose( wxCloseEvent& aEvent );

    WEBVIEW_PANEL*                   m_webView;
    std::unique_ptr<VCS_IPC_HANDLER> m_ipcHandler;

#ifdef __APPLE__
    FSEventStreamRef m_fsWatcher  = nullptr;
    wxString         m_watchedPath;
#endif
};

#endif // VCS_FRAME_H
