#include "agent_frame.h"
#include "agent_chat_history.h"
#include "auth/agent_auth.h"
#include "auth/agent_keychain.h"
#include "ui/pending_changes_popup.h"
#include "ui/history_panel.h"
#include "rendering/agent_markdown.h"
#include "rendering/agent_html_template.h"
#include "tools/agent_tools.h"
#include "core/chat_controller.h"
#include "core/chat_events.h"
#include "tools/tool_registry.h"
#include "tools/kicad_file/file_writer.h"
#include "tools/schematic/sch_parser.h"
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
#include <wx/dir.h>
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
#include <kicad_curl/kicad_curl_easy.h>
#include <kiid.h>

using json = nlohmann::json;

// Helper function to extract the error line from a Python traceback.
// Python tracebacks end with the actual error on the last non-empty line.
static wxString ExtractPythonErrorLine( const std::string& traceback )
{
    // Python tracebacks end with the error line, e.g.:
    // "AttributeError: 'Board' object has no attribute 'foo'"
    // Find the last non-empty line
    size_t pos = traceback.rfind( '\n' );
    while( pos != std::string::npos && pos > 0 )
    {
        size_t prevPos = traceback.rfind( '\n', pos - 1 );
        size_t lineStart = ( prevPos == std::string::npos ) ? 0 : prevPos + 1;
        std::string line = traceback.substr( lineStart, pos - lineStart );

        // Skip empty lines
        if( !line.empty() && line.find_first_not_of( " \t\r" ) != std::string::npos )
        {
            wxString result = wxString::FromUTF8( line );
            result.Replace( "&", "&amp;" );
            result.Replace( "<", "&lt;" );
            result.Replace( ">", "&gt;" );
            return result;
        }
        pos = prevPos;
    }
    return "Unknown error";
}

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
        m_trackAgentBtn( nullptr ),
        m_isTrackingAgent( false ),
        m_historyPanel( nullptr ),
        m_pendingOpenSch( false ),
        m_pendingOpenPcb( false )
{
    // Set frame background color to match the theme
    SetBackgroundColour( wxColour( "#1E1E1E" ) );

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

    // 1. Chat History Area (using wxWebView for full HTML/CSS support)
    m_chatWindow = new WEBVIEW_PANEL( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0 );
    m_chatWindow->AddMessageHandler( wxS( "agent" ),
        [this]( const wxString& msg ) { OnWebViewMessage( msg ); } );

    // Bind the loaded event so message handlers get registered when page loads
    m_chatWindow->BindLoadedEvent();

    // Set initial page content with HTML5 template and CSS
    m_fullHtmlContent = GetAgentHtmlTemplate() + wxS( "</div></body></html>" );
    m_chatWindow->SetPage( m_fullHtmlContent );
    mainSizer->Add( m_chatWindow, 1, wxEXPAND | wxALL, 0 ); // Remove ALL padding for clean edge

    // Pending Changes Panel (between chat and input, hidden by default)
    // Tab-like appearance with margins from the sides
    m_pendingChangesPanel = new PENDING_CHANGES_PANEL( this, this );
    mainSizer->Add( m_pendingChangesPanel, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12 );

    // 2. Input Container (Unified Look)
    // Create m_inputPanel for dark styling
    m_inputPanel = new wxPanel( this, wxID_ANY );
    m_inputPanel->SetBackgroundColour( wxColour( "#1C1C1C" ) );

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
    m_inputCtrl->SetBackgroundColour( wxColour( "#1C1C1C" ) );
    m_inputCtrl->SetForegroundColour( wxColour( "#FFFFFF" ) );

    // Use system default font (matches chat name label)
    wxFont font = wxSystemSettings::GetFont( wxSYS_DEFAULT_GUI_FONT );
    font.SetPointSize( 12 );
    m_inputCtrl->SetFont( font );

    inputContainerSizer->Add( m_inputCtrl, 1, wxEXPAND | wxBOTTOM, 5 );

    // 2b. Control Row (Bottom)
    wxBoxSizer* controlsSizer = new wxBoxSizer( wxHORIZONTAL );

    // Model Selection (Opus only - via zener.so proxy)
    wxArrayString modelChoices;
    modelChoices.Add( "Claude 4.6 Opus" );

    m_modelChoice = new wxChoice( m_inputPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, modelChoices );
    m_modelChoice->SetMinSize( wxSize( m_modelChoice->GetBestSize().x + 20, -1 ) );
    m_modelChoice->SetSelection( 0 ); // Default to Claude 4.6 Opus
    controlsSizer->Add( m_modelChoice, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5 );

    // Track Agent Button
    m_trackAgentBtn = new wxButton( m_inputPanel, wxID_ANY, "Track",
                                    wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT );
    m_trackAgentBtn->SetToolTip( "Follow agent changes automatically" );
    controlsSizer->Add( m_trackAgentBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5 );

    // Spacer
    controlsSizer->AddStretchSpacer();

    // Send Button
    m_actionButton = new wxButton( m_inputPanel, wxID_ANY, "Send" );
    controlsSizer->Add( m_actionButton, 0, wxALIGN_CENTER_VERTICAL );

    inputContainerSizer->Add( controlsSizer, 0, wxEXPAND );



    // Add inner sizer to outer sizer with padding matching top bar (10px on all sides)
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
    m_trackAgentBtn->Bind( wxEVT_BUTTON, &AGENT_FRAME::OnTrackAgentClick, this );

    // m_toolButton->Bind( wxEVT_BUTTON, &AGENT_FRAME::OnToolClick, this );
    // Note: Link clicks and right-click are now handled via JavaScript message passing
    // through OnWebViewMessage() - see AddMessageHandler in constructor
    Bind( wxEVT_MENU, &AGENT_FRAME::OnPopupClick, this, ID_CHAT_COPY );
    m_inputCtrl->Bind( wxEVT_KEY_DOWN, &AGENT_FRAME::OnInputKeyDown, this );
    m_inputCtrl->Bind( wxEVT_TEXT, &AGENT_FRAME::OnInputText, this );

    // Bind Async LLM Streaming Events
    Bind( EVT_LLM_STREAM_CHUNK, &AGENT_FRAME::OnLLMStreamChunk, this );
    Bind( EVT_LLM_STREAM_COMPLETE, &AGENT_FRAME::OnLLMStreamComplete, this );
    Bind( EVT_LLM_STREAM_ERROR, &AGENT_FRAME::OnLLMStreamError, this );

    // Initialize generating animation
    m_generatingDots = 0;
    m_isGenerating = false;
    m_isCompacting = false;
    m_userScrolledUp = false;
    m_lastScrollActivityMs = 0;
    m_htmlUpdatePending = false;
    m_htmlUpdateNeeded = false;
    m_generatingTimer.Bind( wxEVT_TIMER, &AGENT_FRAME::OnGeneratingTimer, this );
    m_htmlUpdateTimer.Bind( wxEVT_TIMER, &AGENT_FRAME::OnHtmlUpdateTimer, this );

    // Initialize thinking state
    m_thinkingExpanded = false;
    m_isThinking = false;
    m_isStreamingMarkdown = false;
    m_currentThinkingIndex = -1;

    // Bind Model Change Event
    m_modelChoice->Bind( wxEVT_CHOICE, &AGENT_FRAME::OnModelSelection, this );

    // Bind Size Event (to reposition overlay)
    Bind( wxEVT_SIZE, &AGENT_FRAME::OnSize, this );

    // Initialize History
    m_chatHistory = nlohmann::json::array();
    m_apiContext = nlohmann::json::array();
    m_pendingToolCalls = nlohmann::json::array();

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
    m_chatController->SetAuth( m_auth );
    m_chatController->SetKiwayRequestFn(
        [this]( int aFrameType, const std::string& aPayload ) -> std::string {
            return SendRequest( aFrameType, aPayload );
        } );
    m_chatController->SetProjectPathFn(
        [this]() -> std::string {
            wxString projectPath = Kiway().Prj().GetProjectPath();
            if( projectPath.IsEmpty() )
                return "";

            // Build JSON with project path, PCB file, and schematic hierarchy
            nlohmann::json projectContext;
            projectContext["path"] = projectPath.ToStdString();

            // Get project name for conventional file detection
            wxFileName projDir( projectPath, "" );
            wxString projName = projDir.GetDirs().IsEmpty() ? wxString() : projDir.GetDirs().Last();

            // Find PCB file - try projectName.kicad_pcb first, then scan directory
            wxDir dir( projectPath );
            if( !projName.IsEmpty() )
            {
                wxString expectedPcb = projectPath + projName + ".kicad_pcb";
                if( wxFileExists( expectedPcb ) )
                {
                    projectContext["pcb_file"] = ( projName + ".kicad_pcb" ).ToStdString();
                }
            }

            // Fallback: scan for all PCB files if expected one not found
            if( !projectContext.contains( "pcb_file" ) && dir.IsOpened() )
            {
                nlohmann::json pcbFiles = nlohmann::json::array();
                wxString filename;
                bool cont = dir.GetFirst( &filename, "*.kicad_pcb", wxDIR_FILES );
                while( cont )
                {
                    pcbFiles.push_back( filename.ToStdString() );
                    cont = dir.GetNext( &filename );
                }
                if( !pcbFiles.empty() )
                    projectContext["pcb_files"] = pcbFiles;
            }

            // Build schematic hierarchy from root sheet(s)
            // Define recursive hierarchy builder
            std::function<nlohmann::json( const std::string&, std::set<std::string>& )> buildHierarchy;
            buildHierarchy = [&]( const std::string& schPath,
                                  std::set<std::string>& visited ) -> nlohmann::json {
                nlohmann::json node;

                // Avoid infinite loops from circular references
                if( visited.count( schPath ) )
                    return node;
                visited.insert( schPath );

                auto summary = SchParser::GetSummary( schPath );
                node["file"] = summary.file;
                node["uuid"] = summary.uuid;

                // Recursively process child sheets
                if( !summary.sheets.empty() )
                {
                    nlohmann::json children = nlohmann::json::array();
                    for( const auto& sheet : summary.sheets )
                    {
                        // Resolve child sheet path relative to parent
                        wxFileName childPath( schPath );
                        childPath.SetFullName( sheet.filename );
                        std::string childFullPath = childPath.GetFullPath().ToStdString();

                        nlohmann::json childNode = buildHierarchy( childFullPath, visited );
                        if( !childNode.empty() )
                        {
                            childNode["name"] = sheet.name;  // Display name from parent
                            children.push_back( childNode );
                        }
                    }
                    if( !children.empty() )
                        node["children"] = children;
                }

                return node;
            };

            // Find root schematic(s) - check .kicad_pro for top_level_sheets
            std::vector<std::string> rootSchFiles;

            // Try to read top-level sheets from project file
            if( !projName.IsEmpty() )
            {
                wxString proFile = projectPath + projName + ".kicad_pro";
                if( wxFileExists( proFile ) )
                {
                    std::ifstream ifs( proFile.ToStdString() );
                    if( ifs.good() )
                    {
                        try
                        {
                            nlohmann::json projJson = nlohmann::json::parse( ifs );
                            if( projJson.contains( "schematic" ) &&
                                projJson["schematic"].contains( "top_level_sheets" ) )
                            {
                                for( const auto& sheet : projJson["schematic"]["top_level_sheets"] )
                                {
                                    if( sheet.contains( "filename" ) )
                                    {
                                        wxString schFile = projectPath +
                                            wxString::FromUTF8( sheet["filename"].get<std::string>() );
                                        if( wxFileExists( schFile ) )
                                            rootSchFiles.push_back( schFile.ToStdString() );
                                    }
                                }
                            }
                        }
                        catch( ... )
                        {
                            // JSON parse error - fall back to heuristics
                        }
                    }
                }
            }

            // Fall back to project-name.kicad_sch if no top-level sheets defined
            if( rootSchFiles.empty() && !projName.IsEmpty() )
            {
                wxString rootCandidate = projectPath + projName + ".kicad_sch";
                if( wxFileExists( rootCandidate ) )
                    rootSchFiles.push_back( rootCandidate.ToStdString() );
            }

            // Build hierarchy for each root
            if( !rootSchFiles.empty() )
            {
                nlohmann::json hierarchyArray = nlohmann::json::array();
                std::set<std::string> visited;

                for( const auto& rootFile : rootSchFiles )
                {
                    nlohmann::json rootNode = buildHierarchy( rootFile, visited );
                    if( !rootNode.empty() )
                        hierarchyArray.push_back( rootNode );
                }

                projectContext["hierarchy"] = hierarchyArray;
            }

            return projectContext.dump( 2 );
        } );

    // Set model on controller
    m_chatController->SetModel( "Claude 4.5 Sonnet" );

    // Bind Controller Events
    Bind( EVT_CHAT_TEXT_DELTA, &AGENT_FRAME::OnChatTextDelta, this );
    Bind( EVT_CHAT_THINKING_START, &AGENT_FRAME::OnChatThinkingStart, this );
    Bind( EVT_CHAT_THINKING_DELTA, &AGENT_FRAME::OnChatThinkingDelta, this );
    Bind( EVT_CHAT_THINKING_DONE, &AGENT_FRAME::OnChatThinkingDone, this );
    Bind( EVT_CHAT_TOOL_GENERATING, &AGENT_FRAME::OnChatToolGenerating, this );
    Bind( EVT_CHAT_TOOL_START, &AGENT_FRAME::OnChatToolStart, this );
    Bind( EVT_CHAT_TOOL_COMPLETE, &AGENT_FRAME::OnChatToolComplete, this );
    Bind( EVT_CHAT_TURN_COMPLETE, &AGENT_FRAME::OnChatTurnComplete, this );
    Bind( EVT_CHAT_ERROR, &AGENT_FRAME::OnChatError, this );
    Bind( EVT_CHAT_STATE_CHANGED, &AGENT_FRAME::OnChatStateChanged, this );
    Bind( EVT_CHAT_TITLE_DELTA, &AGENT_FRAME::OnChatTitleDelta, this );
    Bind( EVT_CHAT_TITLE_GENERATED, &AGENT_FRAME::OnChatTitleGenerated, this );
    Bind( EVT_CHAT_HISTORY_LOADED, &AGENT_FRAME::OnChatHistoryLoaded, this );

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
    // Update internal state for consistency
    wxString closingTags = "</div></body></html>";

    if( m_fullHtmlContent.EndsWith( closingTags ) )
    {
        m_fullHtmlContent = m_fullHtmlContent.Left( m_fullHtmlContent.length() - closingTags.length() );
        m_fullHtmlContent += aHtml;
        m_fullHtmlContent += closingTags;
    }
    else
    {
        m_fullHtmlContent += aHtml;
    }

    // Use RunScriptAsync for incremental DOM update (no SetPage)
    if( m_chatWindow )
    {
        wxString escaped = aHtml;
        escaped.Replace( "\\", "\\\\" );  // Backslashes first
        escaped.Replace( "'", "\\'" );    // Single quotes
        escaped.Replace( "\n", "\\n" );   // Newlines
        escaped.Replace( "\r", "\\r" );   // Carriage returns

        wxString script = wxString::Format( "appendToChat('%s');", escaped );
        m_chatWindow->RunScriptAsync( script );
    }
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

    // Use placeholder if content is empty (during initial THINKING_START)
    // This ensures the content div exists immediately for user clicks
    wxString displayContent = escapedContent.IsEmpty() ? "<i>Thinking...</i>" : escapedContent;

    // Always render both toggle link and content (content hidden by CSS if collapsed)
    // JavaScript will toggle visibility without page reload
    wxString expandedClass = m_thinkingExpanded ? " expanded" : "";
    wxString displayStyle = m_thinkingExpanded ? "block" : "none";

    wxString thinkingText = "Thinking";

    m_thinkingHtml = wxString::Format(
        "<a href=\"toggle:thinking:%d\" class=\"text-text-muted cursor-pointer no-underline hover:underline\" data-thinking-index=\"%d\">%s</a><br>"
        "<div class=\"thinking-content text-[#606060] mt-2 pl-3 border-l-2 border-[#404040] whitespace-pre-wrap%s\" data-thinking-index=\"%d\" style=\"display:%s;\">%s</div><br>",
        m_currentThinkingIndex, m_currentThinkingIndex, thinkingText, expandedClass, m_currentThinkingIndex, displayStyle, displayContent );
}

void AGENT_FRAME::UpdateAgentResponse()
{
    // Mark that an HTML update is needed and start the throttling timer if not running.
    // This batches multiple rapid calls (on every character during streaming) into
    // ~20 updates/sec max to prevent WebKit segfaults from excessive SetPage() calls.
    m_htmlUpdateNeeded = true;

    if( !m_htmlUpdateTimer.IsRunning() )
    {
        // Start timer with 50ms interval (20 updates per second max)
        m_htmlUpdateTimer.Start( 50, wxTIMER_CONTINUOUS );
    }
}

void AGENT_FRAME::OnGeneratingTimer( wxTimerEvent& aEvent )
{
    // Cycle through 1, 2, 3 dots
    m_generatingDots = ( m_generatingDots % 3 ) + 1;
    UpdateAgentResponse();
    // Auto-scroll handled by CSS flex-direction: column-reverse
}

wxString AGENT_FRAME::BuildStreamingContent()
{
    // Build the streaming content HTML from current state
    wxString streamingContent;

    // Include thinking HTML if available (streamed directly in updateStreamingContent)
    if( !m_thinkingHtml.IsEmpty() )
        streamingContent += m_thinkingHtml;

    // Get current response from controller and append with markdown
    std::string currentResponse = m_chatController ? m_chatController->GetCurrentResponse() : "";
    streamingContent += AgentMarkdown::ToHtml( currentResponse );

    // Include any tool call HTML
    if( !m_toolCallHtml.IsEmpty() )
        streamingContent += m_toolCallHtml;

    // Add animated dots when compacting or generating
    if( m_isCompacting )
    {
        wxString dots;
        for( int i = 0; i < m_generatingDots; i++ )
            dots += ".";
        streamingContent += "<font color='#FFA500'>Compacting" + dots + "</font>";
    }
    else if( m_isGenerating && !m_isStreamingMarkdown )
    {
        wxString dots;
        for( int i = 0; i < m_generatingDots; i++ )
            dots += ".";

        // Show tool name if a tool is being generated
        if( !m_generatingToolName.IsEmpty() )
            streamingContent += "<font color='#888888'>" + m_generatingToolName + dots + "</font>";
        else
            streamingContent += "<font color='#888888'>" + dots + "</font>";
    }

    return streamingContent;
}

void AGENT_FRAME::FlushStreamingContentUpdate( bool aForce )
{
    // Immediately flush streaming content to DOM (used to prevent race conditions)
    // This is called when we need to bypass the timer throttling for critical updates
    if( !m_chatWindow )
        return;

    // Skip update if user scrolled up, unless forced (e.g., user-initiated toggle)
    if( m_userScrolledUp && !aForce )
        return;

    // Build streaming content
    wxString streamingContent = BuildStreamingContent();

    // Escape the HTML for JavaScript string
    wxString escaped = streamingContent;
    escaped.Replace( "\\", "\\\\" );  // Escape backslashes first
    escaped.Replace( "'", "\\'" );    // Escape single quotes
    escaped.Replace( "\n", "\\n" );   // Escape newlines
    escaped.Replace( "\r", "\\r" );   // Escape carriage returns

    wxString script = wxString::Format( "updateStreamingContent('%s');", escaped );
    m_chatWindow->RunScriptAsync( script );

    // Clear the pending update flag since we just executed it
    m_htmlUpdateNeeded = false;
}

void AGENT_FRAME::OnHtmlUpdateTimer( wxTimerEvent& aEvent )
{
    // Throttled HTML update - only update if needed AND user hasn't scrolled up
    if( m_htmlUpdateNeeded && !m_userScrolledUp )
    {
        m_htmlUpdateNeeded = false;

        // Build streaming content using shared helper
        wxString streamingContent = BuildStreamingContent();

        // Use RunScript() for incremental DOM update instead of SetPage()
        // This avoids full page reload and the associated scroll/layer conflicts
        if( m_chatWindow )
        {
            // Escape the HTML for JavaScript string
            wxString escaped = streamingContent;
            escaped.Replace( "\\", "\\\\" );  // Escape backslashes first
            escaped.Replace( "'", "\\'" );    // Escape single quotes
            escaped.Replace( "\n", "\\n" );   // Escape newlines
            escaped.Replace( "\r", "\\r" );   // Escape carriage returns

            wxString script = wxString::Format( "updateStreamingContent('%s');", escaped );
            m_chatWindow->RunScriptAsync( script );
        }

        // Also update the full HTML content for when we need to do a full render later
        // Remove any empty streaming divs to prevent accumulation, then append the filled one
        wxString fullHtml = m_htmlBeforeAgentResponse;
        const wxString closingTags = wxS( "</div></body></html>" );

        // Remove closing tags
        if( fullHtml.EndsWith( closingTags ) )
            fullHtml = fullHtml.Left( fullHtml.length() - closingTags.length() );

        // Remove any empty streaming content divs (prevents accumulation)
        fullHtml.Replace( wxS( "<div id=\"streaming-content\"></div>" ), wxS( "" ) );

        // Append the filled streaming div and restore closing tags
        fullHtml += wxS( "<div id=\"streaming-content\">" ) + streamingContent + wxS( "</div>" );
        fullHtml += closingTags;
        m_fullHtmlContent = fullHtml;
    }
    // If user scrolled up, leave m_htmlUpdateNeeded=true so update happens when they scroll back
}

void AGENT_FRAME::StartGeneratingAnimation()
{
    m_isGenerating = true;
    m_isStreamingMarkdown = false;
    m_generatingDots = 1;
    m_generatingTimer.Start( 400 ); // Update every 400ms
    m_actionButton->SetLabel( "Stop" );
}

void AGENT_FRAME::StopGeneratingAnimation()
{
    m_isGenerating = false;
    m_isStreamingMarkdown = false;
    m_generatingTimer.Stop();
    m_htmlUpdateTimer.Stop();
    m_generatingDots = 0;
    m_generatingToolName.Clear();

    // Perform one final HTML update if there was a pending update
    if( m_htmlUpdateNeeded )
    {
        m_htmlUpdateNeeded = false;

        // Build the streaming content (same logic as OnHtmlUpdateTimer)
        wxString streamingContent;

        if( !m_thinkingHtml.IsEmpty() )
            streamingContent += m_thinkingHtml;

        std::string currentResponse = m_chatController ? m_chatController->GetCurrentResponse() : "";
        streamingContent += AgentMarkdown::ToHtml( currentResponse );

        if( !m_toolCallHtml.IsEmpty() )
            streamingContent += m_toolCallHtml;

        // Use RunScriptAsync for incremental DOM update (no SetPage)
        if( m_chatWindow )
        {
            wxString escaped = streamingContent;
            escaped.Replace( "\\", "\\\\" );
            escaped.Replace( "'", "\\'" );
            escaped.Replace( "\n", "\\n" );
            escaped.Replace( "\r", "\\r" );

            wxString script = wxString::Format( "updateStreamingContent('%s');", escaped );
            m_chatWindow->RunScriptAsync( script );
        }

        // Update full HTML content for consistency
        // Remove any empty streaming divs to prevent accumulation, then append the filled one
        wxString html = m_htmlBeforeAgentResponse;
        const wxString closingTags = wxS( "</div></body></html>" );

        // Remove closing tags
        if( html.EndsWith( closingTags ) )
            html = html.Left( html.length() - closingTags.length() );

        // Remove any empty streaming content divs (prevents accumulation)
        html.Replace( wxS( "<div id=\"streaming-content\"></div>" ), wxS( "" ) );

        // Append the filled streaming div and restore closing tags
        html += wxS( "<div id=\"streaming-content\">" ) + streamingContent + wxS( "</div>" );
        html += closingTags;

        m_fullHtmlContent = html;
    }

    // Note: Don't set button to "Send" here - let the caller decide
    // (tool execution keeps "Stop", only IDLE sets "Send")
}

void AGENT_FRAME::SetHtml( const wxString& aHtml )
{
    m_fullHtmlContent = aHtml; // Ensure sync

    // Throttle SetPage() calls using CallAfter to prevent crashes during scroll events
    // WebKit's layer enumeration can conflict with DOM modifications
    if( !m_htmlUpdatePending )
    {
        m_htmlUpdatePending = true;
        CallAfter( [this]()
        {
            m_chatWindow->SetPage( m_fullHtmlContent );
            m_htmlUpdatePending = false;
        } );
    }
}

void AGENT_FRAME::AutoScrollToBottom()
{
    // Auto-scroll now handled by CSS flex-direction: column-reverse
    // No-op kept for compatibility with existing code
}

void AGENT_FRAME::OnChatScroll( wxScrollWinEvent& aEvent )
{
    // Scroll tracking now handled by CSS flex-direction: column-reverse
    // No manual scroll management needed
    aEvent.Skip();
}

void AGENT_FRAME::KiwayMailIn( KIWAY_EXPRESS& aEvent )
{
    if( aEvent.Command() == MAIL_AGENT_RESPONSE )
    {
        std::string payload = aEvent.GetPayload();

        // Check if we're in async tool execution mode (frame's context has an executing tool)
        // NOTE: The controller also executes tools via the synchronous SendRequest path,
        // which expects m_toolResponse to be set. Only use async path if the FRAME
        // actually has a tool marked as executing.
        PendingToolCall* executing = m_conversationCtx.GetExecutingToolCall();

        if( executing )
        {
            // Frame has an executing tool - use async path
            // Post tool completion event
            ToolExecutionResult* result = new ToolExecutionResult();
            result->tool_use_id = executing->tool_use_id;
            result->tool_name = executing->tool_name;
            result->result = payload;
            result->success = !payload.empty() && payload.find( "Error:" ) != 0;
            result->execution_time_ms = ( wxGetLocalTimeMillis() - executing->start_time ).GetValue();

            PostToolResult( this, *result );
            delete result;  // PostToolResult copies the data
        }
        else
        {
            // Sync mode (controller path) - store response for SendRequest() to pick up
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
    else if( aEvent.Command() == MAIL_AGENT_DIFF_CLEARED ||
             aEvent.Command() == MAIL_AGENT_CHECK_CHANGES )
    {
        // Diff overlay was dismissed or items changed - refresh the panel
        wxLogInfo( "AGENT_FRAME: Received diff/check changes notification, refreshing panel" );
        RefreshPendingChangesPanel();

        // Clear the selection pill - selected items may have been deleted by rejection or undo
        // The editors will send a new MAIL_SELECTION if items are still selected
        m_selectionPill->Show( false );
    }
    else if( aEvent.Command() == MAIL_AGENT_TRACKING_BROKEN )
    {
        // User broke tracking by interacting with the editor
        m_isTrackingAgent = false;
        m_trackAgentBtn->SetLabel( "Track" );
    }
    Layout();
}

void AGENT_FRAME::OnSelectionPillClick( wxCommandEvent& aEvent )
{
    wxLogInfo( "AGENT_FRAME::OnSelectionPillClick called" );
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
    wxLogInfo( "AGENT_FRAME::OnSend called" );
    // NOTE: This method still uses legacy code because it handles KiCad-specific requirements
    // (authentication, pending editor state, system prompt with schematic/PCB context, KIWAY
    // target sheet reset) that the controller doesn't currently support.
    // System prompt is now handled server-side.

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

    // Reset scroll state for new user message - user sending indicates engagement at bottom
    m_userScrolledUp = false;

    // Build user message HTML
    wxString escapedText = text;
    escapedText.Replace( "&", "&amp;" );
    escapedText.Replace( "<", "&lt;" );
    escapedText.Replace( ">", "&gt;" );
    escapedText.Replace( "\n", "<br>" );
    wxString msgHtml = wxString::Format(
        "<div class=\"flex justify-end my-1.5\"><div class=\"bg-bg-tertiary py-2 px-3.5 rounded-lg max-w-[80%%] whitespace-pre-wrap\">%s</div></div>",
        escapedText );

    // Add streaming content container for incremental updates
    wxString streamingDiv = wxS( "<div id=\"streaming-content\"></div>" );

    // Append to internal HTML state
    const wxString closingTags = wxS( "</div></body></html>" );
    if( m_fullHtmlContent.EndsWith( closingTags ) )
    {
        m_fullHtmlContent = m_fullHtmlContent.Left( m_fullHtmlContent.length() - closingTags.length() );
        m_fullHtmlContent += msgHtml + streamingDiv + closingTags;
    }

    // Save HTML snapshot for markdown re-rendering during streaming
    m_htmlBeforeAgentResponse = m_fullHtmlContent;

    // Full page re-render - this naturally scrolls to bottom due to flex-direction: column-reverse
    SetHtml( m_fullHtmlContent );

    // Clear Input and Update UI
    m_inputCtrl->Clear();
    m_actionButton->SetLabel( "Stop" );

    // Configure controller for this request (system prompt now handled server-side)
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
    m_htmlUpdatePending = false;

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
    wxLogInfo( "AGENT_FRAME::OnStop called" );
    using json = nlohmann::json;

    // Delegate cancel logic to controller
    if( m_chatController )
        m_chatController->Cancel();

    // Stop generating animation and compacting state
    StopGeneratingAnimation();
    m_isCompacting = false;

    // Cancel any in-progress async LLM request
    if( m_llmClient && m_llmClient->IsRequestInProgress() )
    {
        m_llmClient->CancelRequest();
    }

    // Signal to stop - affects tool execution loops and streaming callbacks (legacy)
    m_stopRequested = true;

    // Note: StopGeneratingAnimation() already did the final render without dots,
    // so we don't need to call UpdateAgentResponse() here (would restart timer with cleared state)

    // Sync frame's history from controller (controller handles orphaned tool_use blocks)
    if( m_chatController )
    {
        m_chatHistory = m_chatController->GetChatHistory();
        m_apiContext = m_chatController->GetApiContext();
        m_chatHistoryDb.Save( m_chatHistory );
    }

    // Clear uncommitted tool calls (haven't been added to history yet)
    if( m_pendingToolCalls.is_array() && !m_pendingToolCalls.empty() )
    {
        m_pendingToolCalls = json::array();
    }

    // Clear pending editor open request state (prevents stale approval button clicks)
    m_pendingOpenSch = false;
    m_pendingOpenPcb = false;
    m_pendingOpenToolId.clear();
    m_pendingOpenFilePath.Clear();

    // Transition state machine to IDLE
    m_conversationCtx.TransitionTo( AgentConversationState::IDLE );

    // Finalize the streaming content div so next response uses a fresh div
    if( m_chatWindow )
    {
        m_chatWindow->RunScriptAsync( "finalizeStreamingContent();" );
    }
    m_fullHtmlContent.Replace( "<div id=\"streaming-content\">", "<div>" );

    // Preserve thinking for index tracking
    if( !m_thinkingContent.IsEmpty() && m_currentThinkingIndex >= 0 )
    {
        m_historicalThinking.push_back( m_thinkingContent );
    }

    // Clear streaming state
    m_thinkingContent.Clear();
    m_thinkingHtml.Clear();
    m_toolCallHtml.Clear();
    m_currentThinkingIndex = -1;

    AppendHtml( "<br><p><i>(Stopped)</i></p>" );
    m_actionButton->SetLabel( "Send" );
}

void AGENT_FRAME::OnWebViewMessage( const wxString& aMessage )
{
    // Parse JSON message from JavaScript
    try
    {
        nlohmann::json msg = nlohmann::json::parse( aMessage.ToStdString() );
        std::string action = msg.value( "action", "" );
        wxLogInfo( "AGENT_FRAME::OnWebViewMessage - action: %s", action.c_str() );

        if( action == "link_click" )
        {
            wxString href = wxString::FromUTF8( msg.value( "href", "" ) );
            wxLogInfo( "AGENT_FRAME::OnWebViewMessage - link_click href: %s", href.ToStdString().c_str() );

            if( href == "agent:approve_open" )
            {
                OnApproveOpenEditor();
            }
            else if( href == "agent:reject_open" )
            {
                OnRejectOpenEditor();
            }
            else if( href.StartsWith( "toggle:thinking:" ) )
            {
                wxLogInfo( "AGENT_FRAME::OnWebViewMessage - toggle thinking block clicked" );
                // Toggle thinking block by index
                wxString indexStr = href.Mid( 16 );  // "toggle:thinking:" is 16 chars
                long index;

                if( indexStr.ToLong( &index ) && index >= 0 )
                {
                    // Check if this is the current streaming thinking
                    if( index == m_currentThinkingIndex && m_currentThinkingIndex >= 0 )
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
            else if( href.StartsWith( "http://" ) || href.StartsWith( "https://" ) )
            {
                // Open standard links in browser
                wxLaunchDefaultBrowser( href );
            }
        }
        else if( action == "copy" )
        {
            // Handle copy request from context menu
            wxString text = wxString::FromUTF8( msg.value( "text", "" ) );
            if( !text.IsEmpty() && wxTheClipboard->Open() )
            {
                wxTheClipboard->SetData( new wxTextDataObject( text ) );
                wxTheClipboard->Close();
            }
        }
        else if( action == "thinking_toggled" )
        {
            // Handle thinking toggle from JavaScript (no page reload needed)
            int index = msg.value( "index", -1 );
            bool expanded = msg.value( "expanded", false );

            // Update state and rebuild HTML so subsequent timer updates use correct state
            if( index == m_currentThinkingIndex && m_currentThinkingIndex >= 0 )
            {
                // Current streaming thinking - update state and rebuild HTML immediately
                m_thinkingExpanded = expanded;
                RebuildThinkingHtml();

                // CRITICAL: Immediately flush the update to DOM to prevent race condition
                // where THINKING_DELTA events rebuild with old state before next timer tick.
                // Force=true bypasses m_userScrolledUp check since this is user-initiated.
                FlushStreamingContentUpdate( true );
            }
            else if( index >= 0 && index < (int)m_historicalThinking.size() )
            {
                // Historical thinking
                if( expanded )
                    m_historicalThinkingExpanded.insert( index );
                else
                    m_historicalThinkingExpanded.erase( index );
            }
        }
        else if( action == "scroll_activity" )
        {
            // Handle scroll activity from JavaScript
            // Pause updates if actively scrolling OR if user has scrolled up (not at bottom)
            bool active = msg.value( "active", false );
            bool coupled = msg.value( "coupled", true );  // coupled=true means at bottom

            // Record timestamp of scroll activity for time-based debouncing
            m_lastScrollActivityMs = wxGetLocalTimeMillis().GetValue();

            // Pause updates if scrolling OR if user is scrolled up
            m_userScrolledUp = active || !coupled;
        }
    }
    catch( const std::exception& e )
    {
        // Log parse errors for debugging
        wxLogError( "AGENT: OnWebViewMessage parse error: %s", e.what() );
    }
}

void AGENT_FRAME::OnTextEnter( wxCommandEvent& aEvent )
{
    wxLogInfo( "AGENT_FRAME::OnTextEnter called" );
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
    wxLogInfo( "AGENT_FRAME::OnModelSelection called" );
    // Reload model context when model changes
    // Track the currently selected model
    wxString newModel = m_modelChoice->GetStringSelection();
    wxLogInfo( "AGENT_FRAME::OnModelSelection - selected model: %s", newModel.ToStdString().c_str() );
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
    // Ensure the target frame exists before sending the message
    // This is necessary because Kiway silently drops messages to non-existent frames
    KIWAY_PLAYER* targetPlayer = Kiway().Player( static_cast<FRAME_T>( aDestFrame ), true );
    if( !targetPlayer )
    {
        wxLogError( "AGENT: SendRequest failed - could not create target frame %d", aDestFrame );
        return "Error: Failed to create target frame for tool execution.";
    }

    // Log the request (truncate payload if too long)
    wxString payloadPreview = wxString::FromUTF8( aPayload.substr( 0, 200 ) );
    if( aPayload.length() > 200 )
        payloadPreview += "...";
    wxLogInfo( "AGENT: SendRequest to frame %d, payload: %s", aDestFrame, payloadPreview );

    // Use a sentinel value to distinguish "no response yet" from "empty response received"
    static const std::string NO_RESPONSE_SENTINEL = "\x01__NO_RESPONSE__\x01";
    m_toolResponse = NO_RESPONSE_SENTINEL;
    std::string payloadCopy = aPayload;

    Kiway().ExpressMail( static_cast<FRAME_T>( aDestFrame ), MAIL_AGENT_REQUEST, payloadCopy );

    // Wait for response (Sync)
    // We expect the target frame to reply via MAIL_AGENT_RESPONSE which sets m_toolResponse
    wxLongLong start = wxGetLocalTimeMillis();
    constexpr long TIMEOUT_MS = 10000;
    m_stopRequested = false;  // Reset stop flag
    while( m_toolResponse == NO_RESPONSE_SENTINEL && ( wxGetLocalTimeMillis() - start < TIMEOUT_MS ) )
    {
        wxYield(); // Process events (including the MailIn event and Stop button)
        if( m_stopRequested )
        {
            wxLogInfo( "AGENT: SendRequest cancelled by user after %lld ms",
                       ( wxGetLocalTimeMillis() - start ).GetValue() );
            return "Error: Tool execution cancelled by user.";
        }
        wxMilliSleep( 10 );
    }

    long elapsed = ( wxGetLocalTimeMillis() - start ).GetValue();
    bool timedOut = ( m_toolResponse == NO_RESPONSE_SENTINEL );
    bool emptyResponse = ( m_toolResponse.empty() );

    if( timedOut )
    {
        wxLogError( "AGENT: SendRequest TIMEOUT after %ld ms waiting for frame %d (no response received)",
                    elapsed, aDestFrame );
        return wxString::Format(
            "Error: Tool execution timed out after %ld ms (no response received).",
            elapsed ).ToStdString();
    }

    if( emptyResponse )
    {
        wxLogWarning( "AGENT: SendRequest got EMPTY response after %ld ms from frame %d",
                      elapsed, aDestFrame );
        return wxString::Format(
            "Error: Tool returned empty response after %ld ms.",
            elapsed ).ToStdString();
    }

    // Log successful response (truncate if too long)
    wxString responsePreview = wxString::FromUTF8( m_toolResponse.substr( 0, 200 ) );
    if( m_toolResponse.length() > 200 )
        responsePreview += "...";
    wxLogInfo( "AGENT: SendRequest got response after %ld ms: %s", elapsed, responsePreview );

    return m_toolResponse;
}

void AGENT_FRAME::OnChatRightClick( wxMouseEvent& aEvent )
{
    // Right-click now handled by JavaScript contextmenu event listener
    // See agent_html_template.cpp for implementation
    aEvent.Skip();
}

void AGENT_FRAME::OnPopupClick( wxCommandEvent& aEvent )
{
    // Copy now handled by JavaScript message passing
    // See OnWebViewMessage() for "copy" action handling
    aEvent.Skip();
}

// ============================================================================
// Native Tool Calling Implementation
// ============================================================================

void AGENT_FRAME::InitializeTools()
{
    m_tools = AgentTools::GetToolDefinitions();
}

// NOTE: ExecuteTool was removed - tools are now executed via CHAT_CONTROLLER::ExecuteNextTool()
// which sets the project path via the m_getProjectPathFn callback.

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

    // Filter out thinking blocks from API context before sending to API
    // (System prompt now handled server-side)
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
        wxLogInfo( "AGENT: Failed to start async LLM request" );
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
    wxLogInfo( "AGENT: OnLLMStreamComplete called" );

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
    wxLogInfo( "AGENT: OnLLMStreamError called" );

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
    wxLogInfo( "AGENT_FRAME::OnNewChat called" );
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

    // UI reset
    m_fullHtmlContent = GetAgentHtmlTemplate() + wxS( "</div></body></html>" );
    SetHtml( m_fullHtmlContent );
    m_chatHistoryDb.StartNewConversation();
    m_chatNameLabel->SetLabel( "New Chat" );

    // Clear historical thinking state
    m_historicalThinking.clear();
    m_historicalThinkingExpanded.clear();
    m_currentThinkingIndex = -1;
}


void AGENT_FRAME::OnHistoryTool( wxCommandEvent& aEvent )
{
    wxLogInfo( "AGENT_FRAME::OnHistoryTool called" );
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
    wxLogInfo( "AGENT_FRAME::OnHistoryShowAll called" );
    // Prevent switching chats while generating
    bool isBusy = m_chatController ? m_chatController->IsBusy()
                                   : ( m_isGenerating || m_conversationCtx.GetState() != AgentConversationState::IDLE );
    if( isBusy )
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
    wxLogInfo( "AGENT_FRAME::OnHistoryMenuSelect called" );
    // Prevent switching chats while generating
    bool isBusy = m_chatController ? m_chatController->IsBusy()
                                   : ( m_isGenerating || m_conversationCtx.GetState() != AgentConversationState::IDLE );
    if( isBusy )
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
    wxLogInfo( "AGENT_FRAME::LoadConversation called with id: %s", aConversationId.c_str() );
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

    // Build HTML from chat history with modern template
    m_fullHtmlContent = GetAgentHtmlTemplate();

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
                    "<div class=\"flex justify-end my-1.5\"><div class=\"bg-bg-tertiary py-2 px-3.5 rounded-lg max-w-[80%%] whitespace-pre-wrap\">%s</div></div>",
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
                            "<div class=\"flex justify-end my-1.5\"><div class=\"bg-bg-tertiary py-2 px-3.5 rounded-lg max-w-[80%%] whitespace-pre-wrap\">%s</div></div>",
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

                        // Always render both toggle link and content (content hidden by CSS)
                        // JavaScript will toggle visibility without page reload
                        wxString expandedClass = expanded ? " expanded" : "";
                        wxString displayStyle = expanded ? "block" : "none";

                        m_fullHtmlContent += wxString::Format(
                            "<a href=\"toggle:thinking:%d\" class=\"text-text-muted cursor-pointer no-underline hover:underline\" data-thinking-index=\"%d\">Thinking</a><br>"
                            "<div class=\"thinking-content text-[#606060] mt-2 pl-3 border-l-2 border-[#404040] whitespace-pre-wrap%s\" data-thinking-index=\"%d\" style=\"display:%s;\">%s</div><br>",
                            thinkingIndex, thinkingIndex, expandedClass, thinkingIndex, displayStyle, escapedText );
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
                    wxString statusClass;
                    wxString statusText;

                    if( isPythonError )
                    {
                        statusClass = "text-accent-red";
                        statusText = "Error";
                        wxString errorLine = ExtractPythonErrorLine( content );
                        displayResult = wxString::Format( "<i>%s</i>", errorLine );
                    }
                    else if( isError )
                    {
                        statusClass = "text-accent-red";
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
                        statusClass = "text-accent-green";
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
                        "<div class=\"bg-bg-secondary p-3 rounded-md my-3 max-w-full break-words\">"
                        "<span class=\"text-accent-green font-bold\"><strong>Tool Call:</strong></span> %s<br>"
                        "<span class=\"%s\"><strong>%s</strong></span><br>"
                        "<span class=\"text-text-secondary font-mono text-[13px] whitespace-pre-wrap break-words\">%s</span>"
                        "</div>",
                        desc, statusClass, statusText, displayResult );
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

    m_fullHtmlContent += "</div></body></html>";

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
    wxLogInfo( "AGENT_FRAME::OnSignIn called" );
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

void AGENT_FRAME::RefreshPendingChangesPanel()
{
    wxLogInfo( "AGENT_FRAME::RefreshPendingChangesPanel" );

    // The panel queries the editors directly and shows/hides itself
    m_pendingChangesPanel->Refresh();

    // Update button visibility to match panel state
    UpdatePendingChangesButtonVisibility();
}


void AGENT_FRAME::UpdatePendingChangesButtonVisibility()
{
    // Show/hide the toggle button based on whether the panel has content
    // When there are no changes, hide the button entirely
    bool hasChanges = m_pendingChangesPanel->IsShown();
    m_pendingChangesBtn->Show( hasChanges );

    // Set correct label based on panel visibility
    if( hasChanges )
    {
        m_pendingChangesBtn->SetLabel( "Hide Changes" );
    }

    Layout();
}


void AGENT_FRAME::OnPendingChangesClick( wxCommandEvent& aEvent )
{
    wxLogInfo( "AGENT_FRAME::OnPendingChangesClick called" );
    // Toggle panel visibility and update button label
    bool isShown = m_pendingChangesPanel->IsShown();
    m_pendingChangesPanel->Show( !isShown );
    m_pendingChangesBtn->SetLabel( isShown ? "Show Changes" : "Hide Changes" );
    Layout();
}


void AGENT_FRAME::OnTrackAgentClick( wxCommandEvent& aEvent )
{
    m_isTrackingAgent = !m_isTrackingAgent;

    nlohmann::json payload;
    payload["tracking"] = m_isTrackingAgent;
    std::string payloadStr = payload.dump();

    if( m_isTrackingAgent )
    {
        m_trackAgentBtn->SetLabel( "Tracking" );
    }
    else
    {
        m_trackAgentBtn->SetLabel( "Track" );
    }

    Kiway().ExpressMail( FRAME_SCH, MAIL_AGENT_TRACKING_MODE, payloadStr );
    Kiway().ExpressMail( FRAME_PCB_EDITOR, MAIL_AGENT_TRACKING_MODE, payloadStr );
}


void AGENT_FRAME::OnSchematicChangeHandled( bool aAccepted )
{
    AppendHtml( aAccepted ? "<p><i>Schematic changes accepted.</i></p>"
                          : "<p><i>Schematic changes rejected.</i></p>" );
    RefreshPendingChangesPanel();
}


void AGENT_FRAME::OnPcbChangeHandled( bool aAccepted )
{
    AppendHtml( aAccepted ? "<p><i>PCB changes accepted.</i></p>"
                          : "<p><i>PCB changes rejected.</i></p>" );
    RefreshPendingChangesPanel();
}


//=============================================================================
// Concurrent Editing Support
//=============================================================================

void AGENT_FRAME::SetAgentTargetSheet( const KIID& aSheetId, const wxString& aSheetName )
{
    m_agentWorkspace.SetTargetSheet( aSheetId );
}


void AGENT_FRAME::BeginAgentTransaction()
{
    if( m_agentWorkspace.BeginTransaction() )
    {
        wxLogInfo( "Agent transaction started" );

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
            wxLogInfo( "Agent transaction committed - pending approval" );
            // Changes are now staged and waiting for user approval
            // The pending changes panel will show them
        }
        else
        {
            wxLogInfo( "Agent transaction reverted" );
        }
    }
}


void AGENT_FRAME::OnConflictDetected( const KIID& aItemId, const CONFLICT_INFO& aInfo )
{
    wxLogInfo( "Conflict detected for item %s: %s",
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
    // Conflicts are now handled via the diff overlay in each editor
    // The pending changes panel just lists sheets with changes
}


//=============================================================================
// Editor Open Approval
//=============================================================================

void AGENT_FRAME::ShowOpenEditorApproval( const wxString& aEditorType )
{
    // Update m_toolCallHtml instead of using AppendHtml() so the approval UI
    // survives calls to UpdateAgentResponse()
    // Use onclick for JavaScript handling with modern CSS classes
    m_toolCallHtml = wxString::Format(
        "<br><br><div class=\"bg-bg-secondary p-3 rounded-md my-3 max-w-full break-words\">"
        "<span class=\"text-text-primary font-bold\"><b>Open %s Editor?</b></span> "
        "<a href=\"agent:approve_open\" class=\"text-[#00AA00] font-bold cursor-pointer no-underline hover:underline mx-2\">Open</a> "
        "<a href=\"agent:reject_open\" class=\"text-[#AA0000] font-bold cursor-pointer no-underline hover:underline mx-2\">Cancel</a>"
        "</div>",
        aEditorType );
    UpdateAgentResponse();
}


void AGENT_FRAME::OnApproveOpenEditor()
{
    wxLogInfo( "AGENT_FRAME::OnApproveOpenEditor called" );

    // Validate that we still have a pending request with a valid tool ID
    if( m_pendingOpenToolId.empty() )
    {
        wxLogWarning( "OnApproveOpenEditor: empty tool ID - ignoring stale click" );
        m_toolCallHtml.Clear();
        UpdateAgentResponse();
        return;
    }

    if( !m_pendingOpenSch && !m_pendingOpenPcb )
    {
        wxLogWarning( "OnApproveOpenEditor: no pending editor type - ignoring stale click" );
        m_pendingOpenToolId.clear();
        m_toolCallHtml.Clear();
        UpdateAgentResponse();
        return;
    }

    // Validate that the controller still has this tool pending
    if( m_chatController && !m_chatController->HasPendingTool( m_pendingOpenToolId ) )
    {
        wxLogWarning( "OnApproveOpenEditor: tool %s no longer pending - ignoring stale click",
                      m_pendingOpenToolId.c_str() );
        m_pendingOpenSch = false;
        m_pendingOpenPcb = false;
        m_pendingOpenToolId.clear();
        m_pendingOpenFilePath.Clear();
        m_toolCallHtml.Clear();
        UpdateAgentResponse();
        return;
    }

    // --- Original logic ---
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

    std::string result = success
        ? editorName.ToStdString() + " editor opened successfully"
        : "Failed to open " + editorName.ToStdString() + " editor";

    std::string toolId = m_pendingOpenToolId;
    m_pendingOpenSch = false;
    m_pendingOpenPcb = false;
    m_pendingOpenToolId.clear();

    if( m_chatController )
        m_chatController->HandleToolResult( toolId, result, success );
}


void AGENT_FRAME::OnRejectOpenEditor()
{
    wxLogInfo( "AGENT_FRAME::OnRejectOpenEditor called" );

    // Validate that we still have a pending request
    if( m_pendingOpenToolId.empty() )
    {
        wxLogWarning( "OnRejectOpenEditor: empty tool ID - ignoring stale click" );
        m_toolCallHtml.Clear();
        UpdateAgentResponse();
        return;
    }

    wxString editorName = m_pendingOpenSch ? "Schematic" : "PCB";
    std::string toolId = m_pendingOpenToolId;

    m_pendingOpenSch = false;
    m_pendingOpenPcb = false;
    m_pendingOpenToolId.clear();
    m_pendingOpenFilePath.Clear();

    // Validate controller still has this tool pending
    if( m_chatController && !m_chatController->HasPendingTool( toolId ) )
    {
        wxLogWarning( "OnRejectOpenEditor: tool %s no longer pending", toolId.c_str() );
        m_toolCallHtml.Clear();
        UpdateAgentResponse();
        return;
    }

    if( m_chatController )
        m_chatController->HandleToolResult( toolId,
            "User declined to open " + editorName.ToStdString() + " editor", false );
}


bool AGENT_FRAME::DoOpenEditor( FRAME_T aFrameType )
{
    KIWAY_PLAYER* player = Kiway().Player( aFrameType, true );
    if( !player )
        return false;

    // Open specific file if path was provided
    if( !m_pendingOpenFilePath.IsEmpty() )
    {
        std::vector<wxString> files;
        files.push_back( m_pendingOpenFilePath );
        player->OpenProjectFiles( files );
        m_pendingOpenFilePath.Clear();
    }

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

    // Markdown text is now streaming - hide the waiting dots
    m_isStreamingMarkdown = true;

    // Controller owns the response - UpdateAgentResponse reads from controller

    // Re-render full response with markdown
    UpdateAgentResponse();

    // Auto-scroll handled by CSS flex-direction: column-reverse

    delete data;
}


void AGENT_FRAME::OnChatThinkingStart( wxThreadEvent& aEvent )
{
    ChatThinkingStartData* data = aEvent.GetPayload<ChatThinkingStartData*>();

    // If there's existing thinking content from a previous block (e.g., before a tool call),
    // preserve it in history before starting the new block. This handles race conditions
    // where THINKING_START arrives before WAITING_FOR_LLM state change is processed.
    if( !m_thinkingContent.IsEmpty() && m_currentThinkingIndex >= 0 )
    {
        m_historicalThinking.push_back( m_thinkingContent );
        if( m_thinkingExpanded )
            m_historicalThinkingExpanded.insert( m_currentThinkingIndex );
    }

    // Initialize thinking state for new block
    m_isThinking = true;
    m_thinkingContent = "";
    m_thinkingExpanded = false;

    // Set index for this thinking block using m_historicalThinking.size()
    // After the push above (if any), this gives us the correct next index
    m_currentThinkingIndex = static_cast<int>( m_historicalThinking.size() );

    // Rebuild thinking HTML and immediately flush to DOM
    // This bypasses the timer to minimize delay before thinking link is clickable
    RebuildThinkingHtml();
    FlushStreamingContentUpdate();  // Immediate flush, don't wait for timer

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

    // Rebuild thinking HTML and trigger update via timer
    // The thinking content is included directly in BuildStreamingContent()
    // and will be updated on the next timer tick (max 50ms delay)
    RebuildThinkingHtml();
    UpdateAgentResponse();

    // Auto-scroll handled by CSS flex-direction: column-reverse

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


void AGENT_FRAME::OnChatToolGenerating( wxThreadEvent& aEvent )
{
    ChatToolGeneratingData* data = aEvent.GetPayload<ChatToolGeneratingData*>();
    if( !data )
        return;

    // Reset streaming markdown flag so tool name shows (not hidden by preceding text)
    m_isStreamingMarkdown = false;

    // Store tool name for display in generating animation
    m_generatingToolName = wxString::FromUTF8( data->toolName );

    wxLogInfo( "AGENT_FRAME::OnChatToolGenerating - tool: %s", data->toolName.c_str() );

    // Trigger UI update to show tool name
    UpdateAgentResponse();

    delete data;
}


void AGENT_FRAME::OnChatToolStart( wxThreadEvent& aEvent )
{
    ChatToolStartData* data = aEvent.GetPayload<ChatToolStartData*>();
    if( !data )
        return;

    wxLogInfo( "AGENT_FRAME::OnChatToolStart - tool: %s (id=%s)",
            data->toolName.c_str(), data->toolId.c_str() );

    // Clear generating tool name (tool is now executing, not generating)
    m_generatingToolName.Clear();

    // Stop animation and finalize thinking
    StopGeneratingAnimation();
    m_isThinking = false;

    // Finalize the current streaming div so the agent's response text stays in place
    if( m_chatWindow )
    {
        m_chatWindow->RunScriptAsync( "finalizeStreamingContent();" );
    }

    // Update m_fullHtmlContent to reflect finalized state (no more streaming-content ID)
    m_fullHtmlContent.Replace( "<div id=\"streaming-content\">", "<div>" );

    // Add a new streaming content div for tool UI and subsequent response
    wxString streamingDiv = wxS( "<div id=\"streaming-content\"></div>" );
    AppendHtml( streamingDiv );

    // Capture HTML state AFTER adding new streaming div
    m_htmlBeforeAgentResponse = m_fullHtmlContent;

    // Now clear streaming state in controller (text is baked into m_htmlBeforeAgentResponse)
    if( m_chatController )
        m_chatController->ClearStreamingState();

    // Preserve thinking content to history before clearing (needed for correct index tracking)
    if( !m_thinkingContent.IsEmpty() && m_currentThinkingIndex >= 0 )
    {
        m_historicalThinking.push_back( m_thinkingContent );
        if( m_thinkingExpanded )
            m_historicalThinkingExpanded.insert( m_currentThinkingIndex );
    }

    // Clear frame's thinking HTML since it's now part of the base HTML (prevents duplication)
    m_thinkingHtml.Clear();
    m_thinkingContent.Clear();

    // Store tool description for result display
    m_lastToolDesc = wxString::FromUTF8( data->description );

    // Handle open_editor specially - requires user approval only if not already open
    if( data->toolName == "open_editor" )
    {
        std::string editorType = data->input.value( "editor_type", "" );
        FRAME_T frameType = ( editorType == "sch" ) ? FRAME_SCH : FRAME_PCB_EDITOR;
        wxString editorLabel = ( editorType == "sch" ) ? "Schematic" : "PCB";

        // Capture optional file path
        std::string filePath = data->input.value( "file_path", "" );
        m_pendingOpenFilePath.Clear();

        // Validate file path if provided
        if( !filePath.empty() )
        {
            wxString projectPath = Kiway().Prj().GetProjectPath();
            auto pathResult = FileWriter::ValidatePathInProject( filePath, projectPath.ToStdString() );
            if( !pathResult.valid )
            {
                // Path validation failed - reject tool call
                if( m_chatController )
                    m_chatController->HandleToolResult( data->toolId,
                        "Error: " + pathResult.error, false );
                delete data;
                return;
            }
            m_pendingOpenFilePath = wxString::FromUTF8( pathResult.resolvedPath );
        }

        // Check if editor is already open (false = don't create if not existing)
        KIWAY_PLAYER* existingPlayer = Kiway().Player( frameType, false );
        if( existingPlayer && existingPlayer->IsShown() )
        {
            // Editor already open - just focus it and optionally open file
            if( existingPlayer->IsIconized() )
                existingPlayer->Iconize( false );
            existingPlayer->Raise();

            // If file path provided, open it in the existing editor
            if( !m_pendingOpenFilePath.IsEmpty() )
            {
                std::vector<wxString> files;
                files.push_back( m_pendingOpenFilePath );
                existingPlayer->OpenProjectFiles( files );
                m_pendingOpenFilePath.Clear();

                if( m_chatController )
                    m_chatController->HandleToolResult( data->toolId,
                        editorLabel.ToStdString() + " editor opened file: " + filePath, true );
            }
            else
            {
                // Send success result immediately
                if( m_chatController )
                    m_chatController->HandleToolResult( data->toolId,
                        editorLabel.ToStdString() + " editor is already open", true );
            }

            delete data;
            return;
        }

        // Editor not open - store pending request and show approval dialog
        m_pendingOpenSch = ( editorType == "sch" );
        m_pendingOpenPcb = ( editorType == "pcb" );
        m_pendingOpenToolId = data->toolId;

        ShowOpenEditorApproval( editorLabel );

        delete data;
        return;
    }

    // Handle check_status - returns project and editor state
    if( data->toolName == "check_status" )
    {
        nlohmann::json status;

        // Project info
        wxString projectPath = Kiway().Prj().GetProjectPath();
        status["project_path"] = projectPath.ToStdString();

        // Check which editors are open
        KIWAY_PLAYER* schEditor = Kiway().Player( FRAME_SCH, false );
        KIWAY_PLAYER* pcbEditor = Kiway().Player( FRAME_PCB_EDITOR, false );

        status["schematic_editor_open"] = ( schEditor && schEditor->IsShown() );
        status["pcb_editor_open"] = ( pcbEditor && pcbEditor->IsShown() );

        // Add project file paths
        wxString prjPath = Kiway().Prj().GetProjectPath();
        if( !prjPath.empty() )
        {
            wxString prjName = Kiway().Prj().GetProjectName();
            status["schematic_file"] = ( prjPath + prjName + ".kicad_sch" ).ToStdString();
            status["pcb_file"] = ( prjPath + prjName + ".kicad_pcb" ).ToStdString();
        }

        // Get current sheet if schematic is open (via IPC would be more accurate, but this is fast)
        status["current_sheet"] = ""; // Would need IPC call for sch.sheets.get_current()

        if( m_chatController )
            m_chatController->HandleToolResult( data->toolId, status.dump( 2 ), true );

        delete data;
        return;
    }

    // Handle close_editor - close schematic or PCB editor
    if( data->toolName == "close_editor" )
    {
        std::string editorType = data->input.value( "editor_type", "" );
        bool saveFirst = data->input.value( "save_first", true );
        FRAME_T frameType = ( editorType == "sch" ) ? FRAME_SCH : FRAME_PCB_EDITOR;
        wxString editorLabel = ( editorType == "sch" ) ? "Schematic" : "PCB";

        KIWAY_PLAYER* player = Kiway().Player( frameType, false );
        if( !player || !player->IsShown() )
        {
            if( m_chatController )
                m_chatController->HandleToolResult( data->toolId,
                    editorLabel.ToStdString() + " editor is not open", true );
            delete data;
            return;
        }

        // Close the editor (it will prompt to save if there are unsaved changes)
        player->Close( !saveFirst ); // If saveFirst is false, force close without prompt

        if( m_chatController )
            m_chatController->HandleToolResult( data->toolId,
                editorLabel.ToStdString() + " editor closed" + ( saveFirst ? " (saved)" : "" ), true );

        delete data;
        return;
    }

    // Handle save - save current documents
    // Note: Saving is better done via IPC tools which have direct access to the editor APIs.
    // This is a simplified version that just reports status.
    if( data->toolName == "save" )
    {
        std::string editorType = data->input.value( "editor_type", "all" );
        nlohmann::json result;
        result["status"] = "info";
        result["message"] = "Use IPC tools for saving. Schematic: sch.save(), PCB: pcb.save()";

        std::vector<std::string> openEditors;
        KIWAY_PLAYER* schEditor = Kiway().Player( FRAME_SCH, false );
        KIWAY_PLAYER* pcbEditor = Kiway().Player( FRAME_PCB_EDITOR, false );

        if( schEditor && schEditor->IsShown() )
            openEditors.push_back( "schematic" );
        if( pcbEditor && pcbEditor->IsShown() )
            openEditors.push_back( "pcb" );

        result["open_editors"] = openEditors;

        if( m_chatController )
            m_chatController->HandleToolResult( data->toolId, result.dump( 2 ), true );

        delete data;
        return;
    }

    // Handle create_project - create new KiCad project
    if( data->toolName == "create_project" )
    {
        std::string projectName = data->input.value( "project_name", "" );
        std::string directory = data->input.value( "directory", "" );

        if( projectName.empty() || directory.empty() )
        {
            if( m_chatController )
                m_chatController->HandleToolResult( data->toolId,
                    "Error: project_name and directory are required", false );
            delete data;
            return;
        }

        // Create project directory
        wxString projDir = wxString::FromUTF8( directory ) + wxFileName::GetPathSeparator() +
                           wxString::FromUTF8( projectName );

        if( !wxDir::Make( projDir, wxS_DIR_DEFAULT ) && !wxDir::Exists( projDir ) )
        {
            if( m_chatController )
                m_chatController->HandleToolResult( data->toolId,
                    "Error: Could not create project directory: " + projDir.ToStdString(), false );
            delete data;
            return;
        }

        wxString basePath = projDir + wxFileName::GetPathSeparator() + wxString::FromUTF8( projectName );

        // Create minimal .kicad_pro file
        wxString proFile = basePath + ".kicad_pro";
        {
            wxFile f( proFile, wxFile::write );
            if( f.IsOpened() )
            {
                nlohmann::json proJson = {
                    { "meta", { { "filename", projectName + ".kicad_pro" }, { "version", 1 } } },
                    { "schematic", { { "legacy_lib_dir", "" }, { "legacy_lib_list", nlohmann::json::array() } } }
                };
                f.Write( wxString::FromUTF8( proJson.dump( 2 ) ) );
            }
        }

        // Create minimal .kicad_sch file
        wxString schFile = basePath + ".kicad_sch";
        {
            wxFile f( schFile, wxFile::write );
            if( f.IsOpened() )
            {
                f.Write(
                    "(kicad_sch\n"
                    "  (version 20250114)\n"
                    "  (generator \"zener_agent\")\n"
                    "  (generator_version \"1.0\")\n"
                    "  (uuid \"" + KIID().AsStdString() + "\")\n"
                    "  (paper \"A4\")\n"
                    "  (lib_symbols)\n"
                    "  (sheet_instances\n"
                    "    (path \"/\" (page \"\"))\n"
                    "  )\n"
                    ")\n"
                );
            }
        }

        // Create minimal .kicad_pcb file
        wxString pcbFile = basePath + ".kicad_pcb";
        {
            wxFile f( pcbFile, wxFile::write );
            if( f.IsOpened() )
            {
                f.Write(
                    "(kicad_pcb\n"
                    "  (version 20250114)\n"
                    "  (generator \"zener_agent\")\n"
                    "  (generator_version \"1.0\")\n"
                    "  (general\n"
                    "    (thickness 1.6)\n"
                    "    (legacy_teardrops no)\n"
                    "  )\n"
                    "  (paper \"A4\")\n"
                    "  (layers\n"
                    "    (0 \"F.Cu\" signal)\n"
                    "    (31 \"B.Cu\" signal)\n"
                    "    (32 \"B.Adhes\" user \"B.Adhesive\")\n"
                    "    (33 \"F.Adhes\" user \"F.Adhesive\")\n"
                    "    (34 \"B.Paste\" user)\n"
                    "    (35 \"F.Paste\" user)\n"
                    "    (36 \"B.SilkS\" user \"B.Silkscreen\")\n"
                    "    (37 \"F.SilkS\" user \"F.Silkscreen\")\n"
                    "    (38 \"B.Mask\" user)\n"
                    "    (39 \"F.Mask\" user)\n"
                    "    (40 \"Dwgs.User\" user \"User.Drawings\")\n"
                    "    (44 \"Edge.Cuts\" user)\n"
                    "  )\n"
                    "  (setup\n"
                    "    (pad_to_mask_clearance 0)\n"
                    "  )\n"
                    ")\n"
                );
            }
        }

        nlohmann::json result = {
            { "status", "success" },
            { "project_path", projDir.ToStdString() },
            { "files_created", {
                projectName + ".kicad_pro",
                projectName + ".kicad_sch",
                projectName + ".kicad_pcb"
            }}
        };

        if( m_chatController )
            m_chatController->HandleToolResult( data->toolId, result.dump( 2 ), true );

        delete data;
        return;
    }

    // Generate tool call HTML with "Running..." status
    m_toolCallHtml = wxString::Format(
        "<div class=\"bg-bg-secondary p-3 rounded-md my-3 max-w-full break-words\">"
        "<span class=\"text-accent-green font-bold\"><strong>Tool Call:</strong></span> %s<br>"
        "<span class=\"text-text-secondary font-mono text-[13px] whitespace-pre-wrap break-words\"><i>Running...</i></span>"
        "</div>",
        m_lastToolDesc );

    UpdateAgentResponse();
    // Auto-scroll handled by CSS flex-direction: column-reverse

    delete data;
}


void AGENT_FRAME::OnChatToolComplete( wxThreadEvent& aEvent )
{
    ChatToolCompleteData* data = aEvent.GetPayload<ChatToolCompleteData*>();
    if( !data )
        return;

    wxLogInfo( "AGENT_FRAME::OnChatToolComplete - tool: %s, success: %s",
            data->toolName.c_str(), data->success ? "true" : "false" );
    // Determine status display
    wxString statusClass;
    wxString statusText;
    wxString displayResult;

    if( data->isPythonError )
    {
        statusClass = "text-accent-red";
        statusText = "Error";

        // Extract just the error line (e.g., "AttributeError: ...")
        wxString errorLine = ExtractPythonErrorLine( data->result );
        displayResult = wxString::Format( "<i>%s</i>", errorLine );
    }
    else if( !data->success )
    {
        statusClass = "text-accent-red";
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
        statusClass = "text-accent-green";
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
        "<div class=\"bg-bg-secondary p-3 rounded-md my-3 max-w-full break-words\">"
        "<span class=\"text-accent-green font-bold\"><strong>Tool Call:</strong></span> %s<br>"
        "<span class=\"%s\"><strong>%s</strong></span><br>"
        "<span class=\"text-text-secondary font-mono text-[13px] whitespace-pre-wrap break-words\">%s</span>"
        "</div>",
        m_lastToolDesc, statusClass, statusText, displayResult );

    // After schematic tools complete successfully, trigger editor refresh for live UI feedback
    if( data->success && data->toolName == "sch_modify" )
    {
        try
        {
            nlohmann::json resultJson = nlohmann::json::parse( data->result );
            if( resultJson.value( "success", false ) )
            {
                std::string filePath = resultJson.value( "file", "" );
                if( !filePath.empty() )
                {
                    // Tell schematic editor to reload this file and refresh display
                    Kiway().ExpressMail( FRAME_SCH, MAIL_SCH_REFRESH, filePath );
                }
            }
        }
        catch( ... )
        {
            // JSON parse failed - tool result may not be in expected format, skip refresh
        }
    }

    // Check for pending approval
    RefreshPendingChangesPanel();

    // Auto-follow on tool complete if tracking is active
    if( m_isTrackingAgent && data->success )
    {
        // Send to both editors - each will check if it has changes to show
        std::string emptyPayload;
        Kiway().ExpressMail( FRAME_SCH, MAIL_AGENT_VIEW_CHANGES, emptyPayload );
        Kiway().ExpressMail( FRAME_PCB_EDITOR, MAIL_AGENT_VIEW_CHANGES, emptyPayload );
    }

    UpdateAgentResponse();
    // Auto-scroll handled by CSS flex-direction: column-reverse

    delete data;
}


void AGENT_FRAME::OnChatTurnComplete( wxThreadEvent& aEvent )
{
    ChatTurnCompleteData* data = aEvent.GetPayload<ChatTurnCompleteData*>();
    if( !data )
        return;

    bool continuing = data->continuing;
    wxLogInfo( "AGENT_FRAME::OnChatTurnComplete - turn completed (continuing=%d)", continuing );

    // If not continuing, stop animation and update button
    if( !continuing )
    {
        StopGeneratingAnimation();
        m_actionButton->SetLabel( "Send" );
    }

    // Finalize thinking state
    m_isThinking = false;

    // Sync history from controller (controller added the assistant message)
    if( m_chatController )
    {
        m_chatHistory = m_chatController->GetChatHistory();
        m_apiContext = m_chatController->GetApiContext();
        if( !continuing )
        {
            m_chatHistoryDb.Save( m_chatHistory );
        }
    }

    // Preserve thinking content for index tracking (so next message gets index+1)
    if( !m_thinkingContent.IsEmpty() && m_currentThinkingIndex >= 0 )
    {
        m_historicalThinking.push_back( m_thinkingContent );
    }

    // Preserve thinking expansion state
    if( m_thinkingExpanded && m_currentThinkingIndex >= 0 )
        m_historicalThinkingExpanded.insert( m_currentThinkingIndex );

    // Finalize the streaming content div - remove its ID so future streams use a fresh div
    if( m_chatWindow )
    {
        m_chatWindow->RunScriptAsync( "finalizeStreamingContent();" );
    }

    // Update m_fullHtmlContent to match DOM state (remove the id from streaming div)
    m_fullHtmlContent.Replace( "<div id=\"streaming-content\">", "<div>" );

    // If continuing, add a new streaming div for the next response
    if( continuing )
    {
        wxString streamingDiv = wxS( "<div id=\"streaming-content\"></div>" );
        AppendHtml( streamingDiv );
    }

    // Clear streaming UI state (content is already in DOM via streaming updates)
    m_currentResponse.clear();
    m_thinkingContent.Clear();
    m_toolCallHtml.Clear();
    m_thinkingExpanded = false;
    m_currentThinkingIndex = -1;

    // NOTE: Don't call RenderChatHistory() here - content is already in DOM from streaming.
    // RenderChatHistory() is only for loading saved conversations from disk.
    // Calling it here would cause a full SetPage() reload which jerks scroll position.

    delete data;
}


void AGENT_FRAME::OnChatError( wxThreadEvent& aEvent )
{
    wxLogInfo( "AGENT_FRAME::OnChatError - error received" );
    ChatErrorData* data = aEvent.GetPayload<ChatErrorData*>();
    if( !data )
        return;

    // Display error message
    wxString errorHtml = wxString::Format(
        "<p><font color='red'><b>Error:</b> %s</font></p>",
        wxString::FromUTF8( data->message ) );
    AppendHtml( errorHtml );

    // Stop all animations and reset button
    StopGeneratingAnimation();
    m_isCompacting = false;
    m_actionButton->SetLabel( "Send" );

    // Clear streaming state
    m_isThinking = false;

    // Sync history from controller - it may have removed orphaned user messages
    if( m_chatController )
    {
        m_chatHistory = m_chatController->GetChatHistory();
        m_apiContext = m_chatController->GetApiContext();
    }

    // Sync frame's state machine with controller (now IDLE after error recovery)
    m_conversationCtx.SetState( AgentConversationState::IDLE );

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

        // If continuing after tool completion, bake in tool result before starting new stream
        // This ensures the tool call result appears BEFORE any new response text
        if( static_cast<AgentConversationState>( data->oldState ) ==
            AgentConversationState::PROCESSING_TOOL_RESULT )
        {
            // IMMEDIATE render before clearing (don't wait for timer)
            // Build the streaming content that should be baked in
            wxString streamingContent;
            if( !m_thinkingHtml.IsEmpty() )
                streamingContent += m_thinkingHtml;
            std::string currentResponse = m_chatController ? m_chatController->GetCurrentResponse() : "";
            streamingContent += AgentMarkdown::ToHtml( currentResponse );
            if( !m_toolCallHtml.IsEmpty() )
                streamingContent += m_toolCallHtml;

            // Send immediate DOM update via JavaScript
            if( m_chatWindow && !streamingContent.IsEmpty() )
            {
                wxString escaped = streamingContent;
                escaped.Replace( "\\", "\\\\" );
                escaped.Replace( "'", "\\'" );
                escaped.Replace( "\n", "\\n" );
                escaped.Replace( "\r", "\\r" );
                m_chatWindow->RunScriptAsync(
                    wxString::Format( "updateStreamingContent('%s');", escaped ) );
            }

            // Update m_fullHtmlContent to match the DOM state
            // Remove any empty streaming divs to prevent accumulation, then append the filled one
            wxString fullHtml = m_htmlBeforeAgentResponse;
            const wxString closingTags = wxS( "</div></body></html>" );

            // Remove closing tags
            if( fullHtml.EndsWith( closingTags ) )
                fullHtml = fullHtml.Left( fullHtml.length() - closingTags.length() );

            // Remove any empty streaming content divs (prevents accumulation)
            fullHtml.Replace( wxS( "<div id=\"streaming-content\"></div>" ), wxS( "" ) );

            // Append the filled streaming div and restore closing tags
            fullHtml += wxS( "<div id=\"streaming-content\">" ) + streamingContent + wxS( "</div>" );
            fullHtml += closingTags;
            m_fullHtmlContent = fullHtml;

            // Finalize this streaming div (tool result now "baked in")
            if( m_chatWindow )
                m_chatWindow->RunScriptAsync( "finalizeStreamingContent();" );
            m_fullHtmlContent.Replace( "<div id=\"streaming-content\">", "<div>" );

            // Add fresh streaming div for next content
            wxString streamingDiv = wxS( "<div id=\"streaming-content\"></div>" );
            AppendHtml( streamingDiv );
            m_htmlBeforeAgentResponse = m_fullHtmlContent;

            // Preserve thinking content in history before clearing
            // This ensures the next thinking block gets a new index
            if( !m_thinkingContent.IsEmpty() && m_currentThinkingIndex >= 0 )
            {
                m_historicalThinking.push_back( m_thinkingContent );
                // Preserve expansion state too
                if( m_thinkingExpanded )
                    m_historicalThinkingExpanded.insert( m_currentThinkingIndex );
            }

            // NOW safe to clear - content is baked into DOM
            m_toolCallHtml.Clear();
            m_thinkingHtml.Clear();
            m_thinkingContent.Clear();
            m_thinkingExpanded = false;
            if( m_chatController )
                m_chatController->ClearStreamingState();
            m_htmlUpdateNeeded = false;

            // Update thinking index so next thinking block gets a new index
            // Now m_historicalThinking.size() reflects the pushed content
            m_currentThinkingIndex = static_cast<int>( m_historicalThinking.size() );
        }

        StartGeneratingAnimation();
        break;

    case AgentConversationState::TOOL_USE_DETECTED:
    case AgentConversationState::EXECUTING_TOOL:
    case AgentConversationState::PROCESSING_TOOL_RESULT:
        // Keep current button state during tool execution
        break;
    }

    delete data;
}


void AGENT_FRAME::OnChatTitleDelta( wxThreadEvent& aEvent )
{
    ChatTitleDeltaData* data = aEvent.GetPayload<ChatTitleDeltaData*>();
    if( !data )
        return;

    // Update title display with partial text (streaming animation)
    m_chatNameLabel->SetLabel( wxString::FromUTF8( data->partialTitle ) );

    delete data;
}


void AGENT_FRAME::OnChatTitleGenerated( wxThreadEvent& aEvent )
{
    ChatTitleGeneratedData* data = aEvent.GetPayload<ChatTitleGeneratedData*>();
    if( !data )
        return;

    wxLogInfo( "AGENT_FRAME::OnChatTitleGenerated - title: %s", data->title.c_str() );
    // Update title display
    m_chatNameLabel->SetLabel( wxString::FromUTF8( data->title ) );

    // Update persistence with the new title
    m_chatHistoryDb.SetTitle( data->title );

    if( m_chatController )
    {
        m_chatHistoryDb.Save( m_chatController->GetChatHistory() );
    }

    // Refresh history panel if it's open
    if( m_historyPanel && m_historyPanel->IsShown() )
    {
        m_historyPanel->RefreshHistory();
    }

    delete data;
}


void AGENT_FRAME::OnChatHistoryLoaded( wxThreadEvent& aEvent )
{
    ChatHistoryLoadedData* data = aEvent.GetPayload<ChatHistoryLoadedData*>();
    if( !data )
        return;

    wxLogInfo( "AGENT_FRAME::OnChatHistoryLoaded - chatId: %s, title: %s",
            data->chatId.c_str(), data->title.c_str() );
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

    // Clear historical thinking toggle state for new history
    m_historicalThinkingExpanded.clear();
    m_currentThinkingIndex = -1;

    // Render the loaded chat history
    RenderChatHistory();

    // Update DB ID so new messages go to this history
    m_chatHistoryDb.SetConversationId( data->chatId );

    // Auto-scroll to bottom handled by CSS flex-direction: column-reverse
    m_userScrolledUp = false;

    delete data;
}
