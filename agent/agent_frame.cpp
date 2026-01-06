#include "agent_frame.h"
#include "agent_thread.h"
#include <kiway_express.h>
#include <mail_type.h>
#include <wx/log.h>
#include <kiway.h>
#include <sstream>
#include <wx/sizer.h>
#include <wx/textctrl.h>
#include <wx/msgdlg.h>
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
    m_chatWindow->SetPage(
            "<html><body bgcolor='#1E1E1E' text='#FFFFFF'><p>Welcome to KiCad Agent.</p></body></html>" );
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

    // Initialize History
    m_chatHistory = nlohmann::json::array();
}

AGENT_FRAME::~AGENT_FRAME()
{
}

void AGENT_FRAME::ShowChangedLanguage()
{
    KIWAY_PLAYER::ShowChangedLanguage();
}

void AGENT_FRAME::KiwayMailIn( KIWAY_EXPRESS& aEvent )
{
    if( aEvent.Command() == MAIL_AGENT_RESPONSE )
    {
        m_toolResponse = aEvent.GetPayload();
        // wxLogMessage( "Agent received response: %s", m_toolResponse.c_str() ); // Removed to prevent pop-ups
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
    // If thread is running, this button acts as Stop
    if( m_workerThread )
    {
        OnStop( aEvent );
        return;
    }

    wxString text = m_inputCtrl->GetValue();
    if( text.IsEmpty() )
        return;

    // Display User Message
    wxString msgHtml = wxString::Format( "<p><b>User:</b> %s</p>", text );
    m_chatWindow->AppendToPage( msgHtml );
    m_chatWindow->AppendToPage( "<p><b>Agent:</b> " ); // Start Agent response block

    // Clear Input and Update UI
    m_inputCtrl->Clear();
    m_actionButton->SetLabel( "Stop" );
    // m_inputCtrl->Enable( false ); // Optional: disable input during generation

    // Create and Run Thread
    // TODO: proper system prompt management
    // Create and Run Thread
    // TODO: proper system prompt management
    std::string systemPrompt = R"(You are a helpful assistant for KiCad PCB design.
You have access to the Terminal to query and modify the project state using Python scripting.
To use the terminal, you MUST format your response as follows:
THOUGHT: [Your reasoning here]
TOOL_CALL: run_terminal_command [mode] [command]

Available Modes:
- sys: System shell (e.g., ls, git, grep).
- pcb: PCB Editor python shell (pre-loaded 'board').
- sch: Schematic Editor python shell (pre-loaded 'schematic').

Commands:
- run_terminal_command create_agent [mode]
- run_terminal_command list
- run_terminal_command [id] [mode] [command]

Examples:
- run_terminal_command create_agent pcb
- run_terminal_command sys ls -la
- run_terminal_command pcb print(len(board.GetTracks()))

When you need information, explain your thought process and then output the TOOL_CALL.
Wait for the tool output before providing the final answer.
)";

    // Pass payload if available (Combine SCH and PCB contexts)
    std::string payload;
    if( !m_schJson.empty() )
        payload += m_schJson + "\n";
    if( !m_pcbJson.empty() )
        payload += m_pcbJson + "\n";

    // Update History
    m_chatHistory.push_back( { { "role", "user" }, { "content", text.ToStdString() } } );
    m_currentResponse = ""; // Reset accumulator

    // Get selected model
    wxString modelNameProp = m_modelChoice->GetStringSelection();

    m_workerThread = new AGENT_THREAD( this, m_chatHistory, systemPrompt, payload, modelNameProp.ToStdString() );
    if( m_workerThread->Run() != wxTHREAD_NO_ERROR )
    {
        wxLogMessage( "Error: Could not create worker thread." );
        delete m_workerThread;
        m_workerThread = nullptr;
        m_actionButton->SetLabel( "->" );
        // m_inputCtrl->Enable( true );
    }
}

void AGENT_FRAME::OnStop( wxCommandEvent& aEvent )
{
    if( m_workerThread )
    {
        m_workerThread->Delete(); // soft delete, checks TestDestroy()
        m_workerThread = nullptr;
    }
    m_stopRequested = true; // Signal sync loops to stop
    m_chatWindow->AppendToPage( "</p><p><i>(Stopped)</i></p>" );
    m_actionButton->SetLabel( "->" );
    // m_inputCtrl->Enable( true );
}

void AGENT_FRAME::OnAgentUpdate( wxCommandEvent& aEvent )
{
    wxString content = aEvent.GetString();

    // Accumulate RAW text for parsing
    m_currentResponse += content;

    // Display with HTML formatting
    wxString displayContent = content;
    displayContent.Replace( "\n", "<br>" );
    m_chatWindow->AppendToPage( displayContent );

    // Auto-scroll
    int x, y;
    m_chatWindow->GetVirtualSize( &x, &y );
    m_chatWindow->Scroll( 0, y );

    // Check for TOOL_CALL to force stop
    // We check m_currentResponse (raw)
    size_t toolPos = m_currentResponse.rfind( "TOOL_CALL:" );
    if( toolPos != std::string::npos )
    {
        // Check if we have the full line (newline after TOOL_CALL)
        size_t lineEnd = m_currentResponse.find( '\n', toolPos );
        if( lineEnd != std::string::npos )
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

void AGENT_FRAME::OnAgentComplete( wxCommandEvent& aEvent )
{
    // Thread has finished naturally
    if( m_workerThread )
    {
        m_workerThread->Wait(); // Join
        delete m_workerThread;
        m_workerThread = nullptr;
    }

    m_chatWindow->AppendToPage( "</p>" ); // Close Agent block
    m_actionButton->SetLabel( "->" );

    // Add Assistant response to history
    if( !m_currentResponse.empty() )
    {
        m_chatHistory.push_back( { { "role", "assistant" }, { "content", m_currentResponse } } );
    }

    if( aEvent.GetInt() == 0 ) // Failure
    {
        m_chatWindow->AppendToPage( "<p><i>(Error generating response)</i></p>" );
    }
    else
    {
        // Parse for Tool Calls
        size_t toolPos = m_currentResponse.rfind( "TOOL_CALL: " );
        if( toolPos != std::string::npos )
        {
            size_t start = toolPos + 11;
            size_t end = m_currentResponse.find_first_of( "\n\r", start );
            if( end == std::string::npos )
                end = m_currentResponse.length();

            std::string toolName = m_currentResponse.substr( start, end - start );
            // Clean whitespace and newlines
            toolName.erase( 0, toolName.find_first_not_of( " \t\r\n" ) );
            toolName.erase( toolName.find_last_not_of( " \t\r\n" ) + 1 );

            m_pendingTool = toolName;

            // Show Inline Approve Link with Colors
            std::string html = "<p><b>Tool Request:</b> " + toolName
                               + " <a href=\"tool:approve\" style=\"color: #00AA00; font-weight: bold;\">[Approve]</a>"
                               + " <a href=\"tool:deny\" style=\"color: #AA0000; font-weight: bold;\">[Deny]</a></p>";
            m_chatWindow->AppendToPage( html );

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
        m_chatWindow->AppendToPage( "<p><i>Tool call denied by user.</i></p>" );
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

    m_chatWindow->AppendToPage( "<p><i>Executing " + m_pendingTool + "...</i></p>" );

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

    m_chatWindow->AppendToPage( "<p><b>System:</b> Tool Output received.</p>" );
    m_chatWindow->AppendToPage( "<p><b>Agent:</b> " );

    // Resume Agent
    std::string systemPrompt = R"(You are a helpful assistant for KiCad PCB design.
You have access to the Terminal.
To use the terminal, you MUST format your response as follows:
THOUGHT: [Your reasoning here]
TOOL_CALL: run_terminal_command [mode] [command]

Available Modes:
- sys: System shell.
- pcb: PCB Editor python shell.
- sch: Schematic Editor python shell.

Commands:
- run_terminal_command create_agent [mode]
- run_terminal_command list
- run_terminal_command [id] [mode] [command]

When you need information, explain your thought process and then output the TOOL_CALL.
Wait for the tool output before providing the final answer.
IMPORTANT: You must STOP generating text immediately after the TOOL_CALL line. Do not hallucinate the result.
)";

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
    // Handle model change if we need immediate feedback, otherwise just read in OnSend
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
