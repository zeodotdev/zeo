/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#include "session_manager.h"
#include "kicad_manager_frame.h"
#include <widgets/kistatusbar.h>
#include <kiway.h>
#include <mail_type.h>
#include <wx/button.h>
#include <wx/file.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/msgdlg.h>
#include <wx/stdpaths.h>
#include <wx/textfile.h>
#include <wx/utils.h>
#include <nlohmann/json.hpp>
#include <wx/menu.h>
#include <wx/utils.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <fstream>
#include <nlohmann/json.hpp>
#include <wx/stattext.h>
#include <wx/image.h>
#include <wx/mstream.h>
#include <wx/statbmp.h>
#include <kicad_curl/kicad_curl_easy.h>
#include <wx/bmpbndl.h>

using json = nlohmann::json;

#include <kiplatform/ui.h>
#include <widgets/ui_common.h>
#include <wx/frame.h>
#include <wx/sizer.h>
#include <wx/hyperlink.h>
#include <wx/dcclient.h>
#include <wx/settings.h>


#include <wx/time.h>

static long long g_session_last_closed = 0;

class SESSION_DROPDOWN : public wxFrame
{
public:
    SESSION_DROPDOWN( wxWindow* aParent, SESSION_MANAGER* aManager, const wxPoint& aPos ) :
            wxFrame( aParent, wxID_ANY, _( "Session" ), aPos, wxDefaultSize,
                     wxFRAME_NO_TASKBAR | wxBORDER_SIMPLE ),
            m_manager( aManager )
    {
        SetSizeHints( wxDefaultSize, wxDefaultSize );

        wxBoxSizer* mainSizer = new wxBoxSizer( wxVERTICAL );

        // Match notification list structure: frame border + inner panel border with colors
        wxColour fg, bg;
        KIPLATFORM::UI::GetInfoBarColours( fg, bg );

        // Inner panel with wxBORDER_SIMPLE to match notification list's scrolled window
        wxPanel* panel = new wxPanel( this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                      wxBORDER_SIMPLE );
        panel->SetBackgroundColour( bg );
        panel->SetForegroundColour( fg );

        wxBoxSizer* contentSizer = new wxBoxSizer( wxVERTICAL );

        // Signed in as...
        wxStaticText* signedInText = new wxStaticText( panel, wxID_ANY, _( "Signed in as:" ) );
        signedInText->SetFont( KIUI::GetControlFont( panel ).Bold() );
        contentSizer->Add( signedInText, 0, wxLEFT | wxRIGHT | wxTOP, 10 );

        wxString userEmail;
        if( aManager->GetAuth() )
            userEmail = aManager->GetAuth()->GetUserEmail();

        wxStaticText* emailText = new wxStaticText( panel, wxID_ANY, userEmail );
        contentSizer->Add( emailText, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10 );

        // Dashboard and Sign out links
        wxBoxSizer* linksSizer = new wxBoxSizer( wxHORIZONTAL );

        wxHyperlinkCtrl* dashboardLink = new wxHyperlinkCtrl( panel, wxID_ANY, _( "Dashboard" ), "" );
        dashboardLink->SetNormalColour( fg );
        dashboardLink->SetVisitedColour( fg );
        dashboardLink->SetHoverColour( fg );
        dashboardLink->Bind( wxEVT_HYPERLINK, &SESSION_DROPDOWN::onDashboard, this );
        linksSizer->Add( dashboardLink, 0, wxRIGHT, 10 );

        wxHyperlinkCtrl* signOutLink = new wxHyperlinkCtrl( panel, wxID_ANY, _( "Sign out" ), "" );
        signOutLink->SetNormalColour( fg );
        signOutLink->SetVisitedColour( fg );
        signOutLink->SetHoverColour( fg );
        signOutLink->Bind( wxEVT_HYPERLINK, &SESSION_DROPDOWN::onSignOut, this );
        linksSizer->Add( signOutLink, 0 );

        contentSizer->Add( linksSizer, 0, wxALIGN_RIGHT | wxRIGHT | wxBOTTOM, 10 );

        panel->SetSizer( contentSizer );
        contentSizer->Fit( panel );

        mainSizer->Add( panel, 1, wxEXPAND | wxALL, 0 );

        Bind( wxEVT_KILL_FOCUS, &SESSION_DROPDOWN::onFocusLoss, this );
        panel->Bind( wxEVT_KILL_FOCUS, &SESSION_DROPDOWN::onFocusLoss, this );

        SetSizer( mainSizer );
        Layout();
        Fit();
    }

private:
    void onDashboard( wxHyperlinkEvent& aEvent )
    {
        wxLaunchDefaultBrowser( "https://zeo.dev/dashboard" );
        Close( true );
    }

    void onSignOut( wxHyperlinkEvent& aEvent )
    {
        m_manager->SignOut();
        Close( true );
    }

    void onFocusLoss( wxFocusEvent& aEvent )
    {
        if( !IsDescendant( aEvent.GetWindow() ) )
        {
            Close( true );
            g_session_last_closed = wxGetLocalTimeMillis().GetValue();
        }
        aEvent.Skip();
    }

    SESSION_MANAGER* m_manager;
};

SESSION_MANAGER::SESSION_MANAGER( KICAD_MANAGER_FRAME* aFrame ) :
    m_frame( aFrame )
{
    m_auth = std::make_unique<AGENT_AUTH>();
}

SESSION_MANAGER::~SESSION_MANAGER()
{
}

void SESSION_MANAGER::Initialize()
{
    // Load Supabase configuration
    std::string supabaseUrl, supabaseKey;
    
    // Try loading from supabase_config.json in agent source directory
    // We assume the source structure is maintained
    wxFileName configPath( __FILE__ );
    configPath.RemoveLastDir(); // kicad
    configPath.AppendDir( "agent" );
    configPath.SetFullName( "supabase_config.json" );
    
    if( wxFileExists( configPath.GetFullPath() ) )
    {
        std::ifstream configFile( configPath.GetFullPath().ToStdString() );
        if( configFile.is_open() )
        {
            try
            {
                json config = json::parse( configFile );
                supabaseUrl = config.value( "project_url", "" );
                supabaseKey = config.value( "publishable_key", "" );
            }
            catch( ... ) {}
            configFile.close();
        }
    }
    
    if( !supabaseUrl.empty() && !supabaseKey.empty() )
    {
        m_auth->Configure( supabaseUrl, supabaseKey );
    }

    UpdateUI();
}

void SESSION_MANAGER::OnSessionButtonClick( wxMouseEvent& aEvent )
{
    if( !m_auth || !m_auth->IsAuthenticated() )
    {
        StartOAuthFlow();
        return;
    }

    // Debounce re-opening immediately after close
    if( wxGetLocalTimeMillis().GetValue() - g_session_last_closed < 300 )
    {
        g_session_last_closed = 0;
        return;
    }

    // Find target context for positioning
    KISTATUSBAR* statusBar = dynamic_cast<KISTATUSBAR*>( m_frame->GetStatusBar() );
    wxWindow* targetCtx = nullptr;

    if( statusBar )
    {
        if( statusBar->GetProfileBitmap() && statusBar->GetProfileBitmap()->IsShown() )
            targetCtx = statusBar->GetProfileBitmap();
        else if( statusBar->GetLabelButton() && statusBar->GetLabelButton()->IsShown() )
            targetCtx = statusBar->GetLabelButton();
        
        if( !targetCtx )
        {
            targetCtx = statusBar->GetLabelButton();
            if( !targetCtx )
                return;
        }
    }

    if( targetCtx )
    {
        // Anchor point: Top-Right of the button/avatar
        wxPoint anchor = targetCtx->GetScreenPosition();
        anchor.x += targetCtx->GetSize().GetWidth();

        // Pass status bar as parent to match Notification List behavior
        SESSION_DROPDOWN* dropdown = new SESSION_DROPDOWN( statusBar ? (wxWindow*)statusBar : (wxWindow*)m_frame, this, anchor );

        // Correct the position: Align bottom-right of dropdown with top-right of anchor (Drop Up & Align Right)
        dropdown->Fit();
        wxSize sz = dropdown->GetSize();
        dropdown->SetPosition( anchor - wxPoint( sz.x, sz.y + 3 ) );

        dropdown->Show();
        KIPLATFORM::UI::ForceFocus( dropdown );
    }
}

void SESSION_MANAGER::StartOAuthFlow()
{
    if( m_auth )
        m_auth->StartOAuthFlow();
}

bool SESSION_MANAGER::HandleDeepLink( const wxString& aUrl )
{
    // VCS-specific GitHub OAuth callback — does NOT touch the main user session
    if( aUrl.Contains( "vcs-callback" ) )
    {
        HandleVcsCallback( aUrl );
        return true;
    }

    std::string errorMsg;
    if( m_auth && m_auth->HandleOAuthCallback( aUrl.ToStdString(), errorMsg ) )
    {
        UpdateUI();

        // Notify agent frame that auth state changed so it can reload from keychain
        // Include source in payload so agent frame knows to bring itself to front
        std::string payload = aUrl.Contains( "callback/agent" ) ? "source=agent" : "";
        m_frame->Kiway().ExpressMail( FRAME_AGENT, MAIL_AUTH_STATE_CHANGED, payload, m_frame );

        return true;
    }
    else
    {
        wxMessageBox( _("Sign in failed: ") + errorMsg, _("Error"), wxOK | wxICON_ERROR );
        return false;
    }
}


// Simple percent-decode for URL query parameter values
static wxString VcsUrlDecode( const wxString& aEncoded )
{
    std::string encoded = aEncoded.ToStdString();
    std::string result;
    result.reserve( encoded.size() );
    for( size_t i = 0; i < encoded.size(); ++i )
    {
        if( encoded[i] == '%' && i + 2 < encoded.size() )
        {
            char hex[3] = { encoded[i + 1], encoded[i + 2], '\0' };
            result += static_cast<char>( std::strtol( hex, nullptr, 16 ) );
            i += 2;
        }
        else if( encoded[i] == '+' )
        {
            result += ' ';
        }
        else
        {
            result += encoded[i];
        }
    }
    return wxString::FromUTF8( result );
}


void SESSION_MANAGER::HandleVcsCallback( const wxString& aUrl )
{
    using json = nlohmann::json;

    // Parse query params from the deep link
    // Format: zeo://vcs-callback?provider_token=xxx&github_username=xxx
    wxString query = aUrl.AfterFirst( '?' );
    std::map<wxString, wxString> params;
    for( wxString pair : wxSplit( query, '&' ) )
    {
        wxString key   = pair.BeforeFirst( '=' );
        wxString value = VcsUrlDecode( pair.AfterFirst( '=' ) );
        params[key] = value;
    }

    wxString token    = params["provider_token"];
    // New format sends "username"; legacy GitHub flow sent "github_username"
    wxString username = !params["username"].IsEmpty() ? params["username"] : params["github_username"];
    wxString provider = !params["provider"].IsEmpty() ? params["provider"] : wxString( "github" );
    std::string host  = ( provider == "gitlab" ) ? "gitlab.com" : "github.com";

    if( token.IsEmpty() || username.IsEmpty() )
    {
        wxLogError( "SESSION_MANAGER: VCS callback missing token or username" );
        return;
    }

    // ── Write to ~/.git-credentials ───────────────────────────────────────
    wxString newEntry = wxString::Format( "https://%s:%s@%s", username, token, host );
    wxString credsPath = wxGetHomeDir() + wxFILE_SEP_PATH + ".git-credentials";

    wxArrayString lines;
    if( wxFileExists( credsPath ) )
    {
        wxTextFile tf;
        if( tf.Open( credsPath ) )
        {
            for( size_t i = 0; i < tf.GetLineCount(); i++ )
            {
                wxString line = tf.GetLine( i );
                if( !line.IsEmpty()
                    && !( line.Contains( wxString( host ) ) && line.Contains( username ) ) )
                    lines.Add( line );
            }
            tf.Close();
        }
    }
    lines.Add( newEntry );

    {
        wxFile out( credsPath, wxFile::write );
        if( out.IsOpened() )
        {
            for( const wxString& line : lines )
                out.Write( line + "\n" );
            out.Close();
        }
    }
    wxFileName fn( credsPath );
    fn.SetPermissions( wxS_IRUSR | wxS_IWUSR );

    // ── Ensure credential.helper = store in git config ────────────────────
    // (libgit2 call omitted here — StoreCredential in git_vcs_bridge handles
    //  this when the VCS frame first opens a repo. Credential file is enough.)

    // ── Write UI credentials JSON ─────────────────────────────────────────
    wxString dataDir = wxStandardPaths::Get().GetUserDataDir();
    if( !wxDirExists( dataDir ) ) wxMkdir( dataDir );
    wxString jsonPath = dataDir + wxFILE_SEP_PATH + "vcs_credentials.json";

    json ui = {
        { "connected", true },
        { "host",      host },
        { "username",  username.ToStdString() }
    };
    {
        wxFile jf( jsonPath, wxFile::write );
        if( jf.IsOpened() )
        {
            jf.Write( wxString::FromUTF8( ui.dump() ) );
            jf.Close();
        }
    }
    wxFileName jfn( jsonPath );
    jfn.SetPermissions( wxS_IRUSR | wxS_IWUSR );

    // ── Notify VCS frame and bring it to front ────────────────────────────
    std::string payload = ui.dump();
    m_frame->Kiway().ExpressMail( FRAME_VCS, MAIL_VCS_AUTH_COMPLETE, payload, m_frame );

    // Raise the VCS window so the user returns to it after the browser OAuth flow
    KIWAY_PLAYER* vcsPlayer = m_frame->Kiway().Player( FRAME_VCS, false, m_frame );
    if( vcsPlayer )
    {
        vcsPlayer->Raise();
        vcsPlayer->SetFocus();
    }

    wxLogMessage( "SESSION_MANAGER: %s VCS auth complete for @%s", provider, username );
}

void SESSION_MANAGER::SignOut()
{
    if( m_auth )
    {
        m_auth->SignOut();
        UpdateUI();

        // Notify agent frame that auth state changed
        std::string payload;
        m_frame->Kiway().ExpressMail( FRAME_AGENT, MAIL_AUTH_STATE_CHANGED, payload, m_frame );
    }
}

void SESSION_MANAGER::UpdateUI()
{
    KISTATUSBAR* statusBar = dynamic_cast<KISTATUSBAR*>( m_frame->GetStatusBar() );
    if( !statusBar )
        return;

    if( m_auth && m_auth->IsAuthenticated() )
    {
        bool imageLoaded = false;
        std::string avatarUrl = m_auth->GetAvatarUrl();
        
        if( !avatarUrl.empty() )
        {
            KICAD_CURL_EASY curl;
            curl.SetURL( avatarUrl );
            
            try 
            {
                curl.Perform();
                if( curl.GetResponseStatusCode() == 200 )
                {
                    std::string buffer = curl.GetBuffer();
                    wxMemoryInputStream stream( buffer.data(), buffer.size() );
                    wxImage image( stream );
                    
                    if( image.IsOk() )
                    {
                        // Apply circular mask + border at full source resolution,
                        // then downsample to 1x and 2x for a wxBitmapBundle.
                        // wx picks the right variant per DPI automatically.

                        if( !image.HasAlpha() )
                            image.InitAlpha();

                        int w = image.GetWidth();
                        int h = image.GetHeight();
                        float cx = w / 2.0f;
                        float cy = h / 2.0f;
                        float r2 = ( std::min( w, h ) / 2.0f ) * ( std::min( w, h ) / 2.0f );

                        unsigned char* alpha = image.GetAlpha();

                        for( int py = 0; py < h; ++py )
                        {
                            for( int px = 0; px < w; ++px )
                            {
                                float dx = px - cx + 0.5f;
                                float dy = py - cy + 0.5f;
                                alpha[py * w + px] = ( dx * dx + dy * dy <= r2 ) ? 255 : 0;
                            }
                        }

                        // Downsample to 1x (18px) and 2x (36px) for the bundle
                        wxImage img1x = image.Scale( 18, 18, wxIMAGE_QUALITY_HIGH );
                        wxImage img2x = image.Scale( 36, 36, wxIMAGE_QUALITY_HIGH );

                        wxVector<wxBitmap> bitmaps;
                        bitmaps.push_back( wxBitmap( img1x ) );
                        bitmaps.push_back( wxBitmap( img2x ) );
                        wxBitmapBundle bundle = wxBitmapBundle::FromBitmaps( bitmaps );

                        statusBar->SetProfileBitmap( bundle );
                        statusBar->GetLabelButton()->Hide();
                        statusBar->GetProfileBitmap()->Show();
                        statusBar->Layout(); // Ensure visibility update
                        imageLoaded = true;
                    }
                }
            }
            catch( ... ) {}
        }
        
        if( !imageLoaded )
        {
            wxString label = m_auth->GetFirstName();
            if( label.IsEmpty() )
                label = m_auth->GetUserEmail();
            
            statusBar->SetLabelButtonText( label );
            statusBar->GetLabelButton()->Show();
            if( statusBar->GetProfileBitmap() )
                statusBar->GetProfileBitmap()->Hide();
        }
    }
    else
    {
        statusBar->SetLabelButtonText( _( "Sign In" ) );
        statusBar->GetLabelButton()->Show();
        if( statusBar->GetProfileBitmap() )
            statusBar->GetProfileBitmap()->Hide();
        statusBar->Layout();
    }
}
