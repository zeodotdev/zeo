#include "agent_frame.h"
#include "agent_thread.h"
#include <kiway_express.h>
#include <mail_type.h>
#include <wx/log.h>
#include <kiway.h>
#include <sstream>
#include <fstream>
#include <wx/sizer.h>
#include <wx/textctrl.h>
#include <wx/msgdlg.h>
#include <wx/stdpaths.h>
#include <wx/filename.h>
#include <bitmaps.h>
#include <id.h>
#include <nlohmann/json.hpp>
#include <wx/settings.h>
#include <wx/clipbrd.h>
#include <wx/menu.h>

BEGIN_EVENT_TABLE( AGENT_FRAME, KIWAY_PLAYER )
EVT_MENU( wxID_EXIT, AGENT_FRAME::OnExit )
END_EVENT_TABLE()

AGENT_FRAME::AGENT_FRAME( KIWAY* aKiway, wxWindow* aParent ) :
        KIWAY_PLAYER( aKiway, aParent, FRAME_AGENT, "Agent", wxDefaultPosition, wxDefaultSize, wxDEFAULT_FRAME_STYLE,
                      "agent_frame_name", schIUScale ),
        m_workerThread( nullptr )
{
    // --- UI Layout ---
    // Top: Chat History (Expandable)
    // Bottom: Input Container (Unified)

    wxBoxSizer* mainSizer = new wxBoxSizer( wxVERTICAL );

    // 1. Chat History Area
    m_chatWindow =
            new wxHtmlWindow( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxHW_SCROLLBAR_AUTO | wxBORDER_NONE );
    // Set a default page content or styling if needed
    m_fullHtmlContent = "<html><body bgcolor='#1E1E1E' text='#FFFFFF'><p>Welcome to KiCad Agent.</p></body></html>";
    m_chatWindow->SetPage( m_fullHtmlContent );
    mainSizer->Add( m_chatWindow, 1, wxEXPAND | wxALL, 0 ); // Remove ALL padding for clean edge

    // 2. Input Container (Unified Look)
    // Create m_inputPanel for dark styling
    m_inputPanel = new wxPanel( this, wxID_ANY );
    m_inputPanel->SetBackgroundColour( wxColour( "#1E1E1E" ) ); // 1E1E1E

    // Use a vertical BoxSizer for the panel
    wxBoxSizer* outerInputSizer = new wxBoxSizer( wxVERTICAL );

    // Use an inner sizer for content padding
    wxBoxSizer* inputContainerSizer = new wxBoxSizer( wxVERTICAL );

    // Status Pill (Selection Info / Add Context)
    m_selectionPill = new wxButton( m_inputPanel, wxID_ANY, "No Selection", wxDefaultPosition, wxDefaultSize );
    // Removed custom colors to keep native round look
    m_selectionPill->Hide(); // Hide on load
    // Match vertical spacing of bottom buttons (approx 5px padding)
    inputContainerSizer->Add( m_selectionPill, 0, wxALIGN_LEFT | wxBOTTOM, 5 );

    // 2a. Text Input (Top)
    m_inputCtrl = new wxTextCtrl( m_inputPanel, wxID_ANY, "", wxDefaultPosition, wxSize( -1, 80 ),
                                  wxTE_MULTILINE | wxTE_PROCESS_ENTER | wxTE_RICH2 | wxBORDER_NONE );
    m_inputCtrl->SetBackgroundColour( wxColour( "#1E1E1E" ) );
    m_inputCtrl->SetForegroundColour( wxColour( "#FFFFFF" ) );

    // Sync Font with Buttons
    // We can't easily get the button font before creating buttons, but we can get system font
    wxFont font = wxSystemSettings::GetFont( wxSYS_DEFAULT_GUI_FONT );
    font.SetPointSize( 14 ); // Increased to 14
    m_inputCtrl->SetFont( font );

    inputContainerSizer->Add( m_inputCtrl, 1, wxEXPAND | wxBOTTOM, 5 );

    // 2b. Control Row (Bottom)
    wxBoxSizer* controlsSizer = new wxBoxSizer( wxHORIZONTAL );

    // Plus Button
    m_plusButton = new wxButton( m_inputPanel, wxID_ANY, "+", wxDefaultPosition, wxSize( 30, -1 ) );
    controlsSizer->Add( m_plusButton, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5 );

    // Model Selection
    wxArrayString modelChoices;
    modelChoices.Add( "Claude 4.5 Opus" );
    modelChoices.Add( "GPT-4o" );

    m_modelChoice = new wxChoice( m_inputPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, modelChoices );
    m_modelChoice->SetSelection( 0 ); // Default to Claude 4.5 Opus
    controlsSizer->Add( m_modelChoice, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5 );

    // Spacer
    controlsSizer->AddStretchSpacer();

    // Send Button
    m_actionButton = new wxButton( m_inputPanel, wxID_ANY, "->" ); // Arrow icon would be better
    controlsSizer->Add( m_actionButton, 0, wxALIGN_CENTER_VERTICAL );

    inputContainerSizer->Add( controlsSizer, 0, wxEXPAND );

    // Add inner sizer to outer sizer with padding
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

    // Bind Events
    m_actionButton->Bind( wxEVT_BUTTON, &AGENT_FRAME::OnSend, this );
    m_inputCtrl->Bind( wxEVT_TEXT_ENTER, &AGENT_FRAME::OnTextEnter, this );
    m_selectionPill->Bind( wxEVT_BUTTON, &AGENT_FRAME::OnSelectionPillClick, this );
    // m_toolButton->Bind( wxEVT_BUTTON, &AGENT_FRAME::OnToolClick, this );
    m_chatWindow->Bind( wxEVT_HTML_LINK_CLICKED, &AGENT_FRAME::OnHtmlLinkClick, this );
    m_chatWindow->Bind( wxEVT_RIGHT_DOWN, &AGENT_FRAME::OnChatRightClick, this );
    Bind( wxEVT_MENU, &AGENT_FRAME::OnPopupClick, this, ID_CHAT_COPY );
    m_inputCtrl->Bind( wxEVT_KEY_DOWN, &AGENT_FRAME::OnInputKeyDown, this );
    m_inputCtrl->Bind( wxEVT_TEXT, &AGENT_FRAME::OnInputText, this );

    // Bind Thread Events
    Bind( wxEVT_AGENT_UPDATE, &AGENT_FRAME::OnAgentUpdate, this );
    Bind( wxEVT_AGENT_COMPLETE, &AGENT_FRAME::OnAgentComplete, this );

    // Bind Async Tool Execution Events
    Bind( EVT_TOOL_EXECUTION_COMPLETE, &AGENT_FRAME::OnToolExecutionComplete, this );
    Bind( EVT_TOOL_EXECUTION_ERROR, &AGENT_FRAME::OnToolExecutionError, this );
    Bind( EVT_TOOL_EXECUTION_PROGRESS, &AGENT_FRAME::OnToolExecutionProgress, this );
    m_toolTimeoutTimer.Bind( wxEVT_TIMER, &AGENT_FRAME::OnToolExecutionTimeout, this );

    // Bind Async LLM Streaming Events
    Bind( EVT_LLM_STREAM_CHUNK, &AGENT_FRAME::OnLLMStreamChunk, this );
    Bind( EVT_LLM_STREAM_COMPLETE, &AGENT_FRAME::OnLLMStreamComplete, this );
    Bind( EVT_LLM_STREAM_ERROR, &AGENT_FRAME::OnLLMStreamError, this );

    // Bind Model Change Event
    m_modelChoice->Bind( wxEVT_CHOICE, &AGENT_FRAME::OnModelSelection, this );

    // Initialize History
    m_chatHistory = nlohmann::json::array();
    m_pendingToolCalls = nlohmann::json::array();

    // Initialize LLM client and tools
    m_llmClient = std::make_unique<AGENT_LLM_CLIENT>( this );
    InitializeTools();

    // Load model context (API reference)
    LoadModelContext();
}

AGENT_FRAME::~AGENT_FRAME()
{
}

void AGENT_FRAME::LoadModelContext()
{
    // Determine context file based on current model
    // For now, all models use the same kipy schematic API reference
    wxString contextPath = wxStandardPaths::Get().GetExecutablePath();
    wxFileName exePath( contextPath );

    // Navigate to model_context folder relative to executable
    // In dev: executable is in build folder, context is in source
    // Try multiple paths
    std::vector<std::string> searchPaths;

    // 1. Environment variable for dev path
    const char* devPath = std::getenv( "KICAD_AGENT_DEV_PATH" );
    if( devPath )
    {
        searchPaths.push_back( std::string( devPath ) + "/../code/kicad-agent/agent/model_context/kipy_schematic_api_v4.txt" );
    }

    // 2. Relative to executable - search up directory tree for source location
    //    This handles various build configurations where:
    //    - Executable may be in: .../code/kicad-agent/build/...
    //    - Source is in: .../code/kicad-agent/agent/model_context/
    wxFileName searchPath( exePath.GetPath(), "" );
    for( int i = 0; i < 6; i++ )  // Search up to 6 levels up
    {
        wxFileName testPath = searchPath;
        testPath.AppendDir( "agent" );
        testPath.AppendDir( "model_context" );
        testPath.SetFullName( "kipy_schematic_api_v4.txt" );
        searchPaths.push_back( std::string( testPath.GetFullPath().mb_str() ) );

        // Also try kicad-agent/agent/model_context path
        wxFileName altPath = searchPath;
        altPath.AppendDir( "kicad-agent" );
        altPath.AppendDir( "agent" );
        altPath.AppendDir( "model_context" );
        altPath.SetFullName( "kipy_schematic_api_v4.txt" );
        searchPaths.push_back( std::string( altPath.GetFullPath().mb_str() ) );

        searchPath.RemoveLastDir();
    }

    // 3. Relative to executable (for installed builds - macOS app bundle)
    // When running standalone from agent.app/Contents/MacOS/agent
    searchPaths.push_back( std::string( exePath.GetPath().mb_str() ) + "/../Resources/model_context/kipy_schematic_api_v4.txt" );

    // 4. When agent runs as KIFACE loaded by main KiCad process
    // The executable is KiCad.app/Contents/MacOS/kicad
    // The model_context is installed to KiCad.app/Contents/SharedSupport/agent/model_context/
    searchPaths.push_back( std::string( exePath.GetPath().mb_str() ) + "/../SharedSupport/agent/model_context/kipy_schematic_api_v4.txt" );

    // 5. Relative to executable (for installed builds - Linux/Windows)
    searchPaths.push_back( std::string( exePath.GetPath().mb_str() ) + "/../share/kicad-agent/model_context/kipy_schematic_api_v4.txt" );

    // Try each path
    m_modelContext = "";
    fprintf( stderr, "AGENT: Searching for model context file...\n" );
    for( const auto& path : searchPaths )
    {
        fprintf( stderr, "AGENT: Trying path: %s\n", path.c_str() );
        std::ifstream file( path );
        if( file.is_open() )
        {
            std::stringstream buffer;
            buffer << file.rdbuf();
            m_modelContext = buffer.str();
            file.close();
            fprintf( stderr, "AGENT: Found model context at: %s (%zu bytes)\n", path.c_str(), m_modelContext.size() );
            break;
        }
    }
    if( m_modelContext.empty() )
    {
        fprintf( stderr, "AGENT: WARNING - Model context file not found!\n" );
    }
}

std::string AGENT_FRAME::GetSystemPrompt()
{
    std::string systemPrompt = R"(You are a helpful assistant for KiCad PCB design.
You have access to tools to interact with schematics and boards.

AVAILABLE TOOLS:

1. run_shell
   - Execute Python code in the KiCad IPC shell
   - Parameters: mode ("sch" or "pcb"), code (Python code string)
   - For schematic: sch object, kipy, and Vector2 are pre-imported
   - For board: board object, kipy, and Vector2 are pre-imported
   - Use Vector2.from_xy_mm(x, y) for positions in millimeters

2. run_terminal
   - Execute bash/shell commands
   - Parameters: command (string)
   - Use for file operations, git commands, etc.

GUIDELINES:
- Use tools when you need to interact with the schematic or board
- Wait for tool results before proceeding with dependent operations
- Be concise in your explanations
)";

    // Append model context (API reference) if loaded
    if( !m_modelContext.empty() )
    {
        systemPrompt += "\n\n=== KIPY SCHEMATIC API REFERENCE ===\n";
        systemPrompt += m_modelContext;
        systemPrompt += "\n=== END API REFERENCE ===\n";
    }

    return systemPrompt;
}

void AGENT_FRAME::ShowChangedLanguage()
{
    KIWAY_PLAYER::ShowChangedLanguage();
}

void AGENT_FRAME::AppendHtml( const wxString& aHtml )
{
    m_fullHtmlContent += aHtml;
    SetHtml( m_fullHtmlContent );
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

void AGENT_FRAME::KiwayMailIn( KIWAY_EXPRESS& aEvent )
{
    fprintf( stderr, "AGENT KiwayMailIn: Received command=%d\n", aEvent.Command() );
    fflush( stderr );

    if( aEvent.Command() == MAIL_AGENT_RESPONSE )
    {
        std::string payload = aEvent.GetPayload();
        fprintf( stderr, "AGENT KiwayMailIn: MAIL_AGENT_RESPONSE received, payload='%.100s...'\n",
                 payload.c_str() );
        fprintf( stderr, "AGENT KiwayMailIn: IsToolExecuting=%d\n", m_conversationCtx.IsToolExecuting() );
        fflush( stderr );

        // Check if we're in async tool execution mode
        if( m_conversationCtx.IsToolExecuting() )
        {
            fprintf( stderr, "AGENT KiwayMailIn: In async mode, finding executing tool\n" );
            fprintf( stderr, "AGENT KiwayMailIn: PendingToolCallCount=%zu\n",
                     m_conversationCtx.GetPendingToolCallCount() );
            fflush( stderr );

            // Find the executing tool call to get its ID
            PendingToolCall* executing = m_conversationCtx.GetExecutingToolCall();
            fprintf( stderr, "AGENT KiwayMailIn: GetExecutingToolCall returned %s\n",
                     executing ? executing->tool_name.c_str() : "null" );
            fflush( stderr );

            if( executing )
            {
                fprintf( stderr, "AGENT KiwayMailIn: Found executing tool '%s', posting result\n",
                         executing->tool_name.c_str() );
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
                fprintf( stderr, "AGENT KiwayMailIn: No executing tool found!\n" );
                fflush( stderr );
            }
        }
        else
        {
            // Legacy sync mode - just store the response
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

    // 1. Reset all to normal
    wxTextAttr normalStyle;
    normalStyle.SetFontWeight( wxFONTWEIGHT_NORMAL );
    m_inputCtrl->SetStyle( 0, text.Length(), normalStyle );

    // 2. Scan for @{...} pairs
    size_t start = 0;
    while( ( start = text.find( "@{", start ) ) != wxString::npos )
    {
        size_t end = text.find( "}", start );
        if( end != wxString::npos )
        {
            // Apply Bold to @{...}
            wxTextAttr boldStyle;
            boldStyle.SetFontWeight( wxFONTWEIGHT_BOLD );
            m_inputCtrl->SetStyle( start, end + 1, boldStyle );
            start = end + 1;
        }
        else
        {
            break; // No more closed tags
        }
    }

    // Restore insertion point/selection (SetStyle might affect it?)
    // Actually, SetStyle preserves insertion point usually, but let's be safe if needed.
    // In wxWidgets, SetStyle shouldn't move cursor.
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
    // If already processing, this button acts as Stop
    if( m_actionButton->GetLabel() == "Stop" )
    {
        OnStop( aEvent );
        return;
    }

    wxString text = m_inputCtrl->GetValue();
    if( text.IsEmpty() )
        return;

    // Display User Message
    wxString msgHtml = wxString::Format( "<p><b>User:</b> %s</p>", text );
    AppendHtml( msgHtml );
    AppendHtml( "<p><b>Agent:</b> " ); // Start Agent response block

    // Clear Input and Update UI
    m_inputCtrl->Clear();
    m_actionButton->SetLabel( "Stop" );

    // Get system prompt with model context
    std::string systemPrompt = GetSystemPrompt();

    // Append payload context to system prompt if available
    if( !m_schJson.empty() )
        systemPrompt += "\n\nCURRENT SCHEMATIC CONTEXT:\n" + m_schJson;
    if( !m_pcbJson.empty() )
        systemPrompt += "\n\nCURRENT PCB CONTEXT:\n" + m_pcbJson;

    // Update History
    m_chatHistory.push_back( { { "role", "user" }, { "content", text.ToStdString() } } );
    m_currentResponse = ""; // Reset accumulator
    m_pendingToolCalls = nlohmann::json::array(); // Reset pending tool calls
    m_stopRequested = false; // Reset stop flag

    // Transition state machine to WAITING_FOR_LLM
    m_conversationCtx.Reset();  // Start fresh
    m_conversationCtx.TransitionTo( AgentConversationState::WAITING_FOR_LLM );

    // Get selected model and configure LLM client
    wxString modelNameProp = m_modelChoice->GetStringSelection();
    m_llmClient->SetModel( modelNameProp.ToStdString() );

    // Use async native tool calling (non-blocking)
    StartAsyncLLMRequest();
}

void AGENT_FRAME::OnStop( wxCommandEvent& aEvent )
{
    // Handle old worker thread approach (for backwards compatibility)
    if( m_workerThread )
    {
        m_workerThread->Delete(); // soft delete, checks TestDestroy()
        m_workerThread = nullptr;
    }

    // Cancel any in-progress async LLM request
    if( m_llmClient && m_llmClient->IsRequestInProgress() )
    {
        m_llmClient->CancelRequest();
    }

    // Signal to stop - affects tool execution loops and streaming callbacks
    m_stopRequested = true;

    // Transition state machine to IDLE
    m_conversationCtx.TransitionTo( AgentConversationState::IDLE );

    AppendHtml( "</p><p><i>(Stopped)</i></p>" );
    m_actionButton->SetLabel( "->" );
}

void AGENT_FRAME::OnAgentUpdate( wxCommandEvent& aEvent )
{
    wxString content = aEvent.GetString();

    // Accumulate RAW text for parsing
    m_currentResponse += content;

    // Display with HTML formatting
    wxString displayContent = content;
    displayContent.Replace( "\n", "<br>" );
    AppendHtml( displayContent );

    // Auto-scroll
    int x, y;
    m_chatWindow->GetVirtualSize( &x, &y );
    m_chatWindow->Scroll( 0, y );

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
    // Thread has finished naturally
    if( m_workerThread )
    {
        m_workerThread->Wait(); // Join
        delete m_workerThread;
        m_workerThread = nullptr;
    }


    AppendHtml( "</p>" ); // Close Agent block
    m_actionButton->SetLabel( "->" );

    // Add Assistant response to history
    if( !m_currentResponse.empty() )
    {
        m_chatHistory.push_back( { { "role", "assistant" }, { "content", m_currentResponse } } );
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
    else if( href == "tool:deny" )
    {
        AppendHtml( "<p><i>Tool call denied by user.</i></p>" );
        m_chatHistory.push_back( { { "role", "user" }, { "content", "Tool execution denied." } } );
        // Optionally resume generation or wait for user input?
        // Usually better to let user type why.
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
        m_chatHistory.push_back(
                { { "role", "user" }, { "content", "Error: Unknown tool command '" + commandName + "'" } } );
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
    m_chatHistory.push_back( { { "role", "user" }, { "content", toolMsg } } );

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

    AppendHtml( "<p><b>Agent:</b> " );

    // Resume Agent with system prompt including model context
    std::string systemPrompt = GetSystemPrompt();

    std::string payload;
    if( !m_schJson.empty() )
        payload += m_schJson + "\n";
    if( !m_pcbJson.empty() )
        payload += m_pcbJson + "\n";

    wxString model = m_modelChoice->GetStringSelection();

    m_currentResponse = "";
    m_actionButton->SetLabel( "Stop" );

    m_workerThread = new AGENT_THREAD( this, m_chatHistory, systemPrompt, payload, model.ToStdString() );
    if( m_workerThread->Run() != wxTHREAD_NO_ERROR )
    {
        wxLogMessage( "Error creating thread" );
        m_actionButton->SetLabel( "->" );
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
    // This allows for model-specific context files in the future
    wxString newModel = m_modelChoice->GetStringSelection();
    if( newModel.ToStdString() != m_currentModel )
    {
        m_currentModel = newModel.ToStdString();
        LoadModelContext();
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
    using json = nlohmann::json;

    m_tools.clear();

    // Tool 1: run_shell - Execute Python code in KiCad IPC shell
    LLM_TOOL runShell;
    runShell.name = "run_shell";
    runShell.description = "Execute Python code in the KiCad IPC shell. Use mode 'sch' for schematic operations "
                           "(sch object pre-imported) or 'pcb' for board operations (board object pre-imported). "
                           "kipy, Vector2 are also pre-imported.";
    runShell.input_schema = {
        { "type", "object" },
        { "properties", {
            { "mode", {
                { "type", "string" },
                { "enum", json::array( { "sch", "pcb" } ) },
                { "description", "sch for schematic operations, pcb for board operations" }
            }},
            { "code", {
                { "type", "string" },
                { "description", "Python code to execute. Variables available: kipy, sch/board (depending on mode), Vector2" }
            }}
        }},
        { "required", json::array( { "mode", "code" } ) }
    };
    m_tools.push_back( runShell );

    // Tool 2: run_terminal - Execute bash/shell commands
    LLM_TOOL runTerminal;
    runTerminal.name = "run_terminal";
    runTerminal.description = "Execute bash/shell commands for file operations, git, and other terminal tasks.";
    runTerminal.input_schema = {
        { "type", "object" },
        { "properties", {
            { "command", {
                { "type", "string" },
                { "description", "Bash command to execute" }
            }}
        }},
        { "required", json::array( { "command" } ) }
    };
    m_tools.push_back( runTerminal );
}

std::string AGENT_FRAME::ExecuteTool( const std::string& aName, const nlohmann::json& aInput )
{
    if( aName == "run_shell" )
    {
        std::string mode = aInput.value( "mode", "" );
        std::string code = aInput.value( "code", "" );

        if( mode.empty() || code.empty() )
            return "Error: run_shell requires 'mode' and 'code' parameters";

        // Build the command string for the terminal frame
        std::string command = "run_shell " + mode + " " + code;
        return SendRequest( FRAME_TERMINAL, command );
    }
    else if( aName == "run_terminal" )
    {
        std::string command = aInput.value( "command", "" );

        if( command.empty() )
            return "Error: run_terminal requires 'command' parameter";

        return SendRequest( FRAME_TERMINAL, "run_terminal " + command );
    }

    return "Error: Unknown tool '" + aName + "'";
}

void AGENT_FRAME::HandleLLMEvent( const LLM_EVENT& aEvent )
{
    using json = nlohmann::json;

    // Ignore events if stop was requested
    if( m_stopRequested )
        return;

    switch( aEvent.type )
    {
    case LLM_EVENT_TYPE::TEXT:
    {
        // Accumulate text and display
        m_currentResponse += aEvent.text;

        wxString displayContent = aEvent.text;
        displayContent.Replace( "\n", "<br>" );
        AppendHtml( displayContent );

        // Auto-scroll
        int x, y;
        m_chatWindow->GetVirtualSize( &x, &y );
        m_chatWindow->Scroll( 0, y );
        break;
    }
    case LLM_EVENT_TYPE::TOOL_USE:
    {
        // Store tool call for execution
        json toolCall;
        toolCall["type"] = "tool_use";
        toolCall["id"] = aEvent.tool_use_id;
        toolCall["name"] = aEvent.tool_name;
        toolCall["input"] = aEvent.tool_input;

        if( !m_pendingToolCalls.is_array() )
            m_pendingToolCalls = json::array();

        m_pendingToolCalls.push_back( toolCall );

        // Display tool call in UI
        std::string inputStr = aEvent.tool_input.dump( 2 );
        wxString htmlToolCall = wxString::Format(
            "<br><table width='100%%' bgcolor='#2d2d2d' cellpadding='10'><tr><td>"
            "<font color='#4ec9b0' face='Courier New' size='2'>🔧 Tool: %s</font><br>"
            "<font color='#d4d4d4' face='Courier New' size='2'>%s</font>"
            "</td></tr></table>",
            aEvent.tool_name, inputStr );
        AppendHtml( htmlToolCall );
        break;
    }
    case LLM_EVENT_TYPE::TOOL_USE_DONE:
    {
        fprintf( stderr, "AGENT HandleLLMEvent: TOOL_USE_DONE received\n" );
        fflush( stderr );

        // All tool calls received, execute them asynchronously
        if( m_pendingToolCalls.is_array() && !m_pendingToolCalls.empty() )
        {
            fprintf( stderr, "AGENT HandleLLMEvent: %zu pending tool calls\n", m_pendingToolCalls.size() );
            fflush( stderr );

            // Transition to TOOL_USE_DETECTED state
            m_conversationCtx.TransitionTo( AgentConversationState::TOOL_USE_DETECTED );

            // Add assistant message with tool use blocks to history
            AddAssistantToolUseToHistory( m_pendingToolCalls );

            // Queue all tools for async execution
            for( const auto& toolCall : m_pendingToolCalls )
            {
                std::string toolId = toolCall.value( "id", "" );
                std::string toolName = toolCall.value( "name", "" );
                json toolInput = toolCall.value( "input", json::object() );

                fprintf( stderr, "AGENT HandleLLMEvent: Queueing tool '%s' id='%s'\n",
                         toolName.c_str(), toolId.c_str() );
                fflush( stderr );

                PendingToolCall pending( toolId, toolName, toolInput );
                m_conversationCtx.AddPendingToolCall( pending );
            }

            // Clear the JSON array (tools are now in state machine)
            m_pendingToolCalls = json::array();

            // Start executing the first tool asynchronously
            PendingToolCall* first = m_conversationCtx.GetNextPendingToolCall();
            if( first )
            {
                fprintf( stderr, "AGENT HandleLLMEvent: Starting first tool '%s'\n", first->tool_name.c_str() );
                fflush( stderr );

                // Show "Running..." state
                wxString runningHtml = wxString::Format(
                    "<br><font color='#888888'><i>Running %s...</i></font>", first->tool_name );
                AppendHtml( runningHtml );

                // Execute async - returns immediately, result comes via event
                ExecuteToolAsync( first->tool_name, first->tool_input, first->tool_use_id );
            }
        }
        break;
    }
    case LLM_EVENT_TYPE::END_TURN:
    {
        // Model finished
        AppendHtml( "</p>" );
        m_actionButton->SetLabel( "->" );

        // Add final assistant message to history if there's accumulated text
        if( !m_currentResponse.empty() )
        {
            m_chatHistory.push_back( { { "role", "assistant" }, { "content", m_currentResponse } } );
        }

        // Transition back to IDLE
        m_conversationCtx.SetState( AgentConversationState::IDLE );
        break;
    }
    case LLM_EVENT_TYPE::ERROR:
    {
        wxString errorHtml = wxString::Format( "<p><font color='red'><b>Error:</b> %s</font></p>",
                                               aEvent.error_message );
        AppendHtml( errorHtml );
        m_actionButton->SetLabel( "->" );
        break;
    }
    }
}

void AGENT_FRAME::ContinueConversation()
{
    // Continue the conversation after tool results
    AppendHtml( "<p><b>Agent:</b> " );
    m_currentResponse = "";

    wxString model = m_modelChoice->GetStringSelection();
    m_llmClient->SetModel( model.ToStdString() );

    std::string systemPrompt = GetSystemPrompt();

    m_llmClient->AskStreamWithTools(
        m_chatHistory,
        systemPrompt,
        m_tools,
        [this]( const LLM_EVENT& event )
        {
            // Post event to main thread for UI updates
            wxCommandEvent* evt = new wxCommandEvent( wxEVT_AGENT_UPDATE );
            // Store event data - we'll handle it in the callback directly for now
            // since we're on the same thread with curl blocking
            HandleLLMEvent( event );
        }
    );
}

void AGENT_FRAME::AddToolResultToHistory( const std::string& aToolUseId, const std::string& aResult )
{
    using json = nlohmann::json;

    // Add tool result as user message with tool_result content block
    json toolResultMsg;
    toolResultMsg["role"] = "user";
    toolResultMsg["content"] = json::array( {
        {
            { "type", "tool_result" },
            { "tool_use_id", aToolUseId },
            { "content", aResult }
        }
    });

    m_chatHistory.push_back( toolResultMsg );
}

void AGENT_FRAME::AddAssistantToolUseToHistory( const nlohmann::json& aToolUseBlocks )
{
    using json = nlohmann::json;

    // Build assistant message with text (if any) and tool_use blocks
    json assistantMsg;
    assistantMsg["role"] = "assistant";

    json content = json::array();

    // Add accumulated text if present
    if( !m_currentResponse.empty() )
    {
        content.push_back( {
            { "type", "text" },
            { "text", m_currentResponse }
        });
    }

    // Add tool use blocks
    for( const auto& toolBlock : aToolUseBlocks )
    {
        content.push_back( toolBlock );
    }

    assistantMsg["content"] = content;
    m_chatHistory.push_back( assistantMsg );

    // Reset accumulated text
    m_currentResponse = "";
}

// ============================================================================
// ASYNC TOOL EXECUTION METHODS
// ============================================================================

AgentConversationState AGENT_FRAME::GetConversationState() const
{
    return m_conversationCtx.GetState();
}


bool AGENT_FRAME::CanAcceptUserInput() const
{
    return m_conversationCtx.CanAcceptUserInput();
}


void AGENT_FRAME::ExecuteToolAsync( const std::string& aToolName,
                                     const nlohmann::json& aInput,
                                     const std::string& aToolUseId )
{
    // IMPORTANT: Copy parameters immediately since they may be references to data
    // inside a vector that could be reallocated during this function
    std::string toolName = aToolName;
    nlohmann::json toolInput = aInput;
    std::string toolUseId = aToolUseId;

    fprintf( stderr, "AGENT ExecuteToolAsync: tool=%s id=%s\n",
             toolName.c_str(), toolUseId.c_str() );
    fflush( stderr );
    wxLogDebug( "AGENT: ExecuteToolAsync called: tool=%s id=%s",
                toolName.c_str(), toolUseId.c_str() );

    // Transition to EXECUTING_TOOL state
    if( !m_conversationCtx.TransitionTo( AgentConversationState::EXECUTING_TOOL ) )
    {
        wxLogDebug( "AGENT: Failed to transition to EXECUTING_TOOL state" );
        PostToolError( this, toolUseId, "Invalid state for tool execution" );
        return;
    }

    // Mark the existing pending tool call as executing (don't add a duplicate!)
    PendingToolCall* existingTool = m_conversationCtx.FindPendingToolCall( toolUseId );
    if( existingTool )
    {
        existingTool->start_time = wxGetLocalTimeMillis();
        existingTool->is_executing = true;
    }

    // Build the payload for the terminal
    std::string payload = BuildToolPayload( toolName, toolInput );

    fprintf( stderr, "AGENT ExecuteToolAsync: Sending MAIL_AGENT_REQUEST to FRAME_TERMINAL, payload='%.100s...'\n",
             payload.c_str() );
    fflush( stderr );

    // Check if payload is an error (BuildToolPayload can return errors)
    if( payload.find( "Error:" ) == 0 )
    {
        fprintf( stderr, "AGENT ExecuteToolAsync: BuildToolPayload returned error: %s\n", payload.c_str() );
        fflush( stderr );
        // Post error result immediately
        PostToolError( this, toolUseId, payload );
        return;
    }

    // Ensure terminal frame exists before sending mail
    // Player() with doCreate=true will create the frame if it doesn't exist
    KIWAY_PLAYER* terminal = Kiway().Player( FRAME_TERMINAL, true );
    if( !terminal )
    {
        fprintf( stderr, "AGENT ExecuteToolAsync: Failed to create terminal frame!\n" );
        fflush( stderr );
        PostToolError( this, toolUseId, "Error: Could not create terminal frame" );
        return;
    }

    fprintf( stderr, "AGENT ExecuteToolAsync: Terminal frame exists, sending ExpressMail\n" );
    fflush( stderr );

    // Send async request to terminal
    Kiway().ExpressMail( FRAME_TERMINAL, MAIL_AGENT_REQUEST, payload );

    fprintf( stderr, "AGENT ExecuteToolAsync: ExpressMail sent, starting timeout timer\n" );
    fflush( stderr );

    // Start timeout timer
    m_toolTimeoutTimer.StartOnce( TOOL_TIMEOUT_MS );

    wxLogDebug( "AGENT: Tool execution started, timeout timer set for %d ms", TOOL_TIMEOUT_MS );
}


std::string AGENT_FRAME::BuildToolPayload( const std::string& aToolName, const nlohmann::json& aInput )
{
    fprintf( stderr, "AGENT BuildToolPayload: aToolName='%s' (len=%zu)\n",
             aToolName.c_str(), aToolName.length() );
    fprintf( stderr, "AGENT BuildToolPayload: aInput=%s\n", aInput.dump().c_str() );
    fflush( stderr );

    // Build the command string for the terminal based on tool name
    if( aToolName == "run_shell" )
    {
        fprintf( stderr, "AGENT BuildToolPayload: Matched run_shell\n" );
        fflush( stderr );

        std::string code = aInput.value( "code", "" );
        std::string mode = aInput.value( "mode", "sch" );

        fprintf( stderr, "AGENT BuildToolPayload: mode='%s', code_len=%zu\n", mode.c_str(), code.length() );
        fflush( stderr );

        if( code.empty() )
            return "Error: run_shell requires 'code' parameter";

        return "run_shell " + mode + " " + code;
    }
    else if( aToolName == "run_terminal" )
    {
        fprintf( stderr, "AGENT BuildToolPayload: Matched run_terminal\n" );
        fflush( stderr );

        std::string command = aInput.value( "command", "" );

        if( command.empty() )
            return "Error: run_terminal requires 'command' parameter";

        return "run_terminal " + command;
    }

    fprintf( stderr, "AGENT BuildToolPayload: No match! Returning error\n" );
    fflush( stderr );
    return "Error: Unknown tool '" + aToolName + "'";
}


void AGENT_FRAME::OnToolExecutionComplete( wxCommandEvent& aEvent )
{
    wxLogDebug( "AGENT: OnToolExecutionComplete called" );

    // Stop the timeout timer
    m_toolTimeoutTimer.Stop();

    // Get the result from the event
    ToolExecutionResult* result = static_cast<ToolExecutionResult*>( aEvent.GetClientData() );
    if( !result )
    {
        wxLogDebug( "AGENT: No result data in event" );
        return;
    }

    // Process the result
    ProcessToolResult( result->tool_use_id, result->result, result->success );

    // Clean up
    delete result;
}


void AGENT_FRAME::OnToolExecutionError( wxCommandEvent& aEvent )
{
    wxLogDebug( "AGENT: OnToolExecutionError called" );

    // Stop the timeout timer
    m_toolTimeoutTimer.Stop();

    // Get the error from the event
    ToolExecutionResult* result = static_cast<ToolExecutionResult*>( aEvent.GetClientData() );
    if( !result )
    {
        wxLogDebug( "AGENT: No error data in event" );
        return;
    }

    // Process as error
    ProcessToolResult( result->tool_use_id, result->error_message, false );

    // Clean up
    delete result;
}


void AGENT_FRAME::OnToolExecutionTimeout( wxTimerEvent& aEvent )
{
    wxLogDebug( "AGENT: OnToolExecutionTimeout called" );

    // Find any executing tool calls
    for( size_t i = 0; i < m_conversationCtx.GetPendingToolCallCount(); i++ )
    {
        PendingToolCall* tool = m_conversationCtx.GetNextPendingToolCall();
        if( tool && tool->is_executing )
        {
            wxLogDebug( "AGENT: Tool '%s' timed out", tool->tool_name.c_str() );

            ProcessToolResult( tool->tool_use_id,
                               "Error: Tool execution timed out after 30 seconds",
                               false );
            break;
        }
    }
}


void AGENT_FRAME::OnToolExecutionProgress( wxCommandEvent& aEvent )
{
    // Handle progress updates (optional - for streaming output)
    ToolExecutionProgress* progress = static_cast<ToolExecutionProgress*>( aEvent.GetClientData() );
    if( !progress )
        return;

    // Display progress in the chat
    if( !progress->output_chunk.empty() )
    {
        wxString chunk = progress->output_chunk;
        chunk.Replace( "\n", "<br>" );
        AppendHtml( chunk );
    }

    delete progress;
}


void AGENT_FRAME::ProcessToolResult( const std::string& aToolUseId,
                                      const std::string& aResult,
                                      bool aSuccess )
{
    wxLogDebug( "AGENT: ProcessToolResult: id=%s success=%d result_len=%zu",
                aToolUseId.c_str(), aSuccess, aResult.size() );

    // Store the result in collected results
    AgentConversationContext::ToolResult toolResult;
    toolResult.tool_use_id = aToolUseId;
    toolResult.result = aResult;
    m_conversationCtx.completed_tool_results.push_back( toolResult );

    // Also store as last (for backward compatibility)
    m_conversationCtx.last_tool_use_id = aToolUseId;
    m_conversationCtx.last_tool_result = aResult;

    // Remove from pending
    m_conversationCtx.RemovePendingToolCall( aToolUseId );

    // Display result in UI
    wxString htmlResult = aResult;
    htmlResult.Replace( "\n", "<br>" );
    htmlResult.Replace( " ", "&nbsp;" );

    wxString statusIcon = aSuccess ? "✓" : "✗";
    wxString statusColor = aSuccess ? "#4ec9b0" : "#f44747";

    wxString resultBox = wxString::Format(
        "<br><table width='100%%' bgcolor='#1e1e1e' cellpadding='10'><tr><td>"
        "<font color='%s' face='Courier New' size='2'>%s Result:</font><br>"
        "<font color='#d4d4d4' face='Courier New' size='2'>%s</font>"
        "</td></tr></table>",
        statusColor, statusIcon, htmlResult );
    AppendHtml( resultBox );

    // Transition to PROCESSING_TOOL_RESULT
    m_conversationCtx.TransitionTo( AgentConversationState::PROCESSING_TOOL_RESULT );

    // Check if there are more pending tools
    if( m_conversationCtx.HasPendingToolCalls() )
    {
        // Execute next tool
        PendingToolCall* next = m_conversationCtx.GetNextPendingToolCall();
        if( next )
        {
            // Show "Running..." state
            wxString runningHtml = wxString::Format(
                "<br><font color='#888888'><i>Running %s...</i></font>", next->tool_name );
            AppendHtml( runningHtml );

            ExecuteToolAsync( next->tool_name, next->tool_input, next->tool_use_id );
        }
    }
    else
    {
        // All tools done, continue conversation
        ContinueConversationWithToolResult();
    }
}


void AGENT_FRAME::ContinueConversationWithToolResult()
{
    wxLogDebug( "AGENT: ContinueConversationWithToolResult called with %zu results",
                m_conversationCtx.completed_tool_results.size() );

    // Add all collected tool results to history as a single user message
    // with multiple tool_result content blocks (per Anthropic API spec)
    using json = nlohmann::json;

    if( !m_conversationCtx.completed_tool_results.empty() )
    {
        json toolResultMsg;
        toolResultMsg["role"] = "user";
        json content = json::array();

        for( const auto& result : m_conversationCtx.completed_tool_results )
        {
            content.push_back( {
                { "type", "tool_result" },
                { "tool_use_id", result.tool_use_id },
                { "content", result.result }
            } );
        }

        toolResultMsg["content"] = content;
        m_chatHistory.push_back( toolResultMsg );

        // Clear collected results
        m_conversationCtx.completed_tool_results.clear();
    }

    // Transition to WAITING_FOR_LLM
    m_conversationCtx.TransitionTo( AgentConversationState::WAITING_FOR_LLM );

    // Continue the conversation
    AppendHtml( "<p><b>Agent:</b> " );
    m_currentResponse = "";

    wxString model = m_modelChoice->GetStringSelection();
    m_llmClient->SetModel( model.ToStdString() );

    std::string systemPrompt = GetSystemPrompt();

    // Use async LLM streaming (non-blocking)
    StartAsyncLLMRequest();
}


// ============================================================================
// Async LLM Streaming Event Handlers
// ============================================================================

void AGENT_FRAME::StartAsyncLLMRequest()
{
    wxString model = m_modelChoice->GetStringSelection();
    m_llmClient->SetModel( model.ToStdString() );

    std::string systemPrompt = GetSystemPrompt();

    // Start async request - returns immediately
    if( !m_llmClient->AskStreamWithToolsAsync( m_chatHistory, systemPrompt, m_tools, this ) )
    {
        wxLogDebug( "AGENT: Failed to start async LLM request" );
        AppendHtml( "<p><font color='red'>Error: Failed to start LLM request</font></p>" );
        m_conversationCtx.TransitionTo( AgentConversationState::IDLE );
        m_actionButton->SetLabel( "->" );
    }
}


void AGENT_FRAME::OnLLMStreamChunk( wxThreadEvent& aEvent )
{
    // Get the chunk data from the event payload
    LLMStreamChunk* chunk = aEvent.GetPayload<LLMStreamChunk*>();
    if( !chunk )
        return;

    // Process the chunk
    HandleLLMChunk( *chunk );

    // Clean up
    delete chunk;
}


void AGENT_FRAME::HandleLLMChunk( const LLMStreamChunk& aChunk )
{
    // Ignore events if stop was requested
    if( m_stopRequested )
        return;

    switch( aChunk.type )
    {
    case LLMChunkType::TEXT:
    {
        // Accumulate text and display
        m_currentResponse += aChunk.text;

        wxString displayContent = aChunk.text;
        displayContent.Replace( "\n", "<br>" );
        AppendHtml( displayContent );

        // Auto-scroll
        int x, y;
        m_chatWindow->GetVirtualSize( &x, &y );
        m_chatWindow->Scroll( 0, y );
        break;
    }
    case LLMChunkType::TOOL_USE:
    {
        // Parse tool input JSON
        nlohmann::json toolInput;
        try
        {
            if( !aChunk.tool_input_json.empty() )
            {
                toolInput = nlohmann::json::parse( aChunk.tool_input_json );
            }
        }
        catch( const nlohmann::json::exception& e )
        {
            wxLogDebug( "AGENT: Failed to parse tool input JSON: %s", e.what() );
            toolInput = nlohmann::json::object();
        }

        // Store tool call for execution
        nlohmann::json toolCall;
        toolCall["type"] = "tool_use";
        toolCall["id"] = aChunk.tool_use_id;
        toolCall["name"] = aChunk.tool_name;
        toolCall["input"] = toolInput;

        m_pendingToolCalls.push_back( toolCall );

        // Display tool call in UI
        wxString toolHtml = wxString::Format(
            "<br><font color='#569cd6'>🔧 Tool:</font> <b>%s</b><br>"
            "<font color='#6a9955' face='Courier New' size='2'>%s</font>",
            aChunk.tool_name, aChunk.tool_input_json );
        AppendHtml( toolHtml );
        break;
    }
    case LLMChunkType::TOOL_USE_DONE:
    {
        fprintf( stderr, "AGENT HandleLLMChunk: TOOL_USE_DONE received\n" );
        fflush( stderr );

        // All tool calls received, execute them asynchronously
        if( m_pendingToolCalls.is_array() && !m_pendingToolCalls.empty() )
        {
            fprintf( stderr, "AGENT HandleLLMChunk: %zu pending tool calls\n", m_pendingToolCalls.size() );
            fflush( stderr );

            // Transition to TOOL_USE_DETECTED state
            m_conversationCtx.TransitionTo( AgentConversationState::TOOL_USE_DETECTED );

            // Add assistant message with tool use blocks to history
            AddAssistantToolUseToHistory( m_pendingToolCalls );

            // Queue all tools for async execution
            for( const auto& toolCall : m_pendingToolCalls )
            {
                std::string toolId = toolCall.value( "id", "" );
                std::string toolName = toolCall.value( "name", "" );
                nlohmann::json toolInput = toolCall.value( "input", nlohmann::json::object() );

                fprintf( stderr, "AGENT HandleLLMChunk: Queueing tool '%s' id='%s'\n",
                         toolName.c_str(), toolId.c_str() );
                fflush( stderr );

                PendingToolCall pending( toolId, toolName, toolInput );
                m_conversationCtx.AddPendingToolCall( pending );
            }

            // Clear the JSON array (tools are now in state machine)
            m_pendingToolCalls = nlohmann::json::array();

            // Start executing the first tool asynchronously
            PendingToolCall* first = m_conversationCtx.GetNextPendingToolCall();
            if( first )
            {
                fprintf( stderr, "AGENT HandleLLMChunk: Starting first tool '%s'\n", first->tool_name.c_str() );
                fflush( stderr );

                // Show "Running..." state
                wxString runningHtml = wxString::Format(
                    "<br><font color='#888888'><i>Running %s...</i></font>", first->tool_name );
                AppendHtml( runningHtml );

                // Execute async - returns immediately, result comes via event
                ExecuteToolAsync( first->tool_name, first->tool_input, first->tool_use_id );
            }
        }
        break;
    }
    case LLMChunkType::END_TURN:
    {
        // Model finished
        AppendHtml( "</p>" );
        m_actionButton->SetLabel( "->" );

        // Add final assistant message to history if there's accumulated text
        if( !m_currentResponse.empty() )
        {
            m_chatHistory.push_back( {
                { "role", "assistant" },
                { "content", m_currentResponse }
            } );
        }

        m_conversationCtx.TransitionTo( AgentConversationState::IDLE );
        break;
    }
    case LLMChunkType::ERROR:
    {
        wxString errorHtml = wxString::Format( "<p><font color='red'><b>Error:</b> %s</font></p>",
                                               aChunk.error_message );
        AppendHtml( errorHtml );
        m_actionButton->SetLabel( "->" );
        m_conversationCtx.TransitionTo( AgentConversationState::IDLE );
        break;
    }
    }
}


void AGENT_FRAME::OnLLMStreamComplete( wxThreadEvent& aEvent )
{
    wxLogDebug( "AGENT: OnLLMStreamComplete called" );

    // Get completion data
    LLMStreamComplete* complete = aEvent.GetPayload<LLMStreamComplete*>();
    if( complete )
    {
        if( !complete->success )
        {
            wxString errorHtml = wxString::Format( "<p><font color='red'><b>Error:</b> %s</font></p>",
                                                   complete->error_message );
            AppendHtml( errorHtml );
        }
        delete complete;
    }

    // If we're still waiting for LLM (no tool calls), transition to IDLE
    if( m_conversationCtx.GetState() == AgentConversationState::WAITING_FOR_LLM )
    {
        m_conversationCtx.TransitionTo( AgentConversationState::IDLE );
        m_actionButton->SetLabel( "->" );
    }
}


void AGENT_FRAME::OnLLMStreamError( wxThreadEvent& aEvent )
{
    wxLogDebug( "AGENT: OnLLMStreamError called" );

    // Get error data
    LLMStreamComplete* complete = aEvent.GetPayload<LLMStreamComplete*>();
    if( complete )
    {
        wxString errorHtml = wxString::Format( "<p><font color='red'><b>Error:</b> %s</font></p>",
                                               complete->error_message );
        AppendHtml( errorHtml );
        delete complete;
    }

    m_conversationCtx.TransitionTo( AgentConversationState::IDLE );
    m_actionButton->SetLabel( "->" );
}
