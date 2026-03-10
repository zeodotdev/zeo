#include "vcs_frame.h"
#include "vcs_ipc_handler.h"
#include "vcs_html_template.h"

#ifdef __APPLE__
#include <agent/platform/macos_webview_bg.h>
#endif

#include <kiway.h>
#include <kiway_mail.h>
#include <mail_type.h>
#include <frame_type.h>
#include <pgm_base.h>
#include <wx/app.h>
#include <wx/sizer.h>
#include <wx/log.h>
#include <wx/msgdlg.h>

using json = nlohmann::json;


#ifdef __APPLE__
// ── FSEvents file-system watcher ─────────────────────────────────────────────

/**
 * Called by macOS FSEvents when files change under the watched directory.
 * Filters out git internals and backup files, then pushes a status_changed
 * notification to the web UI (debounced by the stream's 1.5 s latency).
 */
static void FsEventsCallback( ConstFSEventStreamRef  /*streamRef*/,
                               void*                  aClientInfo,
                               size_t                 aNumEvents,
                               void*                  aEventPaths,
                               const FSEventStreamEventFlags* /*eventFlags*/,
                               const FSEventStreamEventId*    /*eventIds*/ )
{
    VCS_FRAME* frame = static_cast<VCS_FRAME*>( aClientInfo );
    auto**     paths = reinterpret_cast<char**>( aEventPaths );

    json changedPaths = json::array();

    for( size_t i = 0; i < aNumEvents; ++i )
    {
        wxString p = wxString::FromUTF8( paths[i] );

        // Only care about files with visual diff support
        if( !p.EndsWith( wxS( ".kicad_sch" ) ) && !p.EndsWith( wxS( ".kicad_pcb" ) ) )
            continue;

        changedPaths.push_back( paths[i] );
    }

    if( changedPaths.empty() )
        return;

    wxTheApp->CallAfter( [frame, changedPaths]()
    {
        frame->SendToWebView( wxS( "status_changed" ), json{ { "paths", changedPaths } } );
    } );
}
#endif // __APPLE__

// Scale for layout (matches agent_frame pattern)
static EDA_IU_SCALE s_iuScale( 25400 );

BEGIN_EVENT_TABLE( VCS_FRAME, KIWAY_PLAYER )
    EVT_MENU( wxID_EXIT, VCS_FRAME::OnExit )
    EVT_CLOSE( VCS_FRAME::OnClose )
END_EVENT_TABLE()

VCS_FRAME::VCS_FRAME( KIWAY* aKiway, wxWindow* aParent ) :
        KIWAY_PLAYER( aKiway, aParent, FRAME_VCS, "Version Control", wxDefaultPosition,
                      wxSize( 1200, 800 ), wxDEFAULT_FRAME_STYLE, "vcs_frame_name", s_iuScale )
{
    SetTitle( _( "Version Control" ) );

    wxBoxSizer* mainSizer = new wxBoxSizer( wxVERTICAL );

    // Create the webview panel
    m_webView = new WEBVIEW_PANEL( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0 );
    m_webView->BindLoadedEvent();

    // Create the IPC handler
    m_ipcHandler = std::make_unique<VCS_IPC_HANDLER>( this, m_webView );

    // Route all JS→C++ messages through the IPC handler
    m_webView->AddMessageHandler( wxS( "vcs" ),
        [this]( const wxString& msg ) { m_ipcHandler->OnMessage( msg ); } );

    // Load the embedded HTML UI
    wxString htmlContent = GetVcsHtmlTemplate();

#ifdef __APPLE__
    // Apply light theme if system is in light mode
    if( !IsSystemDarkMode() )
    {
        htmlContent.Replace( wxS( "<html lang=\"en\">" ),
                             wxS( "<html lang=\"en\" class=\"light\">" ) );
    }
#endif

    m_webView->SetPage( htmlContent );
    mainSizer->Add( m_webView, 1, wxEXPAND );

    SetSizer( mainSizer );
    Layout();

    wxLogDebug( "VCS_FRAME: Created" );
}


VCS_FRAME::~VCS_FRAME()
{
    StopWatching();
    wxLogDebug( "VCS_FRAME: Destroyed" );
}


bool VCS_FRAME::OpenProjectFiles( const std::vector<wxString>& aFileSet, int aCtl )
{
    return true;
}


void VCS_FRAME::KiwayMailIn( KIWAY_MAIL_EVENT& aEvent )
{
    if( aEvent.GetId() == MAIL_VCS_AUTH_COMPLETE )
    {
        // GitHub OAuth completed — notify the webview
        try
        {
            json payload  = json::parse( aEvent.GetPayload() );
            std::string username = payload.value( "username", "" );
            std::string host     = payload.value( "host",     "github.com" );
            // Route to the correct frontend handler based on provider host
            std::string action   = ( host == "gitlab.com" ) ? "gitlab_auth_complete"
                                                             : "github_auth_complete";
            SendToWebView( wxString::FromUTF8( action ),
                           json{ { "username", username } } );
        }
        catch( ... )
        {
            SendToWebView( "github_auth_complete", json::object() );
        }
        return;
    }

    if( aEvent.GetId() == MAIL_VCS_REFRESH )
    {
        // Agent modified project files — auto-init git if needed and refresh
        if( m_ipcHandler )
            m_ipcHandler->AutoInitIfNeeded();
        return;
    }

    // Refresh git status when notified of project changes
    NotifyProjectChanged();
}


void VCS_FRAME::SendToWebView( const wxString& aAction, const json& aData )
{
    if( !m_webView )
        return;

    json msg = {
        { "action", aAction.ToStdString() },
        { "data",   aData }
    };

    wxString script = wxString::Format(
            wxS( "if(typeof VcsApp !== 'undefined') VcsApp.onMessage(%s);" ),
            wxString::FromUTF8( msg.dump() ) );

    m_webView->RunScriptAsync( script );
}


void VCS_FRAME::NotifyProjectChanged()
{
    // Tell the web UI to refresh its git status
    SendToWebView( "project_changed", json::object() );
}


void VCS_FRAME::StartWatching( const wxString& aPath )
{
#ifdef __APPLE__
    if( aPath == m_watchedPath && m_fsWatcher )
        return; // Already watching this directory

    StopWatching();

    if( aPath.empty() )
        return;

    m_watchedPath = aPath;

    CFStringRef cfPath = CFStringCreateWithCString(
            kCFAllocatorDefault, aPath.ToUTF8().data(), kCFStringEncodingUTF8 );
    CFArrayRef cfPaths = CFArrayCreate(
            kCFAllocatorDefault, reinterpret_cast<const void**>( &cfPath ),
            1, &kCFTypeArrayCallBacks );
    CFRelease( cfPath );

    FSEventStreamContext ctx = { 0, this, nullptr, nullptr, nullptr };

    m_fsWatcher = FSEventStreamCreate(
            kCFAllocatorDefault,
            &FsEventsCallback,
            &ctx,
            cfPaths,
            kFSEventStreamEventIdSinceNow,
            0.1,
            kFSEventStreamCreateFlagFileEvents );

    CFRelease( cfPaths );

    if( m_fsWatcher )
    {
        FSEventStreamScheduleWithRunLoop(
                m_fsWatcher, CFRunLoopGetMain(), kCFRunLoopDefaultMode );
        FSEventStreamStart( m_fsWatcher );
        wxLogDebug( "VCS_FRAME: FSEvents watching %s", aPath );
    }
#endif // __APPLE__
}


void VCS_FRAME::StopWatching()
{
#ifdef __APPLE__
    if( !m_fsWatcher )
        return;

    FSEventStreamStop( m_fsWatcher );
    FSEventStreamUnscheduleFromRunLoop(
            m_fsWatcher, CFRunLoopGetMain(), kCFRunLoopDefaultMode );
    FSEventStreamInvalidate( m_fsWatcher );
    FSEventStreamRelease( m_fsWatcher );
    m_fsWatcher  = nullptr;
    m_watchedPath.clear();
    wxLogDebug( "VCS_FRAME: FSEvents stopped" );
#endif // __APPLE__
}


void VCS_FRAME::OnExit( wxCommandEvent& aEvent )
{
    Close( true );
}


void VCS_FRAME::OnClose( wxCloseEvent& aEvent )
{
    // Allow the frame to close - it will be re-created next time via KIWAY::Player()
    aEvent.Skip();
}
