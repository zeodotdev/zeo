#include "agent_frame.h"
#include "agent_thread.h"
#include "agent_chat_history.h"
#include "auth/agent_auth.h"
#include "auth/agent_keychain.h"
#include "ui/pending_changes_popup.h"
#include "ui/history_panel.h"
#include "rendering/agent_markdown.h"
#include "core/agent_tools.h"
#include "core/chat_controller.h"
#include "core/chat_events.h"
#include <kiway_express.h>
#include <mail_type.h>
#include <wx/log.h>
#include <kiway.h>
#include <sstream>
#include <fstream>
#include <thread>
#include <set>
#include <algorithm>
#include <wx/sizer.h>
#include <wx/textctrl.h>
#include <wx/msgdlg.h>
#include <wx/utils.h>
#include <wx/stdpaths.h>
#include <wx/filename.h>
#include <wx/stattext.h>
#include <bitmaps.h>
#include <id.h>
#include <nlohmann/json.hpp>
#include <wx/settings.h>
#include <wx/clipbrd.h>
#include <wx/menu.h>
#include <wx/toolbar.h>
#include <wx/bitmap.h>
#include <wx/icon.h>

using json = nlohmann::json;

BEGIN_EVENT_TABLE( AGENT_FRAME, KIWAY_PLAYER )
EVT_MENU( wxID_EXIT, AGENT_FRAME::OnExit )

END_EVENT_TABLE()

AGENT_FRAME::AGENT_FRAME( KIWAY* aKiway, wxWindow* aParent ) :
        KIWAY_PLAYER( aKiway, aParent, FRAME_AGENT, "Agent", wxDefaultPosition, wxDefaultSize, wxDEFAULT_FRAME_STYLE,
                      "agent_frame_name", schIUScale ),
        m_signInOverlay( nullptr ),
        m_signInButton( nullptr ),
        m_pendingChangesBtn( nullptr ),
        m_pendingChangesPanel( nullptr ),
        m_historyPanel( nullptr ),
        m_workerThread( nullptr ),
        m_hasPendingSchChanges( false ),
        m_hasPendingPcbChanges( false ),
        m_pendingSchSheetPath( wxEmptyString ),
        m_pendingPcbFilename( wxEmptyString ),
        m_pendingOpenSch( false ),
        m_pendingOpenPcb( false )
{

    // --- UI Layout ---

    // Top Bar: Chat name (left) + History button (right)
    // Middle: Chat History (Expandable)
    // Bottom: Input Container (Unified)

    wxBoxSizer* mainSizer = new wxBoxSizer( wxVERTICAL );

    // Top Bar Panel (solid background to prevent transparency)
    wxPanel* topBarPanel = new wxPanel( this, wxID_ANY );
    topBarPanel->SetBackgroundColour( wxColour( "#1E1E1E" ) );

    wxBoxSizer* topBarSizer = new wxBoxSizer( wxHORIZONTAL );

    // Chat name label on the left
    m_chatNameLabel = new wxStaticText( topBarPanel, wxID_ANY, "New Chat" );
    m_chatNameLabel->SetForegroundColour( wxColour( "#FFFFFF" ) );
    topBarSizer->Add( m_chatNameLabel, 0, wxALIGN_CENTER_VERTICAL );

    topBarSizer->AddStretchSpacer();

    // New Chat button
    m_newChatButton = new wxButton( topBarPanel, wxID_ANY, "New Chat", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT );
    topBarSizer->Add( m_newChatButton, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5 );

    // History button on the right
    m_historyButton = new wxButton( topBarPanel, wxID_ANY, "History", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT );
    topBarSizer->Add( m_historyButton, 0, wxALIGN_CENTER_VERTICAL );

    // Set sizer on the panel with internal padding
    wxBoxSizer* topBarOuterSizer = new wxBoxSizer( wxVERTICAL );
    topBarOuterSizer->Add( topBarSizer, 1, wxEXPAND | wxALL, 10 );
    topBarPanel->SetSizer( topBarOuterSizer );

    // Add top bar panel to main sizer
    mainSizer->Add( topBarPanel, 0, wxEXPAND );

    // 1. Chat History Area
    m_chatWindow =
            new wxHtmlWindow( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxHW_SCROLLBAR_AUTO | wxBORDER_NONE );
    // Set a default page content or styling if needed
    m_fullHtmlContent = "<html><body bgcolor='#1E1E1E' text='#FFFFFF'><p>Welcome to KiCad Agent.</p></body></html>";
    m_chatWindow->SetPage( m_fullHtmlContent );
    mainSizer->Add( m_chatWindow, 1, wxEXPAND | wxALL, 0 ); // Remove ALL padding for clean edge

    // Pending Changes Panel (between chat and input, hidden by default)
    m_pendingChangesPanel = new PENDING_CHANGES_PANEL( this, this );
    mainSizer->Add( m_pendingChangesPanel, 0, wxEXPAND );

    // 2. Input Container (Unified Look)
    // Create m_inputPanel for dark styling
    m_inputPanel = new wxPanel( this, wxID_ANY );
    m_inputPanel->SetBackgroundColour( wxColour( "#1E1E1E" ) ); // 1E1E1E

    // Use a vertical BoxSizer for the panel
    wxBoxSizer* outerInputSizer = new wxBoxSizer( wxVERTICAL );

    // Use an inner sizer for content padding
    wxBoxSizer* inputContainerSizer = new wxBoxSizer( wxVERTICAL );

    // Top row: Selection Pill (left) + Pending Changes Button (right)
    wxBoxSizer* topRowSizer = new wxBoxSizer( wxHORIZONTAL );

    // Status Pill (Selection Info / Add Context)
    m_selectionPill = new wxButton( m_inputPanel, wxID_ANY, "No Selection", wxDefaultPosition, wxDefaultSize );
    // Removed custom colors to keep native round look
    m_selectionPill->Hide(); // Hide on load
    topRowSizer->Add( m_selectionPill, 0, wxALIGN_CENTER_VERTICAL );

    topRowSizer->AddStretchSpacer();

    // Pending Changes Button (hidden by default)
    m_pendingChangesBtn = new wxButton( m_inputPanel, wxID_ANY, "1 change" );
    m_pendingChangesBtn->Hide();
    topRowSizer->Add( m_pendingChangesBtn, 0, wxALIGN_CENTER_VERTICAL );

    inputContainerSizer->Add( topRowSizer, 0, wxEXPAND | wxBOTTOM, 5 );

    // 2a. Text Input (Top)
    m_inputCtrl = new wxTextCtrl( m_inputPanel, wxID_ANY, "", wxDefaultPosition, wxSize( -1, 60 ),
                                  wxTE_MULTILINE | wxTE_PROCESS_ENTER | wxBORDER_NONE );
    m_inputCtrl->SetBackgroundColour( wxColour( "#1E1E1E" ) );
    m_inputCtrl->SetForegroundColour( wxColour( "#FFFFFF" ) );

    // Use system default font (matches chat name label)
    wxFont font = wxSystemSettings::GetFont( wxSYS_DEFAULT_GUI_FONT );
    font.SetPointSize( 12 );
    m_inputCtrl->SetFont( font );

    inputContainerSizer->Add( m_inputCtrl, 1, wxEXPAND | wxBOTTOM, 5 );

    // 2b. Control Row (Bottom)
    wxBoxSizer* controlsSizer = new wxBoxSizer( wxHORIZONTAL );

    // Model Selection (Claude models only - via harold.so proxy)
    wxArrayString modelChoices;
    modelChoices.Add( "Claude 4.5 Sonnet" );
    modelChoices.Add( "Claude 4.5 Opus" );

    m_modelChoice = new wxChoice( m_inputPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, modelChoices );
    m_modelChoice->SetSelection( 0 ); // Default to Claude 4.5 Sonnet
    controlsSizer->Add( m_modelChoice, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5 );

    // Spacer
    controlsSizer->AddStretchSpacer();

    // Send Button
    m_actionButton = new wxButton( m_inputPanel, wxID_ANY, "Send" );
    controlsSizer->Add( m_actionButton, 0, wxALIGN_CENTER_VERTICAL );

    inputContainerSizer->Add( controlsSizer, 0, wxEXPAND );


    
    // Add inner sizer to outer sizer with padding (including top margin)
    outerInputSizer->Add( inputContainerSizer, 1, wxEXPAND | wxALL, 10 );

    m_inputPanel->SetSizer( outerInputSizer );

    // ACCELERATOR TABLE for Cmd+C
    wxAcceleratorEntry entries[1];
    entries[0].Set( wxACCEL_CTRL, (int) 'C', ID_CHAT_COPY );
    wxAcceleratorTable accel( 1, entries );
    SetAcceleratorTable( accel );

    // Add Input Container to Main Sizer
    mainSizer->Add( m_inputPanel, 0, wxEXPAND );

    SetSizer( mainSizer );
    Layout();
    SetSize( 500, 600 ); // Slightly taller for chat

    // History Panel (full overlay, not in sizer - positioned absolutely)
    m_historyPanel = new HISTORY_PANEL( this, this );
    m_historyPanel->Hide();

    // Bind Events
    m_actionButton->Bind( wxEVT_BUTTON, &AGENT_FRAME::OnSend, this );
    m_newChatButton->Bind( wxEVT_BUTTON, &AGENT_FRAME::OnNewChat, this );
    m_historyButton->Bind( wxEVT_BUTTON, &AGENT_FRAME::OnHistoryTool, this );
    m_inputCtrl->Bind( wxEVT_TEXT_ENTER, &AGENT_FRAME::OnTextEnter, this );
    m_selectionPill->Bind( wxEVT_BUTTON, &AGENT_FRAME::OnSelectionPillClick, this );
    m_pendingChangesBtn->Bind( wxEVT_BUTTON, &AGENT_FRAME::OnPendingChangesClick, this );

    // m_toolButton->Bind( wxEVT_BUTTON, &AGENT_FRAME::OnToolClick, this );
    m_chatWindow->Bind( wxEVT_HTML_LINK_CLICKED, &AGENT_FRAME::OnHtmlLinkClick, this );
    m_chatWindow->Bind( wxEVT_RIGHT_DOWN, &AGENT_FRAME::OnChatRightClick, this );
    m_chatWindow->Bind( wxEVT_SCROLLWIN_THUMBTRACK, &AGENT_FRAME::OnChatScroll, this );
    m_chatWindow->Bind( wxEVT_SCROLLWIN_THUMBRELEASE, &AGENT_FRAME::OnChatScroll, this );
    m_chatWindow->Bind( wxEVT_SCROLLWIN_LINEUP, &AGENT_FRAME::OnChatScroll, this );
    m_chatWindow->Bind( wxEVT_SCROLLWIN_LINEDOWN, &AGENT_FRAME::OnChatScroll, this );
    m_chatWindow->Bind( wxEVT_SCROLLWIN_PAGEUP, &AGENT_FRAME::OnChatScroll, this );
    m_chatWindow->Bind( wxEVT_SCROLLWIN_PAGEDOWN, &AGENT_FRAME::OnChatScroll, this );
    m_chatWindow->Bind( wxEVT_MOUSEWHEEL, [this]( wxMouseEvent& evt ) {
        // Detect mouse wheel scroll during generation or tool execution
        bool isBusy = m_isGenerating ||
                      m_conversationCtx.GetState() != AgentConversationState::IDLE;
        if( isBusy && evt.GetWheelRotation() > 0 )
        {
            // Scrolling up
            m_userScrolledUp = true;
        }
        evt.Skip();
    });
    Bind( wxEVT_MENU, &AGENT_FRAME::OnPopupClick, this, ID_CHAT_COPY );
    m_inputCtrl->Bind( wxEVT_KEY_DOWN, &AGENT_FRAME::OnInputKeyDown, this );
    m_inputCtrl->Bind( wxEVT_TEXT, &AGENT_FRAME::OnInputText, this );

    // Bind Thread Events
    Bind( wxEVT_AGENT_UPDATE, &AGENT_FRAME::OnAgentUpdate, this );
    Bind( wxEVT_AGENT_COMPLETE, &AGENT_FRAME::OnAgentComplete, this );

    // Bind Async LLM Streaming Events
    Bind( EVT_LLM_STREAM_CHUNK, &AGENT_FRAME::OnLLMStreamChunk, this );
    Bind( EVT_LLM_STREAM_COMPLETE, &AGENT_FRAME::OnLLMStreamComplete, this );
    Bind( EVT_LLM_STREAM_ERROR, &AGENT_FRAME::OnLLMStreamError, this );

    // Bind Title Generation Event
    Bind( EVT_TITLE_GENERATED, &AGENT_FRAME::OnTitleGeneratedEvent, this );

    // Initialize generating animation
    m_generatingDots = 0;
    m_isGenerating = false;
    m_userScrolledUp = false;
    m_generatingTimer.Bind( wxEVT_TIMER, &AGENT_FRAME::OnGeneratingTimer, this );

    // Initialize thinking state
    m_thinkingExpanded = false;
    m_isThinking = false;
    m_currentThinkingIndex = -1;

    // Initialize title generation
    m_needsTitleGeneration = true;
    m_firstUserMessage = "";

    // Bind Model Change Event
    m_modelChoice->Bind( wxEVT_CHOICE, &AGENT_FRAME::OnModelSelection, this );

    // Bind Size Event (to reposition overlay)
    Bind( wxEVT_SIZE, &AGENT_FRAME::OnSize, this );

    // Initialize History
    m_chatHistory = nlohmann::json::array();
    m_apiContext = nlohmann::json::array();
    m_pendingToolCalls = nlohmann::json::array();

    // Add welcome message as an assistant message
    m_chatHistory.push_back( {
        { "role", "assistant" },
        { "content", "Welcome to KiCad Agent." }
    } );

    // Initialize chat history persistence with timestamp conversation ID
    wxDateTime now = wxDateTime::Now();
    std::string conversationId = now.Format( "%Y-%m-%d_%H-%M-%S" ).ToStdString();
    m_chatHistoryDb.SetConversationId( conversationId );

    // Initialize LLM client and tools
    m_llmClient = std::make_unique<AGENT_LLM_CLIENT>( this );
    InitializeTools();

    // Initialize authentication
    m_auth = new AGENT_AUTH();
    
    // Load Supabase configuration from JSON file
    std::string supabaseUrl, supabaseKey;
    
    // Try loading from supabase_config.json in source directory
    wxFileName configPath( __FILE__ );
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
                
                wxLogTrace( "Agent", "Loaded Supabase config from %s", configPath.GetFullPath() );
            }
            catch( const std::exception& e )
            {
                wxLogWarning( "Failed to parse supabase_config.json: %s", e.what() );
            }
            configFile.close();
        }
    }
    
    if( !supabaseUrl.empty() && !supabaseKey.empty() )
    {
        m_auth->Configure( supabaseUrl, supabaseKey );
        wxLogTrace( "Agent", "Supabase authentication configured" );
    }
    else
    {
        wxLogWarning( "Supabase configuration not found. Authentication features will be disabled." );
        wxLogWarning( "Create supabase_config.json or set KICAD_AGENT_SUPABASE_URL/KEY environment variables." );
    }

    // Wire auth to LLM client for proxy authentication
    m_llmClient->SetAuth( m_auth );

    // Create chat controller
    m_chatController = std::make_unique<CHAT_CONTROLLER>( this );
    m_chatController->SetLLMClient( m_llmClient.get() );
    m_chatController->SetChatHistoryDb( &m_chatHistoryDb );
    m_chatController->SetKiwayRequestFn(
        [this]( int aFrameType, const std::string& aPayload ) -> std::string {
            return SendRequest( aFrameType, aPayload );
        } );

    // Set model on controller
    m_chatController->SetModel( "Claude 4.5 Sonnet" );

    // Bind Controller Events
    Bind( EVT_CHAT_TEXT_DELTA, &AGENT_FRAME::OnChatTextDelta, this );
    Bind( EVT_CHAT_THINKING_START, &AGENT_FRAME::OnChatThinkingStart, this );
    Bind( EVT_CHAT_THINKING_DELTA, &AGENT_FRAME::OnChatThinkingDelta, this );
    Bind( EVT_CHAT_THINKING_DONE, &AGENT_FRAME::OnChatThinkingDone, this );
    Bind( EVT_CHAT_TOOL_START, &AGENT_FRAME::OnChatToolStart, this );
    Bind( EVT_CHAT_TOOL_COMPLETE, &AGENT_FRAME::OnChatToolComplete, this );
    Bind( EVT_CHAT_TURN_COMPLETE, &AGENT_FRAME::OnChatTurnComplete, this );
    Bind( EVT_CHAT_ERROR, &AGENT_FRAME::OnChatError, this );
    Bind( EVT_CHAT_STATE_CHANGED, &AGENT_FRAME::OnChatStateChanged, this );
    Bind( EVT_CHAT_TITLE_GENERATED, &AGENT_FRAME::OnChatTitleGenerated, this );
    Bind( EVT_CHAT_HISTORY_LOADED, &AGENT_FRAME::OnChatHistoryLoaded, this );
    Bind( EVT_CHAT_CONTEXT_STATUS, &AGENT_FRAME::OnChatContextStatus, this );
    Bind( EVT_CHAT_CONTEXT_COMPACTING, &AGENT_FRAME::OnChatContextCompacting, this );
    Bind( EVT_CHAT_CONTEXT_RECOVERED, &AGENT_FRAME::OnChatContextRecovered, this );

    // Create menu bar
    wxMenuBar* menuBar = new wxMenuBar();
    wxMenu* fileMenu = new wxMenu();
    fileMenu->Append( wxID_EXIT, "E&xit\tAlt-X", "Exit application" );

    menuBar->Append( fileMenu, "&File" );
    SetMenuBar( menuBar );

    // Create sign-in overlay (covers entire frame when not authenticated)
    m_signInOverlay = new wxPanel( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL );
    m_signInOverlay->SetBackgroundColour( wxColour( 30, 30, 30, 230 ) ); // Dark semi-transparent

    wxBoxSizer* overlaySizer = new wxBoxSizer( wxVERTICAL );
    overlaySizer->AddStretchSpacer();

    wxStaticText* overlayLabel = new wxStaticText( m_signInOverlay, wxID_ANY, "Sign in to use the agent" );
    overlayLabel->SetForegroundColour( wxColour( 255, 255, 255 ) );
    wxFont labelFont = overlayLabel->GetFont();
    labelFont.SetPointSize( 16 );
    overlayLabel->SetFont( labelFont );
    overlaySizer->Add( overlayLabel, 0, wxALIGN_CENTER );

    m_signInButton = new wxButton( m_signInOverlay, wxID_ANY, "Sign In" );
    m_signInButton->Bind( wxEVT_BUTTON, &AGENT_FRAME::OnSignIn, this );
    overlaySizer->Add( m_signInButton, 0, wxALIGN_CENTER | wxTOP, 15 );

    overlaySizer->AddStretchSpacer();
    m_signInOverlay->SetSizer( overlaySizer );
    m_signInOverlay->Hide(); // Start hidden, UpdateAuthUI will show if needed

    // Update auth UI state
    UpdateAuthUI();
}

AGENT_FRAME::~AGENT_FRAME()
{
    // Stop the generating animation timer to prevent timer events
    m_generatingTimer.Stop();

    // Stop tool timeout timer
    m_toolTimeoutTimer.Stop();

    // Cancel any in-progress LLM request and wait for it to finish
    // This prevents the background thread from posting events to a destroyed handler
    if( m_llmClient )
    {
        m_llmClient->CancelRequest();

        // Wait for the request to finish (with timeout to prevent hang)
        int waitCount = 0;
        const int maxWaitMs = 5000;  // 5 second timeout
        const int sleepMs = 10;

        while( m_llmClient->IsRequestInProgress() && waitCount * sleepMs < maxWaitMs )
        {
            wxMilliSleep( sleepMs );
            waitCount++;
        }

        if( m_llmClient->IsRequestInProgress() )
        {
            wxLogWarning( "LLM request did not finish within timeout during AGENT_FRAME destruction" );
        }

        // Add a small safety delay to ensure the thread has fully finished
        // after m_requestInProgress is set to false
        wxMilliSleep( 50 );
    }

    delete m_auth;
}

void AGENT_FRAME::ShowChangedLanguage()
{
    KIWAY_PLAYER::ShowChangedLanguage();
}

void AGENT_FRAME::AppendHtml( const wxString& aHtml )
{
    // Insert content BEFORE the closing </body></html> tags to maintain valid HTML structure
    // This is critical - content outside <body> may not be rendered by wxHtmlWindow
    wxString closingTags = "</body></html>";

    if( m_fullHtmlContent.EndsWith( closingTags ) )
    {
        // Remove closing tags, append content, add closing tags back
        m_fullHtmlContent = m_fullHtmlContent.Left( m_fullHtmlContent.length() - closingTags.length() );
        m_fullHtmlContent += aHtml;
        m_fullHtmlContent += closingTags;
    }
    else
    {
        // No closing tags found, just append (shouldn't normally happen)
        m_fullHtmlContent += aHtml;
    }

    SetHtml( m_fullHtmlContent );
}

void AGENT_FRAME::RebuildThinkingHtml()
{
    // Rebuild thinking HTML based on current state
    // Shows "Thinking" as a clickable link that expands/collapses content

    if( m_thinkingContent.IsEmpty() && !m_isThinking )
    {
        m_thinkingHtml = "";
        return;
    }

    // Escape HTML in thinking content
    wxString escapedContent = m_thinkingContent;
    escapedContent.Replace( "&", "&amp;" );
    escapedContent.Replace( "<", "&lt;" );
    escapedContent.Replace( ">", "&gt;" );
    escapedContent.Replace( "\n", "<br>" );

    // Truncate if very long
    if( escapedContent.length() > 5000 )
    {
        escapedContent = escapedContent.Left( 5000 ) + "... <i>(truncated)</i>";
    }

    // Build the thinking HTML with indexed toggle link
    // Note: wxHtmlWindow link color overrides outer font, so put font INSIDE the <a> tag
    wxString thinkingLabel = wxString::Format(
        "<a href='toggle:thinking:%d'><font color='#808080'>Thinking</font></a>",
        m_currentThinkingIndex );

    if( m_thinkingExpanded && !escapedContent.IsEmpty() )
    {
        // Expanded: show thinking content with spacing after (no <p> to avoid top margin)
        m_thinkingHtml = wxString::Format(
            "%s<br><font color='#606060'>%s</font><br><br>",
            thinkingLabel, escapedContent );
    }
    else
    {
        // Collapsed: just show the label with spacing after
        m_thinkingHtml = wxString::Format( "%s<br><br>", thinkingLabel );
    }
}

void AGENT_FRAME::UpdateAgentResponse()
{
    // Re-render the full HTML with the current response formatted as markdown
    // IMPORTANT: m_htmlBeforeAgentResponse may contain </body></html> closing tags
    // We need to insert new content BEFORE those tags to maintain valid HTML structure

    wxString closingTags = "</body></html>";
    wxString html = m_htmlBeforeAgentResponse;

    // Get current response from controller (source of truth)
    std::string currentResponse = m_chatController ? m_chatController->GetCurrentResponse() : "";

    // Debug: log the state of HTML building
    fprintf( stderr, "[HTML-DEBUG] UpdateAgentResponse called\n" );
    fprintf( stderr, "[HTML-DEBUG]   m_htmlBeforeAgentResponse length: %zu, ends with closing tags: %s\n",
             (size_t)m_htmlBeforeAgentResponse.length(),
             m_htmlBeforeAgentResponse.EndsWith( closingTags ) ? "YES" : "NO" );
    fprintf( stderr, "[HTML-DEBUG]   currentResponse length: %zu\n", currentResponse.size() );
    fprintf( stderr, "[HTML-DEBUG]   m_toolCallHtml length: %zu\n", (size_t)m_toolCallHtml.length() );

    // Strip closing tags from the base HTML - we'll add them back at the end
    if( html.EndsWith( closingTags ) )
    {
        html = html.Left( html.length() - closingTags.length() );
        fprintf( stderr, "[HTML-DEBUG]   Stripped closing tags from base HTML\n" );
    }

    // Include thinking block HTML if present (shown before response)
    if( !m_thinkingHtml.IsEmpty() )
    {
        html += m_thinkingHtml;
    }

    // Append the current response with markdown formatting
    html += AgentMarkdown::ToHtml( currentResponse );

    // Include any tool call HTML (preserved across re-renders)
    if( !m_toolCallHtml.IsEmpty() )
    {
        html += m_toolCallHtml;
    }

    // Add animated dots if currently generating
    if( m_isGenerating )
    {
        wxString dots;
        for( int i = 0; i < m_generatingDots; i++ )
            dots += ".";
        html += "<font color='#888888'>" + dots + "</font>";
    }

    // Add closing tags back
    html += closingTags;

    fprintf( stderr, "[HTML-DEBUG]   Final HTML length: %zu\n", (size_t)html.length() );
    fflush( stderr );

    SetHtml( html );
    m_fullHtmlContent = html;
}

void AGENT_FRAME::OnGeneratingTimer( wxTimerEvent& aEvent )
{
    // Cycle through 1, 2, 3 dots
    m_generatingDots = ( m_generatingDots % 3 ) + 1;
    UpdateAgentResponse();

    // Auto-scroll (respects user scroll position)
    AutoScrollToBottom();
}

void AGENT_FRAME::StartGeneratingAnimation()
{
    m_isGenerating = true;
    m_generatingDots = 1;
    m_generatingTimer.Start( 400 ); // Update every 400ms
    m_actionButton->SetLabel( "Stop" );
}

void AGENT_FRAME::StopGeneratingAnimation()
{
    m_isGenerating = false;
    m_generatingTimer.Stop();
    m_generatingDots = 0;
    // Note: Don't set button to "Send" here - let the caller decide
    // (tool execution keeps "Stop", only IDLE sets "Send")
}

void AGENT_FRAME::GenerateChatTitle()
{
    wxLogDebug( "AGENT: GenerateChatTitle called, firstUserMessage='%s'", m_firstUserMessage.c_str() );

    if( m_firstUserMessage.empty() )
    {
        wxLogDebug( "AGENT: GenerateChatTitle - firstUserMessage is empty, returning" );
        return;
    }

    // Capture conversation ID on main thread (must match the chat we're generating for)
    std::string conversationId = m_chatHistoryDb.GetConversationId();
    if( conversationId.empty() )
    {
        wxLogDebug( "AGENT: GenerateChatTitle - conversationId is empty, returning" );
        return;
    }

    // Create a simple prompt for title generation
    std::string prompt = "Generate a very short title (3-6 words maximum) for a chat that starts with this question. "
                         "Reply with ONLY the title, no quotes, no punctuation at the end, no explanation.\n\n"
                         "User's question: " + m_firstUserMessage;

    // Capture model name on main thread (wxWidgets UI calls must be on main thread)
    std::string modelName = m_modelChoice->GetStringSelection().ToStdString();
    wxLogDebug( "AGENT: GenerateChatTitle - using model '%s', conversationId='%s'", modelName.c_str(), conversationId.c_str() );

    // Use a background thread to generate the title
    std::thread( [this, prompt, modelName, conversationId]() {
        try
        {
            fprintf( stderr, "[TITLE] Starting title generation with model: %s, convId: %s\n", modelName.c_str(), conversationId.c_str() );

            // Create a temporary LLM client for title generation
            AGENT_LLM_CLIENT titleClient( nullptr );

            titleClient.SetModel( modelName );

            // Simple non-streaming request for title
            nlohmann::json messages = nlohmann::json::array();
            messages.push_back( { { "role", "user" }, { "content", prompt } } );

            std::string title;
            // System prompt now handled server-side
            titleClient.AskStreamWithTools(
                messages,
                {},  // No tools needed
                [&title]( const LLM_EVENT& event ) {
                    if( event.type == LLM_EVENT_TYPE::TEXT )
                    {
                        title += event.text;
                    }
                }
            );

            fprintf( stderr, "[TITLE] Raw title from LLM: '%s'\n", title.c_str() );

            // Clean up the title (remove quotes, trim whitespace)
            while( !title.empty() && ( title.front() == '"' || title.front() == '\'' || title.front() == ' ' ) )
                title.erase( 0, 1 );
            while( !title.empty() && ( title.back() == '"' || title.back() == '\'' || title.back() == ' ' ||
                                        title.back() == '.' || title.back() == '\n' ) )
                title.pop_back();

            fprintf( stderr, "[TITLE] Cleaned title: '%s'\n", title.c_str() );

            // Post result to main thread using thread-safe event
            if( !title.empty() )
            {
                fprintf( stderr, "[TITLE] Posting title to main thread for convId: %s\n", conversationId.c_str() );
                PostTitleGenerated( this, title, conversationId );
            }
            else
            {
                fprintf( stderr, "[TITLE] Title is empty after cleanup, not posting\n" );
            }
        }
        catch( const std::exception& e )
        {
            fprintf( stderr, "[TITLE] Exception: %s\n", e.what() );
        }
        catch( ... )
        {
            fprintf( stderr, "[TITLE] Unknown exception occurred\n" );
        }
    }).detach();
}

void AGENT_FRAME::OnTitleGenerated( const std::string& aTitle, const std::string& aConversationId )
{
    wxLogDebug( "AGENT: OnTitleGenerated called with title='%s', convId='%s'", aTitle.c_str(), aConversationId.c_str() );
    fprintf( stderr, "[TITLE] OnTitleGenerated: '%s' for convId: %s\n", aTitle.c_str(), aConversationId.c_str() );

    // Check if we're still on the same conversation
    std::string currentConvId = m_chatHistoryDb.GetConversationId();

    if( currentConvId == aConversationId )
    {
        // We're on the same conversation - update UI and save
        m_chatHistoryDb.SetTitle( aTitle );
        m_chatHistoryDb.Save( m_chatHistory );
        m_chatNameLabel->SetLabel( wxString::FromUTF8( aTitle ) );
        m_needsTitleGeneration = false;
        fprintf( stderr, "[TITLE] Title saved and UI updated for current chat\n" );
    }
    else
    {
        // Different conversation - need to load, update, and save that conversation
        fprintf( stderr, "[TITLE] Title is for different conversation, saving directly to file\n" );

        // Create a temporary chat history object to save to the correct file
        AGENT_CHAT_HISTORY tempHistory;
        nlohmann::json messages = tempHistory.Load( aConversationId );
        tempHistory.SetTitle( aTitle );
        tempHistory.Save( messages );

        fprintf( stderr, "[TITLE] Title saved to conversation file: %s\n", aConversationId.c_str() );
    }

    wxLogDebug( "AGENT: Title saved" );
}

void AGENT_FRAME::OnTitleGeneratedEvent( wxThreadEvent& aEvent )
{
    wxLogDebug( "AGENT: OnTitleGeneratedEvent received" );
    fprintf( stderr, "[TITLE] OnTitleGeneratedEvent received\n" );

    // Extract the title data from the event payload
    TitleGeneratedData* data = aEvent.GetPayload<TitleGeneratedData*>();
    if( data )
    {
        fprintf( stderr, "[TITLE] Got title from event: '%s' for convId: %s\n", data->title.c_str(), data->conversationId.c_str() );
        OnTitleGenerated( data->title, data->conversationId );
        delete data;
    }
    else
    {
        fprintf( stderr, "[TITLE] Event payload was null\n" );
    }
}

void AGENT_FRAME::SetHtml( const wxString& aHtml )
{
    m_fullHtmlContent = aHtml; // Ensure sync

    // Save scroll position
    int x, y;
    m_chatWindow->GetViewStart( &x, &y );

    m_chatWindow->SetPage( m_fullHtmlContent );

    // Restore scroll position
    m_chatWindow->Scroll( x, y );
}

void AGENT_FRAME::AutoScrollToBottom()
{
    // Only auto-scroll if user hasn't scrolled up during this generation session
    if( m_userScrolledUp )
        return;

    // Get virtual size and client size
    int virtWidth, virtHeight;
    m_chatWindow->GetVirtualSize( &virtWidth, &virtHeight );

    int clientWidth, clientHeight;
    m_chatWindow->GetClientSize( &clientWidth, &clientHeight );

    // Get scroll units (pixels per scroll unit)
    int scrollUnitX, scrollUnitY;
    m_chatWindow->GetScrollPixelsPerUnit( &scrollUnitX, &scrollUnitY );

    if( scrollUnitY <= 0 )
        scrollUnitY = 10; // Fallback

    // Calculate max scroll position in scroll units
    // Use ceiling division to ensure we scroll completely to the bottom
    int maxScrollY = ( virtHeight - clientHeight + scrollUnitY - 1 ) / scrollUnitY;
    if( maxScrollY < 0 )
        maxScrollY = 0;

    // Scroll to bottom
    m_chatWindow->Scroll( 0, maxScrollY );
}

void AGENT_FRAME::OnChatScroll( wxScrollWinEvent& aEvent )
{
    // Only track scroll during generation or tool execution
    if( !m_isGenerating && m_conversationCtx.GetState() == AgentConversationState::IDLE )
    {
        aEvent.Skip();
        return;
    }

    // Get current and max scroll positions
    int scrollX, scrollY;
    m_chatWindow->GetViewStart( &scrollX, &scrollY );

    int virtWidth, virtHeight;
    m_chatWindow->GetVirtualSize( &virtWidth, &virtHeight );

    int clientWidth, clientHeight;
    m_chatWindow->GetClientSize( &clientWidth, &clientHeight );

    int scrollUnitX, scrollUnitY;
    m_chatWindow->GetScrollPixelsPerUnit( &scrollUnitX, &scrollUnitY );

    if( scrollUnitY <= 0 )
        scrollUnitY = 10;

    // Use ceiling division to match AutoScrollToBottom
    int maxScrollY = ( virtHeight - clientHeight + scrollUnitY - 1 ) / scrollUnitY;
    if( maxScrollY < 0 )
        maxScrollY = 0;

    // If user scrolls up from bottom (more than ~30 pixels), detach auto-scroll
    int tolerance = 30 / scrollUnitY;
    if( tolerance < 2 )
        tolerance = 2;

    if( scrollY < maxScrollY - tolerance )
    {
        m_userScrolledUp = true;
    }
    else
    {
        // User scrolled back to bottom, re-attach
        m_userScrolledUp = false;
    }

    aEvent.Skip();
}

void AGENT_FRAME::KiwayMailIn( KIWAY_EXPRESS& aEvent )
{
    fprintf( stderr, "AGENT KiwayMailIn: Received command=%d\n", aEvent.Command() );
    fflush( stderr );

    if( aEvent.Command() == MAIL_AGENT_RESPONSE )
    {
        std::string payload = aEvent.GetPayload();
        fprintf( stderr, "AGENT KiwayMailIn: MAIL_AGENT_RESPONSE received, payload='%.100s...'\n",
                 payload.c_str() );
        fflush( stderr );

        // Check if we're in async tool execution mode (frame's context has an executing tool)
        // NOTE: The controller also executes tools via the synchronous SendRequest path,
        // which expects m_toolResponse to be set. Only use async path if the FRAME
        // actually has a tool marked as executing.
        PendingToolCall* executing = m_conversationCtx.GetExecutingToolCall();

        fprintf( stderr, "AGENT KiwayMailIn: executing=%p (state=%d)\n",
                 (void*)executing, static_cast<int>( m_conversationCtx.GetState() ) );
        fflush( stderr );

        if( executing )
        {
            // Frame has an executing tool - use async path
            fprintf( stderr, "AGENT KiwayMailIn: In async mode, found executing tool '%s'\n",
                     executing->tool_name.c_str() );
            fprintf( stderr, "AGENT KiwayMailIn: PendingToolCallCount=%zu\n",
                     m_conversationCtx.GetPendingToolCallCount() );
            fflush( stderr );

            // Post tool completion event
            ToolExecutionResult* result = new ToolExecutionResult();
            result->tool_use_id = executing->tool_use_id;
            result->tool_name = executing->tool_name;
            result->result = payload;
            result->success = !payload.empty() && payload.find( "Error:" ) != 0;
            result->execution_time_ms = ( wxGetLocalTimeMillis() - executing->start_time ).GetValue();

            PostToolResult( this, *result );
            fprintf( stderr, "AGENT KiwayMailIn: PostToolResult called\n" );
            fflush( stderr );
            delete result;  // PostToolResult copies the data
        }
        else
        {
            // Sync mode (controller path) - store response for SendRequest() to pick up
            fprintf( stderr, "AGENT KiwayMailIn: Sync mode - setting m_toolResponse\n" );
            fflush( stderr );
            m_toolResponse = payload;
        }
    }
    else if( aEvent.Command() == MAIL_SELECTION )
    {
        std::string payload = aEvent.GetPayload();

        if( payload.find( "JSON_PAYLOAD" ) == 0 )
        {
            std::stringstream ss( payload );
            std::string       prefix, source, jsonStr;
            std::getline( ss, prefix ); // JSON_PAYLOAD
            std::getline( ss, source ); // SCH or PCB

            // Remainder is JSON
            // We can determine start pos by prefix.length() + source.length() + 2 (newlines)
            // Or just read the rest of the stream
            std::string line;
            while( std::getline( ss, line ) )
            {
                jsonStr += line + "\n";
            }

            // Update JSON Store
            if( source == "SCH" )
                m_schJson = jsonStr;
            else if( source == "PCB" )
                m_pcbJson = jsonStr;

            // Update Selection Pill
            try
            {
                auto j = nlohmann::json::parse( jsonStr );
                if( j.contains( "selection" ) && !j["selection"].empty() )
                {
                    nlohmann::json& firstItem = j["selection"][0];
                    std::string     label = "Add: Selection"; // Default

                    if( firstItem.contains( "ref" ) )
                    {
                        label = "Add: " + firstItem["ref"].get<std::string>();
                    }
                    else if( firstItem.contains( "type" ) )
                    {
                        std::string type = firstItem["type"].get<std::string>();
                        if( type == "pin" || type == "pad" )
                        {
                            std::string num =
                                    firstItem.contains( "number" ) ? firstItem["number"].get<std::string>() : "?";
                            label = "Add: " + type + " " + num;
                        }
                        else if( type == "wire" || type == "track" )
                        {
                            label = "Add: " + type;
                            if( firstItem.contains( "net_name" ) )
                                label += " (" + firstItem["net_name"].get<std::string>() + ")";
                        }
                        else
                        {
                            label = "Add: " + type;
                        }
                    }

                    size_t count = j["selection"].size();
                    if( count > 1 )
                        label += " +" + std::to_string( count - 1 );

                    m_selectionPill->SetLabel( label );
                    m_selectionPill->Show( true );
                }
                else
                {
                    // If selection empty, hide pill?
                    // Or keep it if we want to add "Context"?
                    // For now, hide if no selection
                    // But wait, the user might want to add *project context* even if nothing selected?
                    // Usually "Add: Selection" implies specific selection.
                    // If empty, maybe hide.
                    // If "CLEARED" message was sent, we might receive empty selection array?
                    // The tools send JSON even if empty?
                    // m_schJson handles global context.
                    // Let's hide pill if no items selected.
                    m_selectionPill->Show( false );
                }
            }
            catch( ... )
            {
                // Parse error
            }
        }
        else if( payload == "SELECTION\nSCH\nCLEARED" )
        {
            // Clear SCH selection specifically?
            // Actually, new tools send JSON with empty selection array instead of this?
            // I updated tools to send JSON *if !empty()*.
            // Wait, I updated tools: "if( m_selection.Empty() ) { ... SELECTION\nSCH\nCLEARED ... }"
            // So I still need to handle legacy clear messages?
            // Yes.
            if( payload.find( "SCH" ) != std::string::npos )
            {
                // Clear SCH JSON? Or just update it to empty selection?
                // If I clear m_schJson, I lose the project components list!
                // I should parsing logic update selection to empty.
                // But the tool doesn't send JSON if empty.
                // So "CLEARED" means "Empty Selection".
                // Use legacy clear to hide pill.
                // And ideally, I should keep the LAST known project components?
                // m_schJson remains valid for components, but selection is invalid.
                // Implementation detail: I might need to update m_schJson to set "selection": []?
                // Too complex to parse/edit JSON string.
                // I'll leave m_schJson as is (it contains last Valid selection).
                // But the prompt will include it.
                // This is a specialized behavior: "If CLEARED, don't use selection from JSON".
                // Maybe I need `m_schJsonSelectionValid` flag.
                // Or, I should update the tools to SEND JSON even on Clear (with empty selection).
                // That would be cleaner.
                // But for now, I'll just hide the pill.
                m_selectionPill->Show( false );
            }
            else if( payload.find( "PCB" ) != std::string::npos )
            {
                m_selectionPill->Show( false );
            }
        }
    }
    else if( aEvent.Command() == MAIL_AUTH_STATE_CHANGED )
    {
        // Reload session from keychain (tokens were saved by launcher's SESSION_MANAGER)
        if( m_auth )
        {
            m_auth->LoadSession();
            UpdateAuthUI();
        }

        // If sign-in was from agent frame, bring it to front (after UI is updated)
        std::string payload = aEvent.GetPayload();
        if( payload.find( "source=agent" ) != std::string::npos )
        {
            if( IsIconized() )
                Iconize( false );
            Show( true );
            Raise();
            RequestUserAttention();
        }
    }
    else if( aEvent.Command() == MAIL_AGENT_DIFF_CLEARED )
    {
        // Diff overlay was dismissed in editor - clear the corresponding approval buttons
        std::string payload = aEvent.GetPayload();
        bool isSchematic = ( payload == "sch" );
        ClearApprovalButtons( isSchematic );
    }
    Layout();
}

void AGENT_FRAME::OnSelectionPillClick( wxCommandEvent& aEvent )
{
    wxString label = m_selectionPill->GetLabel();
    if( !label.IsEmpty() )
    {
        // Strip "Add: " prefix if present for the tag
        if( label.StartsWith( "Add: " ) )
            label = label.Mid( 5 );

        // Hide pill on click
        m_selectionPill->Hide();
        Layout(); // specific layout for input panel/frame?
        // Layout(); // Calling global Layout() might be heavy, but safest.

        // Append @{Label} to input
        wxString currentText = m_inputCtrl->GetValue();
        if( !currentText.IsEmpty() && !currentText.EndsWith( " " ) )
            m_inputCtrl->AppendText( " " );

        // Insert text (formatted by OnInputText)
        m_inputCtrl->AppendText( "@{" + label + "} " );
        m_inputCtrl->SetFocus();
    }
}

void AGENT_FRAME::OnInputText( wxCommandEvent& aEvent )
{
    // Dynamic Highlighting: Bold valid @{...} tags
    long     currentPos = m_inputCtrl->GetInsertionPoint();
    wxString text = m_inputCtrl->GetValue();

    // Get the control's font to preserve it when setting styles
    wxFont baseFont = m_inputCtrl->GetFont();

    // 1. Reset all to normal (must include full font to preserve size)
    wxTextAttr normalStyle;
    wxFont normalFont = baseFont;
    normalFont.SetWeight( wxFONTWEIGHT_NORMAL );
    normalStyle.SetFont( normalFont );
    m_inputCtrl->SetStyle( 0, text.Length(), normalStyle );

    // 2. Scan for @{...} pairs
    size_t start = 0;
    while( ( start = text.find( "@{", start ) ) != wxString::npos )
    {
        size_t end = text.find( "}", start );
        if( end != wxString::npos )
        {
            // Apply Bold to @{...} (must include full font to preserve size)
            wxTextAttr boldStyle;
            wxFont boldFont = baseFont;
            boldFont.SetWeight( wxFONTWEIGHT_BOLD );
            boldStyle.SetFont( boldFont );
            m_inputCtrl->SetStyle( start, end + 1, boldStyle );
            start = end + 1;
        }
        else
        {
            break; // No more closed tags
        }
    }
}

void AGENT_FRAME::OnInputKeyDown( wxKeyEvent& aEvent )
{
    int key = aEvent.GetKeyCode();

    if( key == WXK_BACK || key == WXK_DELETE )
    {
        long     pos = m_inputCtrl->GetInsertionPoint();
        wxString text = m_inputCtrl->GetValue();

        if( pos > 0 )
        {
            // Atomic deletion for @{...}
            // Check if we are deleting a '}'
            if( pos <= text.Length() && text[pos - 1] == '}' )
            {
                // Verify matching @{
                long openBrace = text.rfind( "@{", pos - 1 );
                if( openBrace != wxString::npos )
                {
                    // Ensure no other '}' in between (simple nesting check)
                    wxString content = text.SubString( openBrace, pos - 1 );
                    // content is like @{tag}
                    if( content.find( '}' ) == content.Length() - 1 ) // Last char is the only closing brace
                    {
                        m_inputCtrl->Remove( openBrace, pos );
                        return;
                    }
                }
            }
        }
    }
    aEvent.Skip(); // Default processing
}

void AGENT_FRAME::OnSend( wxCommandEvent& aEvent )
{
    // NOTE: This method still uses legacy code because it handles KiCad-specific requirements
    // (authentication, pending editor state, system prompt with schematic/PCB context, KIWAY
    // target sheet reset) that the controller doesn't currently support.
    // Future refactoring may add SetSystemPrompt() and other methods to the controller.

    // If already processing, this button acts as Stop
    if( m_actionButton->GetLabel() == "Stop" )
    {
        OnStop( aEvent );
        return;
    }

    // Check authentication first
    if( !CheckAuthentication() )
    {
        AppendHtml( "<p><i>Please sign in to continue.</i></p>" );
        return;
    }

    // Auto-reject pending open editor request if user sends a new message.
    // Use Cancel() which handles orphaned tools and transitions to IDLE.
    // Don't use HandleToolResult() as it would call ContinueChat() and start an LLM request,
    // but OnSend will also start a request → we'd get duplicate requests.
    if( m_pendingOpenSch || m_pendingOpenPcb )
    {
        if( m_chatController )
            m_chatController->Cancel();

        m_pendingOpenSch = false;
        m_pendingOpenPcb = false;
        m_pendingOpenToolId.clear();
        m_toolCallHtml = "";
    }

    wxString text = m_inputCtrl->GetValue();
    if( text.IsEmpty() )
        return;

    // Display User Message (right-aligned speech bubble style)
    wxString escapedText = text;
    escapedText.Replace( "&", "&amp;" );
    escapedText.Replace( "<", "&lt;" );
    escapedText.Replace( ">", "&gt;" );
    escapedText.Replace( "\n", "<br>" );
    wxString msgHtml = wxString::Format(
        "<table width='100%%' cellpadding='0'><tr><td align='right'>"
        "<table bgcolor='#3d3d3d' cellpadding='10'><tr><td>"
        "<font color='#ffffff'>%s</font>"
        "</td></tr></table>"
        "</td></tr></table><br><br>",
        escapedText );
    AppendHtml( msgHtml );

    // Save HTML snapshot for markdown re-rendering during streaming
    m_htmlBeforeAgentResponse = m_fullHtmlContent;

    fprintf( stderr, "[HTML-DEBUG] OnSend: Captured m_htmlBeforeAgentResponse, length=%zu, ends with </body></html>: %s\n",
             (size_t)m_htmlBeforeAgentResponse.length(),
             m_htmlBeforeAgentResponse.EndsWith( "</body></html>" ) ? "YES" : "NO" );
    fflush( stderr );

    // Clear Input and Update UI
    m_inputCtrl->Clear();
    m_actionButton->SetLabel( "Stop" );

    // System prompt is now handled server-side

    // Configure controller for this request
    if( m_chatController )
    {
        // Sync frame's history to controller before repair
        // (controller may be out of sync if conversation was loaded from disk)
        m_chatController->SetHistory( m_chatHistory );

        // Repair orphaned tool_use/tool_result blocks
        m_chatController->RepairHistory();

        // Sync repaired history back to frame for rendering/persistence
        m_chatHistory = m_chatController->GetChatHistory();
        m_apiContext = m_chatController->GetApiContext();
    }

    // Capture first user message for title generation
    if( m_needsTitleGeneration && m_firstUserMessage.empty() )
    {
        m_firstUserMessage = text.ToStdString();
        fprintf( stderr, "[TITLE-DEBUG] Captured first user message: '%s'\n", m_firstUserMessage.c_str() );
    }

    // Reset frame streaming state
    m_currentResponse = "";
    m_toolCallHtml = "";
    m_thinkingHtml = "";
    m_thinkingContent = "";
    m_thinkingExpanded = false;
    m_isThinking = false;
    m_pendingToolCalls = nlohmann::json::array();
    m_stopRequested = false;
    m_userScrolledUp = false;

    // Transition frame's state machine (legacy - controller also has state machine)
    m_conversationCtx.Reset();
    m_conversationCtx.TransitionTo( AgentConversationState::WAITING_FOR_LLM );

    // Configure LLM client with selected model
    wxString modelNameProp = m_modelChoice->GetStringSelection();
    m_llmClient->SetModel( modelNameProp.ToStdString() );

    // Reset target sheet for new conversation turn
    std::string emptyPayload;
    Kiway().ExpressMail( FRAME_SCH, MAIL_AGENT_RESET_TARGET_SHEET, emptyPayload );

    // Send message via controller (handles history, starts LLM request)
    if( m_chatController )
    {
        m_chatController->SendMessage( text.ToStdString() );

        // Sync controller's history back to frame for rendering and persistence
        m_chatHistory = m_chatController->GetChatHistory();
        m_apiContext = m_chatController->GetApiContext();
        m_chatHistoryDb.Save( m_chatHistory );
    }
    else
    {
        // Fallback: legacy path if no controller (shouldn't happen)
        nlohmann::json userMsg = { { "role", "user" }, { "content", text.ToStdString() } };
        m_chatHistory.push_back( userMsg );
        m_apiContext.push_back( userMsg );
        m_chatHistoryDb.Save( m_chatHistory );
        StartAsyncLLMRequest();
    }
}

void AGENT_FRAME::OnStop( wxCommandEvent& aEvent )
{
    using json = nlohmann::json;

    // Delegate cancel logic to controller
    if( m_chatController )
        m_chatController->Cancel();

    // Stop generating animation
    StopGeneratingAnimation();

    // Handle old worker thread approach (for backwards compatibility)
    if( m_workerThread )
    {
        m_workerThread->Delete(); // soft delete, checks TestDestroy()
        m_workerThread = nullptr;
    }

    // Cancel any in-progress async LLM request (legacy - controller also does this)
    if( m_llmClient && m_llmClient->IsRequestInProgress() )
    {
        m_llmClient->CancelRequest();
    }

    // Signal to stop - affects tool execution loops and streaming callbacks (legacy)
    m_stopRequested = true;

    // Re-render without dots
    UpdateAgentResponse();

    // Sync frame's history from controller (controller handles orphaned tool_use blocks)
    if( m_chatController )
    {
        m_chatHistory = m_chatController->GetChatHistory();
        m_apiContext = m_chatController->GetApiContext();
    }

    // Clear uncommitted tool calls (haven't been added to history yet)
    if( m_pendingToolCalls.is_array() && !m_pendingToolCalls.empty() )
    {
        m_pendingToolCalls = json::array();
    }

    // Transition state machine to IDLE
    m_conversationCtx.TransitionTo( AgentConversationState::IDLE );

    AppendHtml( "</p><p><i>(Stopped)</i></p>" );
    m_actionButton->SetLabel( "Send" );
}

void AGENT_FRAME::OnAgentUpdate( wxCommandEvent& aEvent )
{
    wxString content = aEvent.GetString();

    // Accumulate RAW text for parsing
    m_currentResponse += content;

    // Re-render full response with markdown formatting
    UpdateAgentResponse();

    // Auto-scroll (respects user scroll position)
    AutoScrollToBottom();

    // Check for TOOL_CALL to force stop
    size_t toolPos = m_currentResponse.rfind( "TOOL_CALL:" );
    if( toolPos != std::string::npos )
    {
        // Check if we have the full line (newline after TOOL_CALL)
        size_t lineEnd = m_currentResponse.find( '\n', toolPos );
        if( lineEnd != std::string::npos )
        {
            // NEW: Check for code block starter "```" after tool call
            // If present, we must wait for the CLOSING "```" before stopping.
            size_t codeStart = m_currentResponse.find( "```", toolPos );
            bool   shouldStop = true;

            if( codeStart != std::string::npos )
            {
                // We have a code block. Check if it is closed.
                size_t codeEnd = m_currentResponse.find( "```", codeStart + 3 );
                if( codeEnd == std::string::npos )
                {
                    // Code block is OPEN. Continue generating.
                    shouldStop = false;
                }
                // Else: Code block is CLOSED. We can stop.
            }

            if( shouldStop )
            {
                // Complete TOOL_CALL detected. Stop generation immediately.
                if( m_workerThread )
                {
                    // Request thread to exit. OnAgentComplete will be called naturally.
                    m_workerThread->Delete();
                    m_workerThread = nullptr;
                    m_chatWindow->AppendToPage( "<p><i>(Tool Call Detected - Stopping Generation)</i></p>" );
                }
            }
        }
    }
}

void AGENT_FRAME::OnAgentComplete( wxCommandEvent& aEvent )
{
    // Stop generating animation
    StopGeneratingAnimation();

    // Thread has finished naturally
    if( m_workerThread )
    {
        m_workerThread->Wait(); // Join
        delete m_workerThread;
        m_workerThread = nullptr;
    }

    // Re-render without dots
    UpdateAgentResponse();
    m_actionButton->SetLabel( "Send" );

    // Add Assistant response to history
    if( !m_currentResponse.empty() || !m_thinkingContent.IsEmpty() )
    {
        nlohmann::json assistantMsg;
        assistantMsg["role"] = "assistant";

        if( !m_thinkingContent.IsEmpty() )
        {
            nlohmann::json content = nlohmann::json::array();
            content.push_back( { { "type", "thinking" }, { "thinking", m_thinkingContent.ToStdString() } } );
            if( !m_currentResponse.empty() )
                content.push_back( { { "type", "text" }, { "text", m_currentResponse } } );
            assistantMsg["content"] = content;
        }
        else
        {
            assistantMsg["content"] = m_currentResponse;
        }

        m_chatHistory.push_back( assistantMsg );
        m_apiContext.push_back( assistantMsg );
        m_chatHistoryDb.Save( m_chatHistory );
    }

    if( aEvent.GetInt() == 0 ) // Failure
    {
        AppendHtml( "<p><i>(Error generating response)</i></p>" );
    }
    else
    {
        // Parse for Tool Calls
        size_t toolPos = m_currentResponse.rfind( "TOOL_CALL: " );
        if( toolPos != std::string::npos )
        {
            size_t start = toolPos + 11;
            // Stop at end of string (since we force stopped)
            size_t end = m_currentResponse.length();

            std::string toolName = m_currentResponse.substr( start, end - start );

            // Clean up Markdown Code Blocks if present
            // Agent might output: run_terminal_command pcb ``` print(1) ```
            // We want to verify if it contains a code block and strip the fences.

            size_t fenceStart = toolName.find( "```" );
            if( fenceStart != std::string::npos )
            {
                // Strip opening fence
                // Also strip language identifier if present (e.g. ```python)
                size_t contentStart = toolName.find( '\n', fenceStart );
                if( contentStart == std::string::npos )
                    contentStart = fenceStart + 3; // Fallback if no newline
                else
                    contentStart++; // Skip newline

                // Find closing fence
                size_t fenceEnd = toolName.rfind( "```" );
                if( fenceEnd != std::string::npos && fenceEnd > contentStart )
                {
                    // Extract inside
                    std::string pre = toolName.substr( 0, fenceStart );
                    std::string core = toolName.substr( contentStart, fenceEnd - contentStart );

                    // Combine: "run_terminal_command pcb " + "print(1)"
                    // We need to ensure spaces.
                    toolName = pre + " " + core;
                }
            }

            // Normal whitespace cleaning
            // Replace newlines with spaces?
            // NO. PCB/SCH commands (python) might need newlines.
            // But 'sys' commands usually don't.
            // `run_terminal_command` expects [mode] [cmd].
            // If cmd is python code with newlines, we should preserve them?
            // Existing logic `ExecuteCommandForAgent` splits by space... wait.
            // If existing `ExecuteCommandForAgent` splits by space, multi-line python will break.
            // I need to check `ExecuteCommandForAgent` logic later.
            // For now, let's just trim outer whitespace.

            toolName.erase( 0, toolName.find_first_not_of( " \t\r\n" ) );
            toolName.erase( toolName.find_last_not_of( " \t\r\n" ) + 1 );
            // Clean whitespace and newlines
            toolName.erase( 0, toolName.find_first_not_of( " \t\r\n" ) );
            toolName.erase( toolName.find_last_not_of( " \t\r\n" ) + 1 );

            m_pendingTool = toolName;

            // Show Inline Approve Link with Colors
            std::string html = "<p><b>Tool Request:</b> " + toolName
                               + " <a href=\"tool:approve\" style=\"color: #00AA00; font-weight: bold;\">[Approve]</a>"
                               + " <a href=\"tool:deny\" style=\"color: #AA0000; font-weight: bold;\">[Deny]</a></p>";
            AppendHtml( html );

            // Allow user to click. Execution halted until link clicked.
            Layout();
        }
    }
}

void AGENT_FRAME::OnHtmlLinkClick( wxHtmlLinkEvent& aEvent )
{
    wxString href = aEvent.GetLinkInfo().GetHref();

    if( href == "tool:approve" )
    {
        OnToolClick( aEvent );
    }
    else if( href == "tool:reject" )
    {
        AppendHtml( "<p><i>Tool call rejected by user.</i></p>" );
        nlohmann::json rejectMsg = { { "role", "user" }, { "content", "Tool execution rejected." } };
        m_chatHistory.push_back( rejectMsg );
        m_apiContext.push_back( rejectMsg );
        // Optionally resume generation or wait for user input?
        // Usually better to let user type why.
    }
    else if( href == "agent:approve_open" )
    {
        OnApproveOpenEditor();
    }
    else if( href == "agent:reject_open" )
    {
        OnRejectOpenEditor();
    }
    else if( href.StartsWith( "toggle:thinking:" ) )
    {
        // Toggle thinking block by index
        wxString indexStr = href.Mid( 16 );  // "toggle:thinking:" is 16 chars
        long index;

        if( indexStr.ToLong( &index ) && index >= 0 )
        {
            // Check if this is the current streaming thinking
            if( index == m_currentThinkingIndex && m_currentThinkingIndex >= 0 &&
                !m_thinkingContent.IsEmpty() )
            {
                // Toggle current streaming thinking
                m_thinkingExpanded = !m_thinkingExpanded;
                RebuildThinkingHtml();
                UpdateAgentResponse();
            }
            else if( index < (int)m_historicalThinking.size() )
            {
                // Toggle historical thinking
                if( m_historicalThinkingExpanded.count( index ) )
                    m_historicalThinkingExpanded.erase( index );
                else
                    m_historicalThinkingExpanded.insert( index );

                // Re-render the chat history with new toggle state
                RenderChatHistory();
            }
        }
    }
    else
    {
        // Open standard links in browser?
        wxLaunchDefaultBrowser( href );
    }
}

void AGENT_FRAME::OnToolClick( wxCommandEvent& aEvent )
{
    if( m_pendingTool.empty() )
        return;

    // m_toolButton->Hide();
    // m_toolButton->GetParent()->Layout();

    // Show "Running..." terminal box
    wxString placeholderId = wxString::Format( "term_%lu", wxGetLocalTimeMillis().GetValue() );
    wxString runningBox = wxString::Format( "<table width='100%%' bgcolor='#1e1e1e' cellpadding='10'><tr><td>"
                                            "<font color='#4ec9b0' face='Courier New' size='2'>&gt; %s</font><br>"
                                            "<font color='#d4d4d4' face='Courier New' size='2'><i>Running...</i></font>"
                                            "</td></tr></table><!--%s-->", // Comment mark for replacement
                                            m_pendingTool, placeholderId );
    AppendHtml( runningBox );

    // Force draw
    wxYield();

    // Execute
    std::string result = SendRequest( FRAME_AGENT, m_pendingTool ); // Assuming internal dispatch handles tool names
    // Actually, SendRequest determines destination based on IDs?
    // My previous code in implementations dispatch used strings "GET_BOARD_INFO" etc.
    // I need to map "get_board_info" to proper Kiway Request or just pass string.
    // Agent -> PCB/SCH dispatch uses SendRequest( int aDest, ... ).
    // I need to determine Dest!
    int dest = FRAME_T::FRAME_PCB_EDITOR; // Default to PCB?
    // Map tool name to command
    std::string toolToRun = m_pendingTool; // Full command string including args

    // Parse command name (first word) to determine destination
    std::stringstream ss( toolToRun );
    std::string       commandName;
    ss >> commandName;

    if( commandName == "run_terminal_command" )
    {
        dest = FRAME_TERMINAL;
    }
    else if( commandName == "get_pcb_components" || commandName == "get_component_details"
             || commandName == "get_pcb_nets" || commandName == "get_net_details" || commandName == "get_board_info" )
    {
        dest = FRAME_PCB_EDITOR;
    }
    else if( commandName == "get_sch_sheets" || commandName == "get_sch_components"
             || commandName == "get_sch_symbol_details" || commandName == "get_connection_graph" )
    {
        dest = FRAME_SCH;
    }
    else
    {
        nlohmann::json errorMsg = { { "role", "user" }, { "content", "Error: Unknown tool command '" + commandName + "'" } };
        m_chatHistory.push_back( errorMsg );
        m_apiContext.push_back( errorMsg );
        m_pendingTool = "";
        // RenderChat(); // Assuming RenderChat is a function that updates the UI based on m_chatHistory
        return;
    }

    // UPDATE UI immediately to show processing state (buttons gone)
    m_pendingTool = "";
    // RenderChat(); // Assuming RenderChat is a function that updates the UI based on m_chatHistory

    // Force UI refresh
    wxYield();

    // Pass the FULL string (e.g. "get_component_details R1") as the payload
    std::string toolOutput = SendRequest( dest, toolToRun );

    // Append Tool Output to History as User message (or System)?
    // "Tool Output: [JSON]"
    std::string toolMsg = "Tool Output (" + toolToRun + "):\n" + toolOutput;
    nlohmann::json toolMsgJson = { { "role", "user" }, { "content", toolMsg } };
    m_chatHistory.push_back( toolMsgJson );
    m_apiContext.push_back( toolMsgJson );

    // Styled Terminal Execution Box (Result)
    wxString htmlOutput = toolOutput;
    htmlOutput.Replace( "\n", "<br>" );
    htmlOutput.Replace( " ", "&nbsp;" );

    wxString finalTermBox = wxString::Format( "<table width='100%%' bgcolor='#1e1e1e' cellpadding='10'><tr><td>"
                                              "<font color='#4ec9b0' face='Courier New' size='2'>&gt; %s</font><br>"
                                              "<font color='#d4d4d4' face='Courier New' size='2'>%s</font>"
                                              "</td></tr></table>",
                                              toolToRun, htmlOutput );

    // REPLACE the running box in m_fullHtmlContent
    // We look for runningBox string.
    m_fullHtmlContent.Replace( runningBox, finalTermBox );
    SetHtml( m_fullHtmlContent );

    // Save HTML snapshot for markdown re-rendering during streaming
    m_currentResponse = "";
    m_htmlBeforeAgentResponse = m_fullHtmlContent;

    // Start generating animation
    StartGeneratingAnimation();

    // System prompt now handled server-side

    std::string payload;
    if( !m_schJson.empty() )
        payload += m_schJson + "\n";
    if( !m_pcbJson.empty() )
        payload += m_pcbJson + "\n";

    wxString model = m_modelChoice->GetStringSelection();
    m_actionButton->SetLabel( "Stop" );

    m_workerThread = new AGENT_THREAD( this, m_chatHistory, payload, model.ToStdString() );
    if( m_workerThread->Run() != wxTHREAD_NO_ERROR )
    {
        wxLogMessage( "Error creating thread" );
        m_actionButton->SetLabel( "Send" );
    }
}

void AGENT_FRAME::OnTextEnter( wxCommandEvent& aEvent )
{
    if( wxGetKeyState( WXK_SHIFT ) )
    {
        m_inputCtrl->WriteText( "\n" );
    }
    else
    {
        OnSend( aEvent );
    }
}

void AGENT_FRAME::OnModelSelection( wxCommandEvent& aEvent )
{
    // Reload model context when model changes
    // Track the currently selected model
    wxString newModel = m_modelChoice->GetStringSelection();
    if( newModel.ToStdString() != m_currentModel )
    {
        m_currentModel = newModel.ToStdString();

        // Delegate to controller
        if( m_chatController )
            m_chatController->SetModel( m_currentModel );
    }
}

void AGENT_FRAME::OnExit( wxCommandEvent& event )
{
    Close( true );
}

std::string AGENT_FRAME::SendRequest( int aDestFrame, const std::string& aPayload )
{
    m_toolResponse = "";
    std::string payloadCopy = aPayload;
    Kiway().ExpressMail( static_cast<FRAME_T>( aDestFrame ), MAIL_AGENT_REQUEST, payloadCopy );

    // Wait for response (Sync)
    // We expect the target frame to reply via MAIL_AGENT_RESPONSE which sets m_toolResponse
    wxLongLong start = wxGetLocalTimeMillis();
    m_stopRequested = false;                                                      // Reset stop flag
    while( m_toolResponse.empty() && ( wxGetLocalTimeMillis() - start < 10000 ) ) // 10s timeout
    {
        wxYield(); // Process events (including the MailIn event and Stop button)
        if( m_stopRequested )
        {
            return "Error: Tool execution cancelled by user.";
        }
        wxMilliSleep( 10 );
    }

    if( m_toolResponse.empty() )
    {
        return "Error: Tool execution timed out or returned empty response.";
    }

    return m_toolResponse;
}

void AGENT_FRAME::OnChatRightClick( wxMouseEvent& aEvent )
{
    wxString selection = m_chatWindow->SelectionToText();
    if( !selection.IsEmpty() )
    {
        wxMenu menu;
        menu.Append( ID_CHAT_COPY, "Copy" );
        menu.Enable( ID_CHAT_COPY, true );
        PopupMenu( &menu );
    }
}

void AGENT_FRAME::OnPopupClick( wxCommandEvent& aEvent )
{
    if( aEvent.GetId() == ID_CHAT_COPY )
    {
        wxString selection = m_chatWindow->SelectionToText();
        if( !selection.IsEmpty() )
        {
            if( wxTheClipboard->Open() )
            {
                wxTheClipboard->SetData( new wxTextDataObject( selection ) );
                wxTheClipboard->Close();
            }
        }
    }
}

// ============================================================================
// Native Tool Calling Implementation
// ============================================================================

void AGENT_FRAME::InitializeTools()
{
    m_tools = AgentTools::GetToolDefinitions();
}

std::string AGENT_FRAME::ExecuteTool( const std::string& aName, const nlohmann::json& aInput )
{
    // Delegate to AgentTools with a callback to SendRequest
    return AgentTools::ExecuteToolSync( aName, aInput,
        [this]( int aDest, const std::string& aPayload ) {
            return SendRequest( aDest, aPayload );
        } );
}

// NOTE: HandleLLMEvent and ContinueConversation were removed in Phase 5.3
// They were dead code - only used in the synchronous path which is no longer called.
// All LLM streaming now uses the async path: StartAsyncLLMRequest -> OnLLMStream* events

// ============================================================================
// Async LLM Streaming Event Handlers
// ============================================================================

void AGENT_FRAME::StartAsyncLLMRequest()
{
    // Start the generating animation
    StartGeneratingAnimation();

    wxString model = m_modelChoice->GetStringSelection();
    m_llmClient->SetModel( model.ToStdString() );

    // System prompt now handled server-side

    // Filter out thinking blocks from API context before sending to API
    // (Anthropic requires signatures for thinking blocks, which we don't store)
    // Note: m_apiContext may be compacted after context recovery
    nlohmann::json filteredHistory = nlohmann::json::array();
    for( const auto& msg : m_apiContext )
    {
        if( msg.contains( "content" ) && msg["content"].is_array() )
        {
            // Filter content array to remove thinking blocks
            nlohmann::json filteredContent = nlohmann::json::array();
            for( const auto& block : msg["content"] )
            {
                if( !block.contains( "type" ) || block["type"] != "thinking" )
                {
                    filteredContent.push_back( block );
                }
            }

            // Only add message if it has non-empty content after filtering
            if( !filteredContent.empty() )
            {
                nlohmann::json filteredMsg = msg;
                filteredMsg["content"] = filteredContent;
                filteredHistory.push_back( filteredMsg );
            }
        }
        else
        {
            // String content or other format - pass through as-is
            filteredHistory.push_back( msg );
        }
    }

    // Start async request - returns immediately
    if( !m_llmClient->AskStreamWithToolsAsync( filteredHistory, m_tools, this ) )
    {
        wxLogDebug( "AGENT: Failed to start async LLM request" );
        StopGeneratingAnimation();
        AppendHtml( "<p><font color='red'>Error: Failed to start LLM request</font></p>" );
        m_conversationCtx.TransitionTo( AgentConversationState::IDLE );
        m_actionButton->SetLabel( "Send" );
    }
}


void AGENT_FRAME::RetryLastRequest()
{
    // Reset streaming state for retry
    m_currentResponse.clear();
    m_thinkingContent.Clear();
    m_thinkingHtml.Clear();
    m_toolCallHtml.Clear();
    m_thinkingExpanded = false;
    m_isThinking = false;
    m_pendingToolCalls = nlohmann::json::array();

    // Ensure we're in the right state
    m_conversationCtx.Reset();
    m_conversationCtx.TransitionTo( AgentConversationState::WAITING_FOR_LLM );

    // Start the async LLM request (uses m_apiContext which has been compacted)
    StartAsyncLLMRequest();
}


void AGENT_FRAME::OnLLMStreamChunk( wxThreadEvent& aEvent )
{
    // Get the chunk data from the event payload
    LLMStreamChunk* chunk = aEvent.GetPayload<LLMStreamChunk*>();
    if( !chunk )
        return;

    // Forward to controller for processing
    // Controller emits EVT_CHAT_* events which are handled by OnChat* methods
    if( m_chatController )
    {
        m_chatController->HandleLLMChunk( *chunk );
    }

    // Clean up
    delete chunk;
}

// NOTE: HandleLLMChunk was removed in Phase 5.3c
// All LLM chunk processing now goes through the controller which emits EVT_CHAT_* events


void AGENT_FRAME::OnLLMStreamComplete( wxThreadEvent& aEvent )
{
    wxLogDebug( "AGENT: OnLLMStreamComplete called" );

    // Forward to controller - it will emit appropriate EVT_CHAT_* events
    // UI updates are handled by OnChatTurnComplete, OnChatError, etc.
    if( m_chatController )
    {
        m_chatController->HandleLLMComplete();
    }

    // Clean up payload
    LLMStreamComplete* complete = aEvent.GetPayload<LLMStreamComplete*>();
    if( complete )
    {
        delete complete;
    }
}


void AGENT_FRAME::OnLLMStreamError( wxThreadEvent& aEvent )
{
    wxLogDebug( "AGENT: OnLLMStreamError called" );

    // Get error data
    LLMStreamComplete* complete = aEvent.GetPayload<LLMStreamComplete*>();
    std::string errorMessage = complete ? complete->error_message : "Unknown error";

    // Forward to controller - it will emit EVT_CHAT_ERROR
    // UI updates are handled by OnChatError
    if( m_chatController )
    {
        m_chatController->HandleLLMError( errorMessage );
    }

    // Clean up payload
    if( complete )
    {
        delete complete;
    }
}

void AGENT_FRAME::OnNewChat( wxCommandEvent& aEvent )
{
    // Prevent switching chats while generating
    bool isBusy = m_chatController ? m_chatController->IsBusy()
                                   : ( m_isGenerating || m_conversationCtx.GetState() != AgentConversationState::IDLE );
    if( isBusy )
    {
        wxMessageBox( _( "Please wait for the current response to complete before starting a new chat." ),
                      _( "Chat in Progress" ), wxOK | wxICON_INFORMATION );
        return;
    }

    // Delegate chat state reset to controller
    if( m_chatController )
        m_chatController->NewChat();

    // Clear current chat and start fresh (legacy - will be removed in Phase 5.4)
    m_chatHistory = nlohmann::json::array();
    m_apiContext = nlohmann::json::array();

    // Add welcome message as an assistant message
    m_chatHistory.push_back( {
        { "role", "assistant" },
        { "content", "Welcome to KiCad Agent." }
    } );

    // UI reset
    m_fullHtmlContent = "<html><body bgcolor='#1E1E1E' text='#FFFFFF'><p>Welcome to KiCad Agent.</p></body></html>";
    SetHtml( m_fullHtmlContent );
    m_chatHistoryDb.StartNewConversation();
    m_chatNameLabel->SetLabel( "New Chat" );

    // Reset title generation state (legacy - controller handles this)
    m_needsTitleGeneration = true;
    m_firstUserMessage = "";

    // Clear historical thinking state
    m_historicalThinking.clear();
    m_historicalThinkingExpanded.clear();
    m_currentThinkingIndex = -1;
}


void AGENT_FRAME::OnHistoryTool( wxCommandEvent& aEvent )
{
    auto historyList = m_chatHistoryDb.GetHistoryList();

    wxMenu menu;

    if( !historyList.empty() )
    {
        int id = ID_CHAT_HISTORY_MENU_BASE;

        // Show only last 5 entries in the dropdown
        size_t count = 0;
        for( const auto& entry : historyList )
        {
            if( count++ >= 5 ) break;
            wxString title = wxString::FromUTF8( entry.title );
            if( title.IsEmpty() )
                title = "Untitled Chat";
            menu.Append( id++, title );
        }

        Bind( wxEVT_MENU, &AGENT_FRAME::OnHistoryMenuSelect, this, ID_CHAT_HISTORY_MENU_BASE, id - 1 );

        // Add separator and "Show All" if there are more than 5 entries
        if( historyList.size() > 5 )
        {
            menu.AppendSeparator();
            menu.Append( ID_CHAT_HISTORY_SHOW_ALL, "Show All..." );
            Bind( wxEVT_MENU, &AGENT_FRAME::OnHistoryShowAll, this, ID_CHAT_HISTORY_SHOW_ALL );
        }
    }
    else
    {
        menu.Append( wxID_ANY, "(No history)" )->Enable( false );
    }

    PopupMenu( &menu );
}


void AGENT_FRAME::OnHistoryShowAll( wxCommandEvent& aEvent )
{
    // Prevent switching chats while generating
    if( m_isGenerating || m_conversationCtx.GetState() != AgentConversationState::IDLE )
    {
        wxMessageBox( _( "Please wait for the current response to complete before viewing history." ),
                      _( "Chat in Progress" ), wxOK | wxICON_INFORMATION );
        return;
    }

    // Position history panel as full overlay covering entire client area
    wxSize clientSize = GetClientSize();
    m_historyPanel->SetSize( clientSize );
    m_historyPanel->SetPosition( wxPoint( 0, 0 ) );

    // Refresh and show the history panel
    m_historyPanel->RefreshHistory();
    m_historyPanel->Show();
    m_historyPanel->Raise();  // Bring to front
}

void AGENT_FRAME::OnHistoryMenuSelect( wxCommandEvent& aEvent )
{
    // Prevent switching chats while generating
    if( m_isGenerating || m_conversationCtx.GetState() != AgentConversationState::IDLE )
    {
        wxMessageBox( _( "Please wait for the current response to complete before switching chats." ),
                      _( "Chat in Progress" ), wxOK | wxICON_INFORMATION );
        return;
    }

    int index = aEvent.GetId() - ID_CHAT_HISTORY_MENU_BASE;
    auto historyList = m_chatHistoryDb.GetHistoryList();

    if( index >= 0 && index < (int)historyList.size() )
    {
        LoadConversation( historyList[index].id );
    }
}

void AGENT_FRAME::LoadConversation( const std::string& aConversationId )
{
    // Delegate to controller - it will emit EVT_CHAT_HISTORY_LOADED
    // which triggers OnChatHistoryLoaded for UI updates
    if( m_chatController )
    {
        m_chatController->LoadChat( aConversationId );
    }
}


void AGENT_FRAME::RenderChatHistory()
{
    // Clear historical thinking storage
    m_historicalThinking.clear();

    // Build HTML from chat history
    m_fullHtmlContent = "<html><body bgcolor='#1E1E1E' text='#FFFFFF'>";

    for( const auto& msg : m_chatHistory )
    {
        if( !msg.contains( "role" ) || !msg.contains( "content" ) )
            continue;

        std::string role = msg["role"];

        // Content can be string or array (tool use)
        if( msg["content"].is_string() )
        {
            std::string content = msg["content"];
            wxString display = content;

            if( role == "user" )
            {
                // Right-aligned speech bubble style for user messages
                display.Replace( "&", "&amp;" );
                display.Replace( "<", "&lt;" );
                display.Replace( ">", "&gt;" );
                display.Replace( "\n", "<br>" );
                m_fullHtmlContent += wxString::Format(
                    "<table width='100%%' cellpadding='0'><tr><td align='right'>"
                    "<table bgcolor='#3d3d3d' cellpadding='10'><tr><td>"
                    "<font color='#ffffff'>%s</font>"
                    "</td></tr></table>"
                    "</td></tr></table><br><br>",
                    display );
            }
            else if( role == "assistant" )
            {
                // Left-aligned markdown formatted response
                m_fullHtmlContent += AgentMarkdown::ToHtml( content );
                m_fullHtmlContent += "<br>";
            }
        }
        else if( msg["content"].is_array() )
        {
            // Iterate through content blocks and render each one
            for( const auto& block : msg["content"] )
            {
                if( !block.contains( "type" ) )
                    continue;

                std::string blockType = block["type"];

                if( blockType == "text" )
                {
                    // Render text block
                    std::string text = block.value( "text", "" );
                    wxString display = text;

                    if( role == "assistant" )
                    {
                        // Left-aligned markdown formatted response
                        m_fullHtmlContent += AgentMarkdown::ToHtml( display );
                        m_fullHtmlContent += "<br>";
                    }
                    else if( role == "user" )
                    {
                        // Right-aligned speech bubble style for user messages
                        display.Replace( "&", "&amp;" );
                        display.Replace( "<", "&lt;" );
                        display.Replace( ">", "&gt;" );
                        display.Replace( "\n", "<br>" );
                        m_fullHtmlContent += wxString::Format(
                            "<table width='100%%' cellpadding='0'><tr><td align='right'>"
                            "<table bgcolor='#3d3d3d' cellpadding='10'><tr><td>"
                            "<font color='#ffffff'>%s</font>"
                            "</td></tr></table>"
                            "</td></tr></table><br>",
                            display );
                    }
                }
                else if( blockType == "thinking" )
                {
                    // Render thinking block with toggle support
                    std::string thinking = block.value( "thinking", "" );
                    if( !thinking.empty() )
                    {
                        int thinkingIndex = m_historicalThinking.size();
                        wxString thinkingText = wxString::FromUTF8( thinking );

                        // Store the raw thinking content for later toggle
                        m_historicalThinking.push_back( thinkingText );

                        // Check if this thinking block is expanded
                        bool expanded = m_historicalThinkingExpanded.count( thinkingIndex ) > 0;

                        // Generate toggle link with index
                        wxString thinkingLabel = wxString::Format(
                            "<a href='toggle:thinking:%d'><font color='#808080'>Thinking</font></a>",
                            thinkingIndex );

                        if( expanded )
                        {
                            // Escape HTML for display
                            wxString escapedText = thinkingText;
                            escapedText.Replace( "&", "&amp;" );
                            escapedText.Replace( "<", "&lt;" );
                            escapedText.Replace( ">", "&gt;" );
                            escapedText.Replace( "\n", "<br>" );

                            // Truncate if very long
                            if( escapedText.length() > 5000 )
                            {
                                escapedText = escapedText.Left( 5000 ) + "... <i>(truncated)</i>";
                            }

                            m_fullHtmlContent += wxString::Format(
                                "%s<br><font color='#606060'>%s</font><br><br>",
                                thinkingLabel, escapedText );
                        }
                        else
                        {
                            // Collapsed: just show label
                            m_fullHtmlContent += wxString::Format( "%s<br><br>", thinkingLabel );
                        }
                    }
                }
                else if( blockType == "tool_use" )
                {
                    // Render tool_use block with human-readable description
                    std::string toolName = block.value( "name", "unknown" );
                    nlohmann::json toolInput = block.value( "input", nlohmann::json::object() );
                    wxString desc = AgentTools::GetToolDescription( toolName, toolInput );

                    // Store for pairing with result (next block)
                    m_lastToolDesc = desc;
                }
                else if( blockType == "tool_result" )
                {
                    // Render combined tool call + result block
                    std::string content = block.value( "content", "" );
                    bool isError = block.value( "is_error", false );

                    // Check if this is a Python traceback
                    bool isPythonError = ( content.find( "Traceback" ) != std::string::npos );

                    wxString displayResult;
                    wxString statusColor;
                    wxString statusText;

                    if( isPythonError )
                    {
                        statusColor = "#f44747";
                        statusText = "Error";
                        displayResult = "<i>Script execution failed.</i>";
                    }
                    else if( isError )
                    {
                        statusColor = "#f44747";
                        statusText = "Failed";
                        wxString htmlResult = content;
                        htmlResult.Replace( "&", "&amp;" );
                        htmlResult.Replace( "<", "&lt;" );
                        htmlResult.Replace( ">", "&gt;" );
                        if( htmlResult.length() > 200 )
                            htmlResult = htmlResult.Left( 200 ) + "...";
                        displayResult = htmlResult;
                    }
                    else
                    {
                        statusColor = "#4ec9b0";
                        statusText = "Completed";
                        wxString htmlResult = content;
                        htmlResult.Replace( "&", "&amp;" );
                        htmlResult.Replace( "<", "&lt;" );
                        htmlResult.Replace( ">", "&gt;" );
                        if( htmlResult.length() > 500 )
                            htmlResult = htmlResult.Left( 500 ) + "... (truncated)";
                        displayResult = htmlResult;
                    }

                    // Use the stored tool description from the preceding tool_use block
                    wxString desc = m_lastToolDesc.IsEmpty() ? "Tool execution" : m_lastToolDesc;

                    wxString resultBox = wxString::Format(
                        "<br><br><table width='100%%' bgcolor='#2d2d2d' cellpadding='10'><tr><td style='word-wrap:break-word;'>"
                        "<font color='#4ec9b0'><b>Tool Call:</b></font> %s<br>"
                        "<font color='%s'><b>%s</b></font><br>"
                        "<font color='#d4d4d4' size='2'>%s</font>"
                        "</td></tr></table><br>",
                        desc, statusColor, statusText, displayResult );
                    m_fullHtmlContent += resultBox;

                    m_lastToolDesc = "";  // Reset for next tool
                }
            }
        }
    }

    // Include any pending tool call UI (e.g., open editor approval)
    if( !m_toolCallHtml.IsEmpty() )
    {
        m_fullHtmlContent += m_toolCallHtml;
    }

    m_fullHtmlContent += "</body></html>";

    SetHtml( m_fullHtmlContent );
}

// ============================================================================
// Authentication Methods
// ============================================================================

void AGENT_FRAME::UpdateAuthUI()
{
    bool authenticated = m_auth && m_auth->IsAuthenticated();

    if( m_signInOverlay )
    {
        m_signInOverlay->Show( !authenticated );

        if( !authenticated )
        {
            // Position overlay to cover the entire client area
            wxSize clientSize = GetClientSize();
            m_signInOverlay->SetSize( clientSize );
            m_signInOverlay->SetPosition( wxPoint( 0, 0 ) );
            m_signInOverlay->Raise(); // Bring to front
        }

        Layout();
    }
}

bool AGENT_FRAME::CheckAuthentication()
{
    if( m_auth )
    {
        return m_auth->IsAuthenticated();
    }
    return true; // No auth configured, allow access
}

void AGENT_FRAME::OnSignIn( wxCommandEvent& aEvent )
{
    if( m_auth )
        m_auth->StartOAuthFlow( "agent" );
}

void AGENT_FRAME::OnSize( wxSizeEvent& aEvent )
{
    wxSize clientSize = GetClientSize();

    // Reposition overlays to cover entire client area when resized
    if( m_signInOverlay && m_signInOverlay->IsShown() )
    {
        m_signInOverlay->SetSize( clientSize );
        m_signInOverlay->SetPosition( wxPoint( 0, 0 ) );
    }

    if( m_historyPanel && m_historyPanel->IsShown() )
    {
        m_historyPanel->SetSize( clientSize );
        m_historyPanel->SetPosition( wxPoint( 0, 0 ) );
    }

    aEvent.Skip(); // Let default handling continue
}


// ============================================================================
// Agent Change Approval Methods
// ============================================================================

void AGENT_FRAME::CheckForPendingChanges()
{
    m_hasPendingSchChanges = false;
    m_hasPendingPcbChanges = false;

    // Query schematic editor for pending changes via ExpressMail
    KIWAY_PLAYER* schPlayer = Kiway().Player( FRAME_SCH, false );
    if( schPlayer )
    {
        std::string response;
        Kiway().ExpressMail( FRAME_SCH, MAIL_AGENT_HAS_CHANGES, response );

        // Response is JSON with has_changes and sheet_path
        try
        {
            nlohmann::json j = nlohmann::json::parse( response );
            m_hasPendingSchChanges = j.value( "has_changes", false );
            m_pendingSchSheetPath = wxString::FromUTF8( j.value( "sheet_path", "" ) );
        }
        catch( ... )
        {
            // Fallback for legacy format (plain "true"/"false")
            m_hasPendingSchChanges = ( response == "true" );
            m_pendingSchSheetPath = wxEmptyString;
        }
    }

    // Query PCB editor for pending changes via ExpressMail
    KIWAY_PLAYER* pcbPlayer = Kiway().Player( FRAME_PCB_EDITOR, false );
    if( pcbPlayer )
    {
        std::string response;
        Kiway().ExpressMail( FRAME_PCB_EDITOR, MAIL_AGENT_HAS_CHANGES, response );
        m_hasPendingPcbChanges = ( response == "true" );
    }

    if( m_hasPendingSchChanges || m_hasPendingPcbChanges )
        ShowApproveRejectButtons();
}


void AGENT_FRAME::ShowApproveRejectButtons()
{
    // Check if the button was already visible (user may have manually collapsed the panel)
    bool wasAlreadyShowing = m_pendingChangesBtn->IsShown();

    // Always show the indicator button
    m_pendingChangesBtn->Show();

    // Always update the panel content with sheet path information
    m_pendingChangesPanel->UpdateChanges( m_hasPendingSchChanges, m_hasPendingPcbChanges,
                                           m_pendingSchSheetPath, m_pendingPcbFilename );

    // Only auto-show the panel if this is the first time showing changes
    // (respect user's choice if they manually collapsed it)
    if( !wasAlreadyShowing )
    {
        m_pendingChangesBtn->SetLabel( "Hide Changes" );
        m_pendingChangesPanel->Show();
    }

    Layout();
}


void AGENT_FRAME::OnPendingChangesClick( wxCommandEvent& aEvent )
{
    // Toggle panel visibility and update button label
    bool isShown = m_pendingChangesPanel->IsShown();
    m_pendingChangesPanel->Show( !isShown );
    m_pendingChangesBtn->SetLabel( isShown ? "Show Changes" : "Hide Changes" );
    Layout();
}


void AGENT_FRAME::OnSchematicChangeHandled( bool aAccepted )
{
    m_hasPendingSchChanges = false;
    m_pendingSchSheetPath = wxEmptyString;

    // Update panel
    m_pendingChangesPanel->UpdateChanges( m_hasPendingSchChanges, m_hasPendingPcbChanges,
                                           m_pendingSchSheetPath, m_pendingPcbFilename );

    if( !m_hasPendingSchChanges && !m_hasPendingPcbChanges )
    {
        // No more pending changes - hide panel and button
        m_pendingChangesPanel->Hide();
        m_pendingChangesBtn->Hide();
        Layout();
    }

    AppendHtml( aAccepted ? "<p><i>Schematic changes accepted.</i></p>"
                          : "<p><i>Schematic changes rejected.</i></p>" );
}


void AGENT_FRAME::OnPcbChangeHandled( bool aAccepted )
{
    m_hasPendingPcbChanges = false;
    m_pendingPcbFilename = wxEmptyString;

    // Update panel
    m_pendingChangesPanel->UpdateChanges( m_hasPendingSchChanges, m_hasPendingPcbChanges,
                                           m_pendingSchSheetPath, m_pendingPcbFilename );

    if( !m_hasPendingSchChanges && !m_hasPendingPcbChanges )
    {
        // No more pending changes - hide panel and button
        m_pendingChangesPanel->Hide();
        m_pendingChangesBtn->Hide();
        Layout();
    }

    AppendHtml( aAccepted ? "<p><i>PCB changes accepted.</i></p>"
                          : "<p><i>PCB changes rejected.</i></p>" );
}


void AGENT_FRAME::ClearApprovalButtons( bool aIsSchematic )
{
    // Clear the pending changes flag and path for this editor
    if( aIsSchematic )
    {
        m_hasPendingSchChanges = false;
        m_pendingSchSheetPath = wxEmptyString;
    }
    else
    {
        m_hasPendingPcbChanges = false;
        m_pendingPcbFilename = wxEmptyString;
    }

    // Update panel
    m_pendingChangesPanel->UpdateChanges( m_hasPendingSchChanges, m_hasPendingPcbChanges,
                                           m_pendingSchSheetPath, m_pendingPcbFilename );

    if( !m_hasPendingSchChanges && !m_hasPendingPcbChanges )
    {
        // No more pending changes - hide panel and button
        m_pendingChangesPanel->Hide();
        m_pendingChangesBtn->Hide();
        Layout();
        AppendHtml( "<p><i>Changes handled via overlay.</i></p>" );
    }
}


//=============================================================================
// Concurrent Editing Support
//=============================================================================

void AGENT_FRAME::SetAgentTargetSheet( const KIID& aSheetId, const wxString& aSheetName )
{
    m_agentWorkspace.SetTargetSheet( aSheetId );

    // Update the pending changes panel to show target sheet indicator
    // The current sheet name would need to be queried from the schematic editor
    // For now, we'll pass an empty string and let the panel handle it
    m_pendingChangesPanel->SetTargetSheet( aSheetName, wxEmptyString );
}


void AGENT_FRAME::BeginAgentTransaction()
{
    if( m_agentWorkspace.BeginTransaction() )
    {
        wxLogDebug( "Agent transaction started" );

        // Set up conflict callback
        m_agentWorkspace.SetConflictCallback(
            [this]( const KIID& aItemId, const CONFLICT_INFO& aInfo )
            {
                OnConflictDetected( aItemId, aInfo );
            } );
    }
}


void AGENT_FRAME::EndAgentTransaction( bool aCommit )
{
    if( m_agentWorkspace.EndTransaction( aCommit ) )
    {
        if( aCommit )
        {
            wxLogDebug( "Agent transaction committed - pending approval" );
            // Changes are now staged and waiting for user approval
            // The pending changes panel will show them
        }
        else
        {
            wxLogDebug( "Agent transaction reverted" );
        }
    }
}


void AGENT_FRAME::OnConflictDetected( const KIID& aItemId, const CONFLICT_INFO& aInfo )
{
    wxLogDebug( "Conflict detected for item %s: %s",
                aItemId.AsString(), aInfo.m_propertyName );

    // Update the conflict display in the pending changes panel
    UpdateConflictDisplay();

    // Optionally show a notification to the user
    AppendHtml( wxString::Format(
        "<p style='color: #FFA500;'><b>Conflict:</b> You modified item %s which the agent was also editing.</p>",
        aItemId.AsString() ) );
}


void AGENT_FRAME::OnConflictResolved( const KIID& aItemId, CONFLICT_RESOLUTION aResolution )
{
    m_agentWorkspace.ResolveConflict( aItemId, aResolution );
    UpdateConflictDisplay();

    wxString resolutionStr;
    switch( aResolution )
    {
    case CONFLICT_RESOLUTION::KEEP_USER:   resolutionStr = "kept your version"; break;
    case CONFLICT_RESOLUTION::KEEP_AGENT:  resolutionStr = "kept agent's version"; break;
    case CONFLICT_RESOLUTION::AUTO_MERGE:  resolutionStr = "merged both changes"; break;
    default:                               resolutionStr = "resolved manually"; break;
    }

    AppendHtml( wxString::Format(
        "<p><i>Conflict for %s: %s.</i></p>",
        aItemId.AsString(), resolutionStr ) );
}


void AGENT_FRAME::UpdateConflictDisplay()
{
    auto conflicts = m_agentWorkspace.GetConflicts();
    m_pendingChangesPanel->UpdateConflicts( conflicts );

    // If all conflicts are resolved, re-enable accept buttons
    if( conflicts.empty() && m_pendingChangesPanel->IsShown() )
    {
        m_pendingChangesPanel->ClearConflicts();
    }
}


//=============================================================================
// Editor Open Approval
//=============================================================================

void AGENT_FRAME::ShowOpenEditorApproval( const wxString& aEditorType )
{
    // Update m_toolCallHtml instead of using AppendHtml() so the approval UI
    // survives calls to UpdateAgentResponse()
    m_toolCallHtml = wxString::Format(
        "<br><br><table width='100%%' bgcolor='#2d2d2d' cellpadding='10'><tr><td>"
        "<font color='#4ec9b0'><b>Open %s Editor?</b></font> "
        "<a href=\"agent:approve_open\" style=\"color: #00AA00;\">[Open]</a> "
        "<a href=\"agent:reject_open\" style=\"color: #AA0000;\">[Cancel]</a>"
        "</td></tr></table>",
        aEditorType );
    UpdateAgentResponse();
}


void AGENT_FRAME::OnApproveOpenEditor()
{
    bool success = false;
    wxString editorName;

    if( m_pendingOpenSch )
    {
        editorName = "Schematic";
        success = DoOpenEditor( FRAME_SCH );
    }
    else if( m_pendingOpenPcb )
    {
        editorName = "PCB";
        success = DoOpenEditor( FRAME_PCB_EDITOR );
    }

    // Build tool result message
    std::string result = success
        ? editorName.ToStdString() + " editor opened successfully"
        : "Failed to open " + editorName.ToStdString() + " editor";

    // Clear pending state before processing result (in case of re-entrancy)
    std::string toolId = m_pendingOpenToolId;
    m_pendingOpenSch = false;
    m_pendingOpenPcb = false;
    m_pendingOpenToolId.clear();

    // Send tool result to controller - it will continue the conversation
    if( m_chatController )
        m_chatController->HandleToolResult( toolId, result, success );
}


void AGENT_FRAME::OnRejectOpenEditor()
{
    wxString editorName = m_pendingOpenSch ? "Schematic" : "PCB";

    // Clear pending state before processing result (in case of re-entrancy)
    std::string toolId = m_pendingOpenToolId;
    m_pendingOpenSch = false;
    m_pendingOpenPcb = false;
    m_pendingOpenToolId.clear();

    // Send rejection result to controller
    if( m_chatController )
        m_chatController->HandleToolResult( toolId,
            "User declined to open " + editorName.ToStdString() + " editor", false );
}


bool AGENT_FRAME::DoOpenEditor( FRAME_T aFrameType )
{
    KIWAY_PLAYER* player = Kiway().Player( aFrameType, true );
    if( !player )
        return false;

    player->Show( true );
    if( player->IsIconized() )
        player->Iconize( false );
    player->Raise();

    return true;
}


// ============================================================================
// Controller Event Handlers
// These handle events emitted by CHAT_CONTROLLER
// ============================================================================

void AGENT_FRAME::OnChatTextDelta( wxThreadEvent& aEvent )
{
    ChatTextDeltaData* data = aEvent.GetPayload<ChatTextDeltaData*>();
    if( !data )
        return;

    // Controller owns the response - UpdateAgentResponse reads from controller

    // Re-render full response with markdown
    UpdateAgentResponse();

    // Auto-scroll (respects user scroll position)
    AutoScrollToBottom();

    delete data;
}


void AGENT_FRAME::OnChatThinkingStart( wxThreadEvent& aEvent )
{
    ChatThinkingStartData* data = aEvent.GetPayload<ChatThinkingStartData*>();

    // Initialize thinking state
    m_isThinking = true;
    m_thinkingContent = "";
    m_thinkingExpanded = false;

    // Set index for this thinking block (based on historical thinking count)
    m_currentThinkingIndex = static_cast<int>( m_historicalThinking.size() );

    // Rebuild thinking display (shows loading animation)
    RebuildThinkingHtml();
    UpdateAgentResponse();

    if( data )
        delete data;
}


void AGENT_FRAME::OnChatThinkingDelta( wxThreadEvent& aEvent )
{
    ChatThinkingDeltaData* data = aEvent.GetPayload<ChatThinkingDeltaData*>();
    if( !data )
        return;

    // Update thinking state (m_isThinking should already be true from THINKING_START)
    m_isThinking = true;
    m_thinkingContent = data->fullThinking;

    // Rebuild thinking display and re-render
    RebuildThinkingHtml();
    UpdateAgentResponse();

    // Auto-scroll to show thinking
    AutoScrollToBottom();

    delete data;
}


void AGENT_FRAME::OnChatThinkingDone( wxThreadEvent& aEvent )
{
    ChatThinkingDoneData* data = aEvent.GetPayload<ChatThinkingDoneData*>();

    // Finalize thinking state
    m_isThinking = false;

    // Update content from event if available
    if( data )
    {
        m_thinkingContent = data->finalThinking;
        delete data;
    }

    // Rebuild thinking display (removes loading animation)
    RebuildThinkingHtml();
    UpdateAgentResponse();
}


void AGENT_FRAME::OnChatToolStart( wxThreadEvent& aEvent )
{
    ChatToolStartData* data = aEvent.GetPayload<ChatToolStartData*>();
    if( !data )
        return;

    // Stop animation and finalize thinking
    StopGeneratingAnimation();
    m_isThinking = false;

    // Render any pending text and capture HTML state.
    // IMPORTANT: Controller's currentResponse still has the text at this point.
    // We render it, capture the HTML, then clear the streaming state.
    UpdateAgentResponse();
    m_htmlBeforeAgentResponse = m_fullHtmlContent;

    // Now clear streaming state in controller (text is baked into m_htmlBeforeAgentResponse)
    if( m_chatController )
        m_chatController->ClearStreamingState();

    // Clear frame's thinking HTML since it's now part of the base HTML (prevents duplication)
    m_thinkingHtml.Clear();
    m_thinkingContent.Clear();

    // Store tool description for result display
    m_lastToolDesc = wxString::FromUTF8( data->description );

    // Handle open_editor specially - requires user approval
    if( data->toolName == "open_editor" )
    {
        std::string editorType = data->input.value( "editor_type", "" );

        // Store the pending request
        m_pendingOpenSch = ( editorType == "sch" );
        m_pendingOpenPcb = ( editorType == "pcb" );
        m_pendingOpenToolId = data->toolId;

        // Show approval dialog
        wxString editorLabel = m_pendingOpenSch ? "Schematic" : "PCB";
        ShowOpenEditorApproval( editorLabel );

        delete data;
        return;
    }

    // Generate tool call HTML with "Running..." status
    m_toolCallHtml = wxString::Format(
        "<br><br><table width='100%%' bgcolor='#2d2d2d' cellpadding='10'><tr><td style='word-wrap:break-word;'>"
        "<font color='#4ec9b0'><b>Tool Call:</b></font> %s<br>"
        "<font color='#888888'><i>Running...</i></font>"
        "</td></tr></table>",
        m_lastToolDesc );

    UpdateAgentResponse();
    AutoScrollToBottom();

    delete data;
}


void AGENT_FRAME::OnChatToolComplete( wxThreadEvent& aEvent )
{
    ChatToolCompleteData* data = aEvent.GetPayload<ChatToolCompleteData*>();
    if( !data )
        return;

    // Determine status display
    wxString statusColor;
    wxString statusText;
    wxString displayResult;

    if( data->isPythonError )
    {
        statusColor = "#f44747";
        statusText = "Error";
        displayResult = "<i>Script execution failed. The model will attempt to fix the issue.</i>";
    }
    else if( !data->success )
    {
        statusColor = "#f44747";
        statusText = "Failed";
        wxString htmlResult = wxString::FromUTF8( data->result );
        htmlResult.Replace( "&", "&amp;" );
        htmlResult.Replace( "<", "&lt;" );
        htmlResult.Replace( ">", "&gt;" );
        if( htmlResult.length() > 200 )
            htmlResult = htmlResult.Left( 200 ) + "...";
        displayResult = htmlResult;
    }
    else
    {
        statusColor = "#4ec9b0";
        statusText = "Completed";
        wxString htmlResult = wxString::FromUTF8( data->result );
        htmlResult.Replace( "&", "&amp;" );
        htmlResult.Replace( "<", "&lt;" );
        htmlResult.Replace( ">", "&gt;" );
        if( htmlResult.length() > 500 )
            htmlResult = htmlResult.Left( 500 ) + "... (truncated)";
        displayResult = htmlResult;
    }

    // Update tool call HTML with result (replace "Running..." with actual result)
    m_toolCallHtml = wxString::Format(
        "<br><br><table width='100%%' bgcolor='#2d2d2d' cellpadding='10'><tr><td style='word-wrap:break-word;'>"
        "<font color='#4ec9b0'><b>Tool Call:</b></font> %s<br>"
        "<font color='%s'><b>%s</b></font><br>"
        "<font color='#d4d4d4' size='2'>%s</font>"
        "</td></tr></table><br>",
        m_lastToolDesc, statusColor, statusText, displayResult );

    // Check for pending approval
    CheckForPendingChanges();

    UpdateAgentResponse();
    AutoScrollToBottom();

    delete data;
}


void AGENT_FRAME::OnChatTurnComplete( wxThreadEvent& aEvent )
{
    ChatTurnCompleteData* data = aEvent.GetPayload<ChatTurnCompleteData*>();
    if( !data )
        return;

    // Stop animation and update button
    StopGeneratingAnimation();
    m_actionButton->SetLabel( "Send" );

    // Finalize thinking state
    m_isThinking = false;

    // Sync history from controller (controller added the assistant message in END_TURN)
    // This must happen BEFORE RenderChatHistory() which uses frame's m_chatHistory
    if( m_chatController )
    {
        m_chatHistory = m_chatController->GetChatHistory();
        m_apiContext = m_chatController->GetApiContext();
        m_chatHistoryDb.Save( m_chatHistory );
    }

    // Clear streaming UI state
    m_currentResponse.clear();
    m_thinkingContent.Clear();
    m_toolCallHtml.Clear();

    // Preserve thinking expansion state
    if( m_thinkingExpanded && m_currentThinkingIndex >= 0 )
        m_historicalThinkingExpanded.insert( m_currentThinkingIndex );

    m_thinkingExpanded = false;
    m_currentThinkingIndex = -1;

    // Re-render from history to show saved content
    RenderChatHistory();

    delete data;
}


void AGENT_FRAME::OnChatError( wxThreadEvent& aEvent )
{
    ChatErrorData* data = aEvent.GetPayload<ChatErrorData*>();
    if( !data )
        return;

    // Display error message
    wxString errorHtml = wxString::Format(
        "<p><font color='red'><b>Error:</b> %s</font></p>",
        wxString::FromUTF8( data->message ) );
    AppendHtml( errorHtml );

    // Stop animation and reset button
    StopGeneratingAnimation();
    m_actionButton->SetLabel( "Send" );

    // Clear streaming state
    m_isThinking = false;

    delete data;
}


void AGENT_FRAME::OnChatStateChanged( wxThreadEvent& aEvent )
{
    ChatStateChangedData* data = aEvent.GetPayload<ChatStateChangedData*>();
    if( !data )
        return;

    // Update button based on new state
    AgentConversationState newState = static_cast<AgentConversationState>( data->newState );

    switch( newState )
    {
    case AgentConversationState::IDLE:
    case AgentConversationState::ERROR:
        m_actionButton->SetLabel( "Send" );
        break;

    case AgentConversationState::WAITING_FOR_LLM:
        m_actionButton->SetLabel( "Stop" );
        StartGeneratingAnimation();

        // If continuing after tool completion, capture current state as base
        // This ensures the tool call result appears BEFORE any new response text
        if( static_cast<AgentConversationState>( data->oldState ) ==
            AgentConversationState::PROCESSING_TOOL_RESULT )
        {
            // Render current state (includes tool call result)
            UpdateAgentResponse();
            m_htmlBeforeAgentResponse = m_fullHtmlContent;

            // Clear tool call HTML since it's now baked into the base
            m_toolCallHtml.Clear();
            m_thinkingHtml.Clear();
        }
        break;

    case AgentConversationState::TOOL_USE_DETECTED:
    case AgentConversationState::EXECUTING_TOOL:
    case AgentConversationState::PROCESSING_TOOL_RESULT:
        // Keep current button state during tool execution
        break;
    }

    delete data;
}


void AGENT_FRAME::OnChatTitleGenerated( wxThreadEvent& aEvent )
{
    ChatTitleGeneratedData* data = aEvent.GetPayload<ChatTitleGeneratedData*>();
    if( !data )
        return;

    // Update title display
    m_chatNameLabel->SetLabel( wxString::FromUTF8( data->title ) );

    // Update persistence with the new title
    m_chatHistoryDb.SetTitle( data->title );
    if( m_chatController )
    {
        m_chatHistoryDb.Save( m_chatController->GetChatHistory() );
    }

    delete data;
}


void AGENT_FRAME::OnChatHistoryLoaded( wxThreadEvent& aEvent )
{
    ChatHistoryLoadedData* data = aEvent.GetPayload<ChatHistoryLoadedData*>();
    if( !data )
        return;

    // Hide history panel overlay if visible
    if( m_historyPanel && m_historyPanel->IsShown() )
    {
        m_historyPanel->Hide();
    }

    // Update chat name label with title
    std::string title = data->title;
    if( title.empty() )
        title = "Untitled Chat";
    m_chatNameLabel->SetLabel( wxString::FromUTF8( title ) );

    // Sync history from controller
    if( m_chatController )
    {
        m_chatHistory = m_chatController->GetChatHistory();
        m_apiContext = m_chatController->GetApiContext();
    }

    // Mark that title is already generated for this loaded chat
    m_needsTitleGeneration = false;
    m_firstUserMessage = "";

    // Clear historical thinking toggle state for new history
    m_historicalThinkingExpanded.clear();
    m_currentThinkingIndex = -1;

    // Render the loaded chat history
    RenderChatHistory();

    // Update DB ID so new messages go to this history
    m_chatHistoryDb.SetConversationId( data->chatId );

    // Scroll to bottom of loaded conversation (use CallAfter to ensure layout is complete)
    m_userScrolledUp = false;
    CallAfter( [this]() {
        int virtWidth, virtHeight;
        m_chatWindow->GetVirtualSize( &virtWidth, &virtHeight );

        int clientWidth, clientHeight;
        m_chatWindow->GetClientSize( &clientWidth, &clientHeight );

        int scrollUnitX, scrollUnitY;
        m_chatWindow->GetScrollPixelsPerUnit( &scrollUnitX, &scrollUnitY );

        if( scrollUnitY <= 0 )
            scrollUnitY = 10;

        // Use ceiling division to ensure we scroll completely to the bottom
        int maxScrollY = ( virtHeight - clientHeight + scrollUnitY - 1 ) / scrollUnitY;
        if( maxScrollY < 0 )
            maxScrollY = 0;

        m_chatWindow->Scroll( 0, maxScrollY );
    });

    delete data;
}


void AGENT_FRAME::OnChatContextStatus( wxThreadEvent& aEvent )
{
    ChatContextStatusData* data = aEvent.GetPayload<ChatContextStatusData*>();

    if( data && data->wasCompacted )
    {
        AppendHtml( "<p><font color='#FFA500'><i>Context was automatically "
                    "compacted to continue the conversation.</i></font></p>" );
    }

    if( data )
        delete data;
}


void AGENT_FRAME::OnChatContextCompacting( wxThreadEvent& aEvent )
{
    ChatContextCompactingData* data = aEvent.GetPayload<ChatContextCompactingData*>();

    AppendHtml( "<p><font color='#FFA500'><i>Compacting context...</i></font></p>" );

    if( data )
        delete data;
}


void AGENT_FRAME::OnChatContextRecovered( wxThreadEvent& aEvent )
{
    ChatContextRecoveredData* data = aEvent.GetPayload<ChatContextRecoveredData*>();

    if( data && !data->summarizedMessages.empty() && data->summarizedMessages.is_array() )
    {
        // Replace API context with compacted version (display history unchanged)
        m_apiContext = data->summarizedMessages;

        // Show notification
        AppendHtml( "<p><font color='#FFA500'><i>Context compacted. Retrying...</i></font></p>" );

        // Retry with compacted context
        CallAfter( [this]() { RetryLastRequest(); } );
    }

    if( data )
        delete data;
}
