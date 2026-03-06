#ifndef __WXMAC__

#include <zeo/auth_callback_server.h>

#include <wx/tokenzr.h>

#include <sstream>


wxDEFINE_EVENT( EVT_AUTH_CALLBACK, wxCommandEvent );

AUTH_CALLBACK_SERVER::AUTH_CALLBACK_SERVER( wxEvtHandler* aOwner ) :
        m_owner( aOwner ),
        m_port( 0 ),
        m_done( false )
{
    m_timeout.SetOwner( this );
    Bind( wxEVT_TIMER, &AUTH_CALLBACK_SERVER::OnTimeout, this, m_timeout.GetId() );
}

AUTH_CALLBACK_SERVER::~AUTH_CALLBACK_SERVER()
{
    m_timeout.Stop();
    Unbind( wxEVT_SOCKET, &AUTH_CALLBACK_SERVER::OnSocketEvent, this );
    Unbind( wxEVT_TIMER, &AUTH_CALLBACK_SERVER::OnTimeout, this, m_timeout.GetId() );
    Shutdown();
    m_timeout.SetOwner( nullptr );
}

bool AUTH_CALLBACK_SERVER::Start()
{
    wxIPV4address addr;
    addr.Hostname( "127.0.0.1" );
    addr.Service( 0 );

    std::unique_ptr<wxSocketServer> server =
            std::make_unique<wxSocketServer>( addr, wxSOCKET_REUSEADDR );

    if( !server->IsOk() )
        return false;

    server->SetEventHandler( *this );
    server->SetNotify( wxSOCKET_CONNECTION_FLAG );
    server->Notify( true );

    wxIPV4address local;
    server->GetLocal( local );
    m_port = local.Service();

    Bind( wxEVT_SOCKET, &AUTH_CALLBACK_SERVER::OnSocketEvent, this );

    m_server = std::move( server );
    m_timeout.StartOnce( 120000 );
    return m_port != 0;
}

std::string AUTH_CALLBACK_SERVER::GetCallbackUrl( const std::string& aSource ) const
{
    std::ostringstream url;
    url << "http://127.0.0.1:" << m_port << "/callback";

    if( !aSource.empty() )
        url << "/" << aSource;

    return url.str();
}

void AUTH_CALLBACK_SERVER::OnSocketEvent( wxSocketEvent& aEvent )
{
    if( !m_server || aEvent.GetSocketEvent() != wxSOCKET_CONNECTION )
        return;

    std::unique_ptr<wxSocketBase> client( m_server->Accept( false ) );

    if( !client )
        return;

    HandleClient( client.get() );
}

void AUTH_CALLBACK_SERVER::OnTimeout( wxTimerEvent& aEvent )
{
    wxUnusedVar( aEvent );
    Shutdown();
}

void AUTH_CALLBACK_SERVER::HandleClient( wxSocketBase* aClient )
{
    if( !aClient )
        return;

    aClient->SetTimeout( 5 );
    aClient->SetFlags( wxSOCKET_NONE );

    std::string request;
    request.reserve( 4096 );

    char buffer[512];

    while( aClient->IsConnected() )
    {
        if( !aClient->WaitForRead( 1, 0 ) )
            break;

        aClient->Read( buffer, sizeof( buffer ) );
        size_t count = aClient->LastCount();

        if( count == 0 )
            break;

        request.append( buffer, count );

        if( request.find( "\r\n\r\n" ) != std::string::npos || request.size() > 4096 )
            break;
    }

    SendSuccessResponse( aClient );

    // Extract the request path + query from the HTTP request line
    // Format: "GET /callback/agent?access_token=...&refresh_token=... HTTP/1.1\r\n..."
    wxString requestWx = wxString::FromUTF8( request.data(), request.size() );
    int endOfLine = requestWx.Find( wxS( "\r\n" ) );
    wxString requestLine = endOfLine == wxNOT_FOUND ? requestWx : requestWx.Mid( 0, endOfLine );

    // Extract path+query (second token in "GET /path?query HTTP/1.1")
    wxStringTokenizer tokenizer( requestLine, wxS( " " ) );

    if( !tokenizer.HasMoreTokens() )
        return;

    tokenizer.GetNextToken(); // skip method (GET)

    if( !tokenizer.HasMoreTokens() )
        return;

    wxString pathAndQuery = tokenizer.GetNextToken();

    // Build full callback URL: http://127.0.0.1:PORT/path?query
    std::ostringstream callbackUrl;
    callbackUrl << "http://127.0.0.1:" << m_port << pathAndQuery.ToStdString();

    Finish( wxString::FromUTF8( callbackUrl.str() ) );
}

void AUTH_CALLBACK_SERVER::SendSuccessResponse( wxSocketBase* aClient )
{
    if( !aClient )
        return;

    wxString html;
    html << wxS( "<!DOCTYPE html><html><head>" )
         << wxS( "<meta charset=\"utf-8\">" )
         << wxS( "<title>Sign-in Successful</title>" )
         << wxS( "<style>" )
         << wxS( "body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif; " )
         << wxS( "display: flex; justify-content: center; align-items: center; " )
         << wxS( "min-height: 100vh; margin: 0; background: #f5f5f5; color: #333; }" )
         << wxS( ".card { text-align: center; padding: 2rem; background: white; " )
         << wxS( "border-radius: 8px; box-shadow: 0 2px 8px rgba(0,0,0,0.1); }" )
         << wxS( "h1 { color: #2e7d32; margin-bottom: 0.5rem; }" )
         << wxS( "</style>" )
         << wxS( "</head><body>" )
         << wxS( "<div class=\"card\">" )
         << wxS( "<h1>Sign-in Successful</h1>" )
         << wxS( "<p>You can close this tab and return to the application.</p>" )
         << wxS( "</div>" )
         << wxS( "</body></html>" );

    wxScopedCharBuffer body = html.ToUTF8();

    wxString response;
    response << wxS( "HTTP/1.1 200 OK\r\n" )
             << wxS( "Content-Type: text/html; charset=utf-8\r\n" )
             << wxS( "Access-Control-Allow-Origin: *\r\n" )
             << wxS( "Cache-Control: no-store\r\n" )
             << wxS( "Connection: close\r\n" )
             << wxS( "Content-Length: " ) << body.length() << wxS( "\r\n\r\n" );

    wxScopedCharBuffer header = response.ToUTF8();

    aClient->Write( header.data(), header.length() );
    aClient->Write( body.data(), body.length() );
    aClient->Close();
}

void AUTH_CALLBACK_SERVER::Finish( const wxString& aCallbackUrl )
{
    if( m_done )
        return;

    m_done = true;
    m_timeout.Stop();

    wxCommandEvent evt( EVT_AUTH_CALLBACK );
    evt.SetString( aCallbackUrl );
    wxQueueEvent( m_owner, evt.Clone() );

    Shutdown();
}

void AUTH_CALLBACK_SERVER::Shutdown()
{
    if( m_server )
    {
        m_server->Notify( false );
        m_server.reset();
    }
}

#endif // !__WXMAC__
