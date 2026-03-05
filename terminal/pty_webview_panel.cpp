#include "pty_webview_panel.h"
#include "pty_handler.h"

#include <widgets/webview_panel.h>
#include <wx/sizer.h>
#include <wx/base64.h>
#include <wx/log.h>

#include <nlohmann/json.hpp>

#ifdef __APPLE__
#include <agent/platform/macos_webview_bg.h>
#endif

// Define the custom event for title changes
wxDEFINE_EVENT( wxEVT_TERMINAL_TITLE_CHANGED, wxCommandEvent );


PTY_WEBVIEW_PANEL::PTY_WEBVIEW_PANEL( wxWindow* aParent ) :
        wxPanel( aParent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxNO_BORDER ),
        m_cols( 80 ),
        m_rows( 24 ),
        m_pageReady( false ),
        m_shellRequested( false ),
        m_agentCapturing( false ),
        m_agentSentinelStartFound( false )
{
#ifdef __APPLE__
    // Match background color to system theme
    if( IsSystemDarkMode() )
        SetBackgroundColour( wxColour( 30, 30, 30 ) );
    else
        SetBackgroundColour( wxColour( 255, 255, 255 ) );
#else
    SetBackgroundColour( wxColour( 30, 30, 30 ) );
#endif

    // Create the webview that will host xterm.js
    m_webView = new WEBVIEW_PANEL( this );
    m_webView->BindLoadedEvent();

    // Register message handler for JS → C++ communication
    m_webView->AddMessageHandler( wxS( "terminal" ),
            [this]( const wxString& msg ) { OnMessage( msg ); } );

    // Load the xterm.js HTML page
#ifdef __APPLE__
    bool lightMode = !IsSystemDarkMode();
#else
    bool lightMode = false;
#endif
    m_webView->SetPage( GetTerminalHtml( lightMode ) );

    // Layout: webview fills entire panel
    wxBoxSizer* sizer = new wxBoxSizer( wxVERTICAL );
    sizer->Add( m_webView, 1, wxEXPAND );
    SetSizer( sizer );
    Layout();

    // Bind PTY events
    Bind( wxEVT_PTY_DATA, &PTY_WEBVIEW_PANEL::OnPtyData, this );
    Bind( wxEVT_PTY_EXIT, &PTY_WEBVIEW_PANEL::OnPtyExit, this );

    // Bind size event to ensure webview fills panel on resize
    Bind( wxEVT_SIZE, &PTY_WEBVIEW_PANEL::OnSize, this );

    // Agent timeout timer
    m_agentTimeoutTimer.SetOwner( this );
    Bind( wxEVT_TIMER, &PTY_WEBVIEW_PANEL::OnAgentTimeout, this, m_agentTimeoutTimer.GetId() );

    // Title update timer - polls foreground process info periodically
    m_titleUpdateTimer.SetOwner( this );
    Bind( wxEVT_TIMER, &PTY_WEBVIEW_PANEL::OnTitleUpdate, this, m_titleUpdateTimer.GetId() );
}


PTY_WEBVIEW_PANEL::~PTY_WEBVIEW_PANEL()
{
    wxLogInfo( "PTY_WEBVIEW_PANEL destructor called" );

    m_titleUpdateTimer.Stop();
    m_agentTimeoutTimer.Stop();
    wxLogInfo( "PTY_WEBVIEW_PANEL - timers stopped" );

    if( m_pty )
    {
        wxLogInfo( "PTY_WEBVIEW_PANEL - stopping PTY..." );
        m_pty->Stop();
        wxLogInfo( "PTY_WEBVIEW_PANEL - PTY stopped" );
    }

    wxLogInfo( "PTY_WEBVIEW_PANEL destructor complete" );
}


bool PTY_WEBVIEW_PANEL::StartShell()
{
    m_shellRequested = true;

    // If the page is already ready, start immediately
    if( m_pageReady )
    {
        m_pty = std::make_unique<PTY_HANDLER>( this );

        if( !m_pty->Start( m_cols, m_rows ) )
        {
            wxLogError( "PTY_WEBVIEW: Failed to start PTY" );
            m_pty.reset();
            return false;
        }

        // Start title update timer to poll foreground process info
        m_titleUpdateTimer.Start( TITLE_UPDATE_MS );
    }
    // Otherwise, PTY will be started when JS sends 'ready'

    return true;
}


bool PTY_WEBVIEW_PANEL::IsShellRunning() const
{
    return m_pty && m_pty->IsRunning();
}


wxString PTY_WEBVIEW_PANEL::GetTitle() const
{
    if( !m_termTitle.IsEmpty() )
        return m_termTitle;

    // Default title shows shell type
    const char* shell = getenv( "SHELL" );

    if( shell )
    {
        wxString shellName = wxString::FromUTF8( shell );
        int lastSlash = shellName.Find( '/', true );

        if( lastSlash != wxNOT_FOUND )
            shellName = shellName.Mid( lastSlash + 1 );

        return shellName;
    }

    return "Shell";
}


// ---- PTY Events ----

void PTY_WEBVIEW_PANEL::OnPtyData( wxThreadEvent& aEvent )
{
    std::string data = aEvent.GetPayload<std::string>();

    // Process agent capture if active
    if( m_agentCapturing )
        ProcessAgentCapture( data );

    // Send to xterm.js
    if( m_pageReady )
        SendDataToTerminal( data );
    else
        m_pendingOutput += data;
}


void PTY_WEBVIEW_PANEL::OnPtyExit( wxThreadEvent& aEvent )
{
    wxLogInfo( "PTY_WEBVIEW: Shell process exited" );

    if( m_agentCapturing && m_agentCallback )
    {
        m_agentTimeoutTimer.Stop();
        m_agentCapturing = false;
        m_agentCallback( "Error: Shell process exited", false );
        m_agentCallback = nullptr;
    }
}


void PTY_WEBVIEW_PANEL::OnSize( wxSizeEvent& aEvent )
{
    // Ensure the webview fills the panel when resized
    Layout();

    // Explicitly trigger xterm.js to refit after layout
    if( m_pageReady && m_webView )
    {
        m_webView->RunScriptAsync( "if(window.triggerRefit) window.triggerRefit();" );
    }

    aEvent.Skip();
}


void PTY_WEBVIEW_PANEL::SendDataToTerminal( const std::string& aData )
{
    if( aData.empty() )
        return;

    // Base64 encode the raw PTY output for safe JS transfer
    wxString b64 = wxBase64Encode( aData.data(), aData.size() );

    // Call the JS function that writes to xterm.js
    m_webView->RunScriptAsync( wxString::Format( "termWrite('%s')", b64 ) );
}


// ---- JS Message Handler ----

void PTY_WEBVIEW_PANEL::OnMessage( const wxString& aMsg )
{
    try
    {
        nlohmann::json msg = nlohmann::json::parse( aMsg.ToStdString() );
        std::string    action = msg.value( "action", "" );

        if( action == "ready" )
        {
            m_pageReady = true;
            m_cols = msg.value( "cols", 80 );
            m_rows = msg.value( "rows", 24 );

            wxLogInfo( "PTY_WEBVIEW: Page ready, cols=%d rows=%d", m_cols, m_rows );

            // Start PTY if requested
            if( m_shellRequested && !m_pty )
            {
                m_pty = std::make_unique<PTY_HANDLER>( this );

                if( !m_pty->Start( m_cols, m_rows ) )
                {
                    wxLogError( "PTY_WEBVIEW: Failed to start PTY" );
                    m_pty.reset();
                }
                else
                {
                    // Start title update timer to poll foreground process info
                    m_titleUpdateTimer.Start( TITLE_UPDATE_MS );
                }
            }

            // Flush any buffered output
            if( !m_pendingOutput.empty() )
            {
                SendDataToTerminal( m_pendingOutput );
                m_pendingOutput.clear();
            }

            // Execute queued agent command if any
            if( !m_queuedAgentCmd.empty() && m_queuedAgentCallback )
            {
                ExecuteForAgent( m_queuedAgentCmd, m_queuedAgentCallback );
                m_queuedAgentCmd.clear();
                m_queuedAgentCallback = nullptr;
            }
        }
        else if( action == "input" )
        {
            // User typed something in xterm.js → send to PTY
            std::string b64Data = msg.value( "data", "" );

            if( !b64Data.empty() && m_pty )
            {
                wxMemoryBuffer decoded = wxBase64Decode( wxString::FromUTF8( b64Data ) );
                m_pty->Write( (const char*) decoded.GetData(), decoded.GetDataLen() );
            }
        }
        else if( action == "resize" )
        {
            int cols = msg.value( "cols", m_cols );
            int rows = msg.value( "rows", m_rows );

            if( cols != m_cols || rows != m_rows )
            {
                m_cols = cols;
                m_rows = rows;

                if( m_pty )
                    m_pty->Resize( cols, rows );
            }
        }
        else if( action == "title" )
        {
            wxString newTitle = wxString::FromUTF8( msg.value( "title", "" ) );

            if( newTitle != m_termTitle )
            {
                m_termTitle = newTitle;

                // Notify parent (TERMINAL_FRAME) to update the notebook tab
                wxCommandEvent evt( wxEVT_TERMINAL_TITLE_CHANGED );
                evt.SetEventObject( this );
                evt.SetString( m_termTitle );
                wxPostEvent( GetParent(), evt );
            }
        }
    }
    catch( const std::exception& e )
    {
        wxLogDebug( "PTY_WEBVIEW: Failed to parse message: %s", e.what() );
    }
}


// ---- Agent Integration ----

void PTY_WEBVIEW_PANEL::WriteToShell( const std::string& aData )
{
    if( m_pty )
        m_pty->Write( aData );
}


void PTY_WEBVIEW_PANEL::DisplayAgentCommand( const wxString& aCmd, const wxString& aMode )
{
    // No-op: the command will be visible in the terminal output via PTY echo
}


void PTY_WEBVIEW_PANEL::ExecuteForAgent( const std::string& aCmd, AgentCallback aCallback )
{
    if( !m_pty || !m_pty->IsRunning() )
    {
        // If PTY isn't ready yet, queue the command
        if( m_shellRequested && !m_pty )
        {
            m_queuedAgentCmd = aCmd;
            m_queuedAgentCallback = aCallback;
            return;
        }

        if( aCallback )
            aCallback( "Error: Shell not running", false );

        return;
    }

    m_agentCallback = aCallback;
    m_agentCapturing = true;
    m_agentCaptureBuffer.clear();
    m_agentSentinelStart = "__ZEO_CMD_START_" + std::to_string( (intptr_t) this ) + "__";
    m_agentSentinelEnd = "__ZEO_CMD_END_" + std::to_string( (intptr_t) this ) + "__";
    m_agentSentinelStartFound = false;

    // Start timeout timer
    m_agentTimeoutTimer.Start( AGENT_TIMEOUT_MS, wxTIMER_ONE_SHOT );

    // Write the command wrapped in sentinels
    std::string wrappedCmd = "echo '" + m_agentSentinelStart + "'\n"
                             + aCmd + "\n"
                             + "echo '" + m_agentSentinelEnd + "'\n";
    m_pty->Write( wrappedCmd );
}


void PTY_WEBVIEW_PANEL::ProcessAgentCapture( const std::string& aData )
{
    m_agentCaptureBuffer += aData;

    // Look for start sentinel
    if( !m_agentSentinelStartFound )
    {
        size_t startPos = m_agentCaptureBuffer.find( m_agentSentinelStart );

        if( startPos != std::string::npos )
        {
            m_agentSentinelStartFound = true;

            size_t endOfSentinel = startPos + m_agentSentinelStart.size();

            if( endOfSentinel < m_agentCaptureBuffer.size()
                && m_agentCaptureBuffer[endOfSentinel] == '\n' )
            {
                endOfSentinel++;
            }

            m_agentCaptureBuffer = m_agentCaptureBuffer.substr( endOfSentinel );
        }
    }

    // Look for end sentinel
    if( m_agentSentinelStartFound )
    {
        size_t endPos = m_agentCaptureBuffer.find( m_agentSentinelEnd );

        if( endPos != std::string::npos )
        {
            std::string output = m_agentCaptureBuffer.substr( 0, endPos );

            while( !output.empty() && output.back() == '\n' )
                output.pop_back();

            m_agentTimeoutTimer.Stop();
            m_agentCapturing = false;

            if( m_agentCallback )
            {
                m_agentCallback( output.empty() ? "(no output)" : output, true );
                m_agentCallback = nullptr;
            }
        }
    }
}


void PTY_WEBVIEW_PANEL::OnAgentTimeout( wxTimerEvent& aEvent )
{
    if( !m_agentCapturing )
        return;

    m_agentCapturing = false;

    if( m_agentCallback )
    {
        m_agentCallback( "Error: Command execution timed out", false );
        m_agentCallback = nullptr;
    }
}


void PTY_WEBVIEW_PANEL::OnTitleUpdate( wxTimerEvent& aEvent )
{
    if( !m_pty || !m_pty->IsRunning() )
        return;

    // Get the foreground process name and cwd
    std::string procName = m_pty->GetForegroundProcessName();
    std::string procCwd = m_pty->GetForegroundCwd();

    if( procName.empty() && procCwd.empty() )
        return;

    // Build a title like "vim: ~/projects/myapp" or just "zsh: ~/projects"
    wxString newTitle;

    // Get the short directory name (last component or ~ for home)
    wxString shortDir;

    if( !procCwd.empty() )
    {
        wxString cwd = wxString::FromUTF8( procCwd );

        // Replace home directory with ~
        wxString homeDir = wxGetHomeDir();

        if( cwd.StartsWith( homeDir ) )
        {
            if( cwd.length() == homeDir.length() )
            {
                shortDir = "~";
            }
            else
            {
                cwd = "~" + cwd.Mid( homeDir.length() );
            }
        }

        if( shortDir.IsEmpty() )
        {
            // Get last path component
            int lastSlash = cwd.Find( '/', true );

            if( lastSlash != wxNOT_FOUND && lastSlash < (int) cwd.length() - 1 )
                shortDir = cwd.Mid( lastSlash + 1 );
            else
                shortDir = cwd;
        }
    }

    // Format: "process: directory" like modern terminals
    if( !procName.empty() )
    {
        newTitle = wxString::FromUTF8( procName );

        if( !shortDir.IsEmpty() )
            newTitle += ": " + shortDir;
    }
    else if( !shortDir.IsEmpty() )
    {
        newTitle = shortDir;
    }

    // Only send event if title actually changed
    if( newTitle != m_termTitle && !newTitle.IsEmpty() )
    {
        m_termTitle = newTitle;

        wxCommandEvent evt( wxEVT_TERMINAL_TITLE_CHANGED );
        evt.SetEventObject( this );
        evt.SetString( m_termTitle );
        wxPostEvent( GetParent(), evt );
    }
}


// ---- HTML Template ----

wxString PTY_WEBVIEW_PANEL::GetTerminalHtml( bool aLightMode )
{
    // Choose background color based on theme
    wxString bgColor = aLightMode ? wxS( "#ffffff" ) : wxS( "#1e1e1e" );
    wxString themeMode = aLightMode ? wxS( "true" ) : wxS( "false" );

    wxString html = wxString::Format( R"HTML(<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<link rel="stylesheet" href="https://cdn.jsdelivr.net/npm/xterm@5.3.0/css/xterm.min.css">
<style>
    * {
        margin: 0;
        padding: 0;
        box-sizing: border-box;
    }
    html, body {
        width: 100%%;
        height: 100%%;
        overflow: hidden;
        background: %s;
    }
    #terminal {
        position: absolute;
        top: 0;
        left: 0;
        right: 0;
        bottom: 0;
    }
    /* Ensure xterm internal elements fill the container */
    .xterm,
    .xterm-viewport,
    .xterm-screen {
        width: 100%% !important;
        height: 100%% !important;
    }
</style>
</head>
<body>
<div id="terminal"></div>

<script src="https://cdn.jsdelivr.net/npm/xterm@5.3.0/lib/xterm.min.js"></script>
<script src="https://cdn.jsdelivr.net/npm/xterm-addon-fit@0.8.0/lib/xterm-addon-fit.min.js"></script>
<script>
(function() {
    'use strict';

    var isLightMode = %s;

    // Send message to C++ via WebKit message handler
    function sendMsg(action, data) {
        var msg = JSON.stringify(Object.assign({action: action}, data || {}));
        if (window.webkit && window.webkit.messageHandlers && window.webkit.messageHandlers.terminal) {
            window.webkit.messageHandlers.terminal.postMessage(msg);
        }
    }

    // Theme definitions
    var darkTheme = {
        background: '#1e1e1e',
        foreground: '#d4d4d4',
        cursor: '#aeafad',
        cursorAccent: '#1e1e1e',
        selectionBackground: '#44688b',
        black: '#1e1e1e',
        red: '#f44747',
        green: '#6a9955',
        yellow: '#d7ba7d',
        blue: '#569cd6',
        magenta: '#c586c0',
        cyan: '#4ec9b0',
        white: '#d4d4d4',
        brightBlack: '#808080',
        brightRed: '#f44747',
        brightGreen: '#6a9955',
        brightYellow: '#d7ba7d',
        brightBlue: '#569cd6',
        brightMagenta: '#c586c0',
        brightCyan: '#4ec9b0',
        brightWhite: '#ffffff'
    };

    var lightTheme = {
        background: '#ffffff',
        foreground: '#383a42',
        cursor: '#526eff',
        cursorAccent: '#ffffff',
        selectionBackground: '#add6ff',
        black: '#383a42',
        red: '#e45649',
        green: '#50a14f',
        yellow: '#c18401',
        blue: '#4078f2',
        magenta: '#a626a4',
        cyan: '#0184bc',
        white: '#a0a1a7',
        brightBlack: '#4f525e',
        brightRed: '#e06c75',
        brightGreen: '#98c379',
        brightYellow: '#e5c07b',
        brightBlue: '#61afef',
        brightMagenta: '#c678dd',
        brightCyan: '#56b6c2',
        brightWhite: '#ffffff'
    };

    // Create terminal with appropriate theme
    var term = new Terminal({
        theme: isLightMode ? lightTheme : darkTheme,
        fontFamily: 'Menlo, Monaco, "Courier New", monospace',
        fontSize: 13,
        cursorBlink: true,
        scrollback: 10000,
        allowProposedApi: true
    });

    // Fit addon for auto-resize
    var fitAddon = new FitAddon.FitAddon();
    term.loadAddon(fitAddon);

    // Open terminal in container
    term.open(document.getElementById('terminal'));
    fitAddon.fit();

    // User input → C++ → PTY
    term.onData(function(data) {
        sendMsg('input', { data: btoa(data) });
    });

    // Binary input (e.g., from some paste operations)
    term.onBinary(function(data) {
        sendMsg('input', { data: btoa(data) });
    });

    // Terminal resized → notify C++ to resize PTY
    term.onResize(function(size) {
        sendMsg('resize', { cols: size.cols, rows: size.rows });
    });

    // Title change → notify C++
    term.onTitleChange(function(title) {
        sendMsg('title', { title: title });
    });

    // Watch for container resize → refit terminal
    var resizeObserver = new ResizeObserver(function() {
        try { fitAddon.fit(); } catch(e) {}
    });
    resizeObserver.observe(document.getElementById('terminal'));

    // C++ calls this to write PTY data to the terminal
    // Data is base64 encoded for safe transfer
    window.termWrite = function(b64data) {
        try {
            var raw = atob(b64data);
            var bytes = new Uint8Array(raw.length);
            for (var i = 0; i < raw.length; i++) {
                bytes[i] = raw.charCodeAt(i);
            }
            term.write(bytes);
        } catch(e) {
            console.error('termWrite error:', e);
        }
    };

    // C++ calls this to trigger a refit when the panel is resized
    window.triggerRefit = function() {
        try {
            fitAddon.fit();
        } catch(e) {
            console.error('triggerRefit error:', e);
        }
    };

    // Signal to C++ that the page is ready
    // Use setTimeout to ensure xterm.js is fully initialized
    setTimeout(function() {
        fitAddon.fit();
        sendMsg('ready', { cols: term.cols, rows: term.rows });
    }, 50);

    // Additional delayed refit to catch late layout changes
    setTimeout(function() {
        fitAddon.fit();
        sendMsg('resize', { cols: term.cols, rows: term.rows });
    }, 200);

})();
</script>
</body>
</html>)HTML", bgColor, themeMode );

    return html;
}
