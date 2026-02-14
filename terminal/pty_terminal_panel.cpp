#include "pty_terminal_panel.h"
#include "pty_handler.h"
#include "vterm_handler.h"

#include <wx/dcclient.h>
#include <wx/dcmemory.h>
#include <wx/dcbuffer.h>
#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#include <wx/settings.h>
#include <wx/log.h>

#include <vterm_keycodes.h>
#include <algorithm>


static const int    DEFAULT_FONT_SIZE = 13;
static const char*  DEFAULT_FONT_NAME = "Menlo";
static const int    CURSOR_BLINK_MS   = 500;
static const int    AGENT_TIMEOUT_MS  = 15000;


PTY_TERMINAL_PANEL::PTY_TERMINAL_PANEL( wxWindow* aParent ) :
        wxPanel( aParent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                 wxWANTS_CHARS | wxNO_BORDER ),
        m_cellWidth( 8 ),
        m_cellHeight( 16 ),
        m_fontAscent( 12 ),
        m_scaleFactor( 1.0 ),
        m_needsFullRedraw( true ),
        m_lastFontIdx( -1 ),
        m_cursorBlinkOn( true ),
        m_selecting( false ),
        m_selStartRow( 0 ), m_selStartCol( 0 ),
        m_selEndRow( 0 ), m_selEndCol( 0 ),
        m_hasSelection( false ),
        m_scrollOffset( 0 ),
        m_renderPending( false ),
        m_agentCapturing( false ),
        m_agentSentinelStartFound( false )
{
    SetBackgroundStyle( wxBG_STYLE_PAINT ); // Avoid flicker

    // Create fonts
    m_fontNormal = wxFont( DEFAULT_FONT_SIZE, wxFONTFAMILY_TELETYPE,
                           wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL,
                           false, DEFAULT_FONT_NAME );

    m_fontBold = wxFont( DEFAULT_FONT_SIZE, wxFONTFAMILY_TELETYPE,
                         wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD,
                         false, DEFAULT_FONT_NAME );

    m_fontItalic = wxFont( DEFAULT_FONT_SIZE, wxFONTFAMILY_TELETYPE,
                           wxFONTSTYLE_ITALIC, wxFONTWEIGHT_NORMAL,
                           false, DEFAULT_FONT_NAME );

    m_fontBoldItalic = wxFont( DEFAULT_FONT_SIZE, wxFONTFAMILY_TELETYPE,
                               wxFONTSTYLE_ITALIC, wxFONTWEIGHT_BOLD,
                               false, DEFAULT_FONT_NAME );

    MeasureFont();

    // Bind events
    Bind( wxEVT_PAINT, &PTY_TERMINAL_PANEL::OnPaint, this );
    Bind( wxEVT_SIZE, &PTY_TERMINAL_PANEL::OnSize, this );
    Bind( wxEVT_KEY_DOWN, &PTY_TERMINAL_PANEL::OnKeyDown, this );
    Bind( wxEVT_CHAR, &PTY_TERMINAL_PANEL::OnChar, this );
    Bind( wxEVT_LEFT_DOWN, &PTY_TERMINAL_PANEL::OnMouseDown, this );
    Bind( wxEVT_MOTION, &PTY_TERMINAL_PANEL::OnMouseMove, this );
    Bind( wxEVT_LEFT_UP, &PTY_TERMINAL_PANEL::OnMouseUp, this );
    Bind( wxEVT_MOUSEWHEEL, &PTY_TERMINAL_PANEL::OnMouseWheel, this );
    Bind( wxEVT_SET_FOCUS, &PTY_TERMINAL_PANEL::OnSetFocus, this );
    Bind( wxEVT_KILL_FOCUS, &PTY_TERMINAL_PANEL::OnKillFocus, this );

    // PTY events
    Bind( wxEVT_PTY_DATA, &PTY_TERMINAL_PANEL::OnPtyData, this );
    Bind( wxEVT_PTY_EXIT, &PTY_TERMINAL_PANEL::OnPtyExit, this );

    // Cursor blink timer
    m_cursorBlinkTimer.SetOwner( this );
    Bind( wxEVT_TIMER, &PTY_TERMINAL_PANEL::OnCursorBlink, this, m_cursorBlinkTimer.GetId() );

    // Render coalesce timer (~60fps)
    m_renderTimer.SetOwner( this );
    Bind( wxEVT_TIMER, &PTY_TERMINAL_PANEL::OnRenderTimer, this, m_renderTimer.GetId() );

    // Agent timeout timer
    m_agentTimeoutTimer.SetOwner( this );
    Bind( wxEVT_TIMER, &PTY_TERMINAL_PANEL::OnAgentTimeout, this, m_agentTimeoutTimer.GetId() );

    SetBackgroundColour( wxColour( 30, 30, 30 ) );
}


PTY_TERMINAL_PANEL::~PTY_TERMINAL_PANEL()
{
    m_cursorBlinkTimer.Stop();
    m_renderTimer.Stop();
    m_agentTimeoutTimer.Stop();

    if( m_pty )
        m_pty->Stop();
}


void PTY_TERMINAL_PANEL::MeasureFont()
{
    wxClientDC dc( this );
    dc.SetFont( m_fontNormal );

    wxSize extent = dc.GetTextExtent( "M" );
    m_cellWidth = extent.GetWidth();
    m_cellHeight = extent.GetHeight();

    // Get font metrics for baseline
    int descent, externalLeading;
    dc.GetTextExtent( "M", nullptr, nullptr, &descent, &externalLeading );
    m_fontAscent = m_cellHeight - descent;

    // Add a small line spacing
    m_cellHeight += 2;

    m_scaleFactor = GetContentScaleFactor();

    wxLogInfo( "PTY_TERMINAL: Font metrics: cellW=%d cellH=%d ascent=%d scale=%.1f",
               m_cellWidth, m_cellHeight, m_fontAscent, m_scaleFactor );
}


bool PTY_TERMINAL_PANEL::StartShell()
{
    // Create vterm handler with initial size guess
    int cols = std::max( 1, GetClientSize().GetWidth() / std::max( 1, m_cellWidth ) );
    int rows = std::max( 1, GetClientSize().GetHeight() / std::max( 1, m_cellHeight ) );

    if( cols < 2 ) cols = 80;
    if( rows < 2 ) rows = 24;

    m_vterm = std::make_unique<VTERM_HANDLER>( rows, cols );

    // Set up vterm output callback (terminal responses → PTY)
    m_vterm->SetOutputCallback( [this]( const char* data, size_t len ) {
        if( m_pty )
            m_pty->Write( data, len );
    } );

    // Set up title callback
    m_vterm->SetTitleCallback( [this]( const std::string& title ) {
        m_termTitle = wxString::FromUTF8( title );
    } );

    // Create PTY handler
    m_pty = std::make_unique<PTY_HANDLER>( this );

    if( !m_pty->Start( cols, rows ) )
    {
        wxLogError( "PTY_TERMINAL: Failed to start PTY" );
        m_pty.reset();
        m_vterm.reset();
        return false;
    }

    // Start cursor blink
    m_cursorBlinkTimer.Start( CURSOR_BLINK_MS );

    m_needsFullRedraw = true;
    Refresh();

    // Grab focus so cursor shows as filled block
    SetFocus();

    return true;
}


bool PTY_TERMINAL_PANEL::IsShellRunning() const
{
    return m_pty && m_pty->IsRunning();
}


wxString PTY_TERMINAL_PANEL::GetTitle() const
{
    if( !m_termTitle.IsEmpty() )
        return m_termTitle;

    return "Shell";
}


int PTY_TERMINAL_PANEL::GetTermCols() const
{
    return m_vterm ? m_vterm->GetCols() : 80;
}


int PTY_TERMINAL_PANEL::GetTermRows() const
{
    return m_vterm ? m_vterm->GetRows() : 24;
}


void PTY_TERMINAL_PANEL::RecalcTermSize()
{
    if( !m_vterm || !m_pty || m_cellWidth <= 0 || m_cellHeight <= 0 )
        return;

    wxSize clientSize = GetClientSize();
    int    cols = std::max( 2, clientSize.GetWidth() / m_cellWidth );
    int    rows = std::max( 2, clientSize.GetHeight() / m_cellHeight );

    if( cols != m_vterm->GetCols() || rows != m_vterm->GetRows() )
    {
        m_pty->Resize( cols, rows );
        m_vterm->Resize( rows, cols );
        m_needsFullRedraw = true;
    }
}


// ---- Rendering ----

void PTY_TERMINAL_PANEL::RenderToBackBuffer()
{
    if( !m_vterm )
        return;

    wxSize clientSize = GetClientSize();

    if( clientSize.GetWidth() <= 0 || clientSize.GetHeight() <= 0 )
        return;

    // Create or resize back-buffer
    if( !m_backBuffer.IsOk()
        || m_backBuffer.GetWidth() != clientSize.GetWidth()
        || m_backBuffer.GetHeight() != clientSize.GetHeight() )
    {
        m_backBuffer = wxBitmap( clientSize.GetWidth(), clientSize.GetHeight() );
        m_needsFullRedraw = true;
    }

    wxMemoryDC dc( m_backBuffer );

    // Reset cached GDI state for this render pass
    m_lastBgColor = wxColour();
    m_lastFgColor = wxColour();
    m_lastFontIdx = -1;

    if( m_needsFullRedraw )
    {
        // Clear with background color
        dc.SetBackground( wxBrush( wxColour( 30, 30, 30 ) ) );
        dc.Clear();

        int rows = m_vterm->GetRows();
        int cols = m_vterm->GetCols();

        // Render scrollback lines if scrolled back
        if( m_scrollOffset > 0 )
        {
            int scrollbackLines = m_vterm->GetScrollbackLines();

            for( int row = 0; row < rows; row++ )
            {
                int scrollRow = scrollbackLines - m_scrollOffset + row;

                if( scrollRow < 0 )
                    continue;

                if( scrollRow < scrollbackLines )
                {
                    // Render from scrollback
                    for( int col = 0; col < cols; col++ )
                    {
                        TerminalCell cell = m_vterm->GetScrollbackCell( scrollRow, col );

                        // Draw background
                        wxColour bgColor( cell.bg_r, cell.bg_g, cell.bg_b );
                        dc.SetBrush( wxBrush( bgColor ) );
                        dc.SetPen( *wxTRANSPARENT_PEN );
                        dc.DrawRectangle( col * m_cellWidth, row * m_cellHeight,
                                          m_cellWidth * cell.width, m_cellHeight );

                        if( cell.codepoint > ' ' )
                        {
                            wxFont& font = cell.bold
                                           ? ( cell.italic ? m_fontBoldItalic : m_fontBold )
                                           : ( cell.italic ? m_fontItalic : m_fontNormal );
                            dc.SetFont( font );
                            dc.SetTextForeground( wxColour( cell.fg_r, cell.fg_g, cell.fg_b ) );
                            dc.SetTextBackground( bgColor );

                            wxString ch( (wxChar) cell.codepoint );
                            dc.DrawText( ch, col * m_cellWidth, row * m_cellHeight );
                        }

                        if( cell.width > 1 )
                            col += cell.width - 1;
                    }
                }
                else
                {
                    // Render from live screen
                    int liveRow = scrollRow - scrollbackLines;

                    for( int col = 0; col < cols; col++ )
                        RenderCell( dc, liveRow, col );
                }
            }
        }
        else
        {
            // Render live screen
            for( int row = 0; row < rows; row++ )
            {
                for( int col = 0; col < cols; col++ )
                    RenderCell( dc, row, col );
            }
        }

        m_needsFullRedraw = false;
        m_vterm->ClearDamage();
    }
    else
    {
        // Incremental redraw - only damaged cells
        const auto& damage = m_vterm->GetDamage();

        for( const auto& rect : damage )
        {
            for( int row = rect.startRow; row < rect.endRow; row++ )
            {
                for( int col = rect.startCol; col < rect.endCol; col++ )
                    RenderCell( dc, row, col );
            }
        }

        m_vterm->ClearDamage();
    }
}


void PTY_TERMINAL_PANEL::RenderCell( wxMemoryDC& aDC, int aRow, int aCol )
{
    if( !m_vterm )
        return;

    TerminalCell cell = m_vterm->GetCell( aRow, aCol );

    int x = aCol * m_cellWidth;
    int y = aRow * m_cellHeight;

    // Selection highlight
    bool selected = IsCellSelected( aRow, aCol );

    uint8_t bgR = cell.bg_r, bgG = cell.bg_g, bgB = cell.bg_b;
    uint8_t fgR = cell.fg_r, fgG = cell.fg_g, fgB = cell.fg_b;

    if( selected )
    {
        bgR = 68;  bgG = 110; bgB = 155;
        fgR = 255; fgG = 255; fgB = 255;
    }

    // Draw background - use cached brush/pen when colors match
    wxColour bgColor( bgR, bgG, bgB );

    if( bgColor != m_lastBgColor )
    {
        m_lastBgColor = bgColor;
        aDC.SetBrush( wxBrush( bgColor ) );
        aDC.SetPen( *wxTRANSPARENT_PEN );
    }

    aDC.DrawRectangle( x, y, m_cellWidth * std::max( 1, cell.width ), m_cellHeight );

    // Draw character
    if( cell.codepoint > ' ' )
    {
        // Select font variant based on attributes
        int fontIdx = ( cell.bold ? 1 : 0 ) | ( cell.italic ? 2 : 0 );

        if( fontIdx != m_lastFontIdx )
        {
            m_lastFontIdx = fontIdx;

            switch( fontIdx )
            {
            case 0: aDC.SetFont( m_fontNormal );     break;
            case 1: aDC.SetFont( m_fontBold );       break;
            case 2: aDC.SetFont( m_fontItalic );     break;
            case 3: aDC.SetFont( m_fontBoldItalic );  break;
            }
        }

        wxColour fgColor( fgR, fgG, fgB );

        if( fgColor != m_lastFgColor )
        {
            m_lastFgColor = fgColor;
            aDC.SetTextForeground( fgColor );
        }

        wxString ch( (wxChar) cell.codepoint );
        aDC.DrawText( ch, x, y );
    }

    // Underline
    if( cell.underline )
    {
        aDC.SetPen( wxPen( wxColour( fgR, fgG, fgB ), 1 ) );
        int underlineY = y + m_cellHeight - 2;
        aDC.DrawLine( x, underlineY, x + m_cellWidth, underlineY );
        // Reset pen state since we changed it
        aDC.SetPen( *wxTRANSPARENT_PEN );
    }

    // Strikethrough
    if( cell.strikethrough )
    {
        aDC.SetPen( wxPen( wxColour( fgR, fgG, fgB ), 1 ) );
        int strikeY = y + m_cellHeight / 2;
        aDC.DrawLine( x, strikeY, x + m_cellWidth, strikeY );
        aDC.SetPen( *wxTRANSPARENT_PEN );
    }
}


void PTY_TERMINAL_PANEL::RenderCursor( wxDC& aDC )
{
    if( !m_vterm || !m_cursorBlinkOn || m_scrollOffset > 0 )
        return;

    if( !m_vterm->IsCursorVisible() )
        return;

    int cursorRow, cursorCol;
    m_vterm->GetCursorPos( cursorRow, cursorCol );

    int x = cursorCol * m_cellWidth;
    int y = cursorRow * m_cellHeight;

    // Draw block cursor
    if( FindFocus() == this )
    {
        // Filled cursor when focused
        aDC.SetBrush( wxBrush( wxColour( 200, 200, 200 ) ) );
        aDC.SetPen( *wxTRANSPARENT_PEN );
        aDC.DrawRectangle( x, y, m_cellWidth, m_cellHeight );

        // Draw the character under the cursor in the bg color
        TerminalCell cell = m_vterm->GetCell( cursorRow, cursorCol );

        if( cell.codepoint > ' ' )
        {
            aDC.SetFont( m_fontNormal );
            aDC.SetTextForeground( wxColour( 30, 30, 30 ) );
            aDC.SetTextBackground( wxColour( 200, 200, 200 ) );

            wxString ch( (wxChar) cell.codepoint );
            aDC.DrawText( ch, x, y );
        }
    }
    else
    {
        // Hollow cursor when unfocused
        aDC.SetBrush( *wxTRANSPARENT_BRUSH );
        aDC.SetPen( wxPen( wxColour( 200, 200, 200 ), 1 ) );
        aDC.DrawRectangle( x, y, m_cellWidth, m_cellHeight );
    }
}


void PTY_TERMINAL_PANEL::OnPaint( wxPaintEvent& aEvent )
{
    wxAutoBufferedPaintDC dc( this );

    // Only re-render back buffer if there's actual damage (not just cursor blink)
    if( m_needsFullRedraw || ( m_vterm && !m_vterm->GetDamage().empty() ) )
        RenderToBackBuffer();

    if( m_backBuffer.IsOk() )
    {
        dc.DrawBitmap( m_backBuffer, 0, 0 );
        RenderCursor( dc );
    }
    else
    {
        dc.SetBackground( wxBrush( wxColour( 30, 30, 30 ) ) );
        dc.Clear();
    }
}


void PTY_TERMINAL_PANEL::OnSize( wxSizeEvent& aEvent )
{
    m_needsFullRedraw = true;
    RecalcTermSize();
    Refresh();
    aEvent.Skip();
}


// ---- Input Handling ----

void PTY_TERMINAL_PANEL::OnKeyDown( wxKeyEvent& aEvent )
{
    if( !m_vterm || !m_pty )
    {
        aEvent.Skip();
        return;
    }

    int key = aEvent.GetKeyCode();

    // Handle Cmd+C (copy) and Cmd+V (paste)
    if( aEvent.CmdDown() )
    {
        if( key == 'C' || key == 'c' )
        {
            if( m_hasSelection )
            {
                CopySelection();
                return;
            }
            // If no selection, fall through to send Ctrl+C to terminal
        }

        if( key == 'V' || key == 'v' )
        {
            Paste();
            return;
        }
    }

    // Reset scroll position on any key input
    if( m_scrollOffset > 0 )
    {
        m_scrollOffset = 0;
        m_needsFullRedraw = true;
        Refresh();
    }

    // Clear selection on key input
    m_hasSelection = false;

    // Build modifier mask
    VTermModifier mods = VTERM_MOD_NONE;

    if( aEvent.ControlDown() ) mods = (VTermModifier)( mods | VTERM_MOD_CTRL );
    if( aEvent.AltDown() )     mods = (VTermModifier)( mods | VTERM_MOD_ALT );
    if( aEvent.ShiftDown() )   mods = (VTermModifier)( mods | VTERM_MOD_SHIFT );

    // Map wx keys to vterm keys
    VTermKey vtKey = VTERM_KEY_NONE;

    switch( key )
    {
    case WXK_RETURN:
    case WXK_NUMPAD_ENTER:  vtKey = VTERM_KEY_ENTER;     break;
    case WXK_BACK:          vtKey = VTERM_KEY_BACKSPACE;  break;
    case WXK_TAB:           vtKey = VTERM_KEY_TAB;        break;
    case WXK_ESCAPE:        vtKey = VTERM_KEY_ESCAPE;     break;
    case WXK_UP:            vtKey = VTERM_KEY_UP;         break;
    case WXK_DOWN:          vtKey = VTERM_KEY_DOWN;       break;
    case WXK_LEFT:          vtKey = VTERM_KEY_LEFT;       break;
    case WXK_RIGHT:         vtKey = VTERM_KEY_RIGHT;      break;
    case WXK_HOME:          vtKey = VTERM_KEY_HOME;       break;
    case WXK_END:           vtKey = VTERM_KEY_END;        break;
    case WXK_PAGEUP:        vtKey = VTERM_KEY_PAGEUP;     break;
    case WXK_PAGEDOWN:      vtKey = VTERM_KEY_PAGEDOWN;   break;
    case WXK_DELETE:        vtKey = VTERM_KEY_DEL;        break;
    case WXK_INSERT:        vtKey = VTERM_KEY_INS;        break;
    case WXK_F1: case WXK_F2: case WXK_F3: case WXK_F4:
    case WXK_F5: case WXK_F6: case WXK_F7: case WXK_F8:
    case WXK_F9: case WXK_F10: case WXK_F11: case WXK_F12:
        vtKey = (VTermKey)( VTERM_KEY_FUNCTION_0 + ( key - WXK_F1 + 1 ) );
        break;
    default:
        // For printable characters and Ctrl+letter, handle in OnChar
        aEvent.Skip();
        return;
    }

    if( vtKey != VTERM_KEY_NONE )
        m_vterm->KeyboardKey( vtKey, mods );
}


void PTY_TERMINAL_PANEL::OnChar( wxKeyEvent& aEvent )
{
    if( !m_vterm || !m_pty )
    {
        aEvent.Skip();
        return;
    }

    // Don't process Cmd+key combinations as characters
    if( aEvent.CmdDown() )
    {
        aEvent.Skip();
        return;
    }

    wxChar ch = aEvent.GetUnicodeKey();

    if( ch == WXK_NONE )
    {
        aEvent.Skip();
        return;
    }

    VTermModifier mods = VTERM_MOD_NONE;

    // On macOS, Ctrl+letter needs special handling
    if( aEvent.ControlDown() && ch >= 'a' && ch <= 'z' )
    {
        mods = (VTermModifier)( mods | VTERM_MOD_CTRL );
    }

    if( aEvent.AltDown() )
    {
        // macOS Option key: send as Alt/Meta prefix
        mods = (VTermModifier)( mods | VTERM_MOD_ALT );
    }

    m_vterm->KeyboardUnichar( (uint32_t) ch, mods );
}


// ---- Mouse ----

void PTY_TERMINAL_PANEL::PixelToCell( int aPixelX, int aPixelY, int& aCol, int& aRow ) const
{
    aCol = ( m_cellWidth > 0 ) ? ( aPixelX / m_cellWidth ) : 0;
    aRow = ( m_cellHeight > 0 ) ? ( aPixelY / m_cellHeight ) : 0;

    if( m_vterm )
    {
        aCol = std::max( 0, std::min( aCol, m_vterm->GetCols() - 1 ) );
        aRow = std::max( 0, std::min( aRow, m_vterm->GetRows() - 1 ) );
    }
}


void PTY_TERMINAL_PANEL::OnMouseDown( wxMouseEvent& aEvent )
{
    SetFocus();

    int col, row;
    PixelToCell( aEvent.GetX(), aEvent.GetY(), col, row );

    m_selecting = true;
    m_selStartRow = row;
    m_selStartCol = col;
    m_selEndRow = row;
    m_selEndCol = col;
    m_hasSelection = false;

    CaptureMouse();
    Refresh();
}


void PTY_TERMINAL_PANEL::OnMouseMove( wxMouseEvent& aEvent )
{
    if( !m_selecting )
        return;

    int col, row;
    PixelToCell( aEvent.GetX(), aEvent.GetY(), col, row );

    m_selEndRow = row;
    m_selEndCol = col;
    m_hasSelection = ( m_selStartRow != m_selEndRow || m_selStartCol != m_selEndCol );

    m_needsFullRedraw = true;
    Refresh();
}


void PTY_TERMINAL_PANEL::OnMouseUp( wxMouseEvent& aEvent )
{
    if( !m_selecting )
        return;

    m_selecting = false;

    if( HasCapture() )
        ReleaseMouse();

    int col, row;
    PixelToCell( aEvent.GetX(), aEvent.GetY(), col, row );

    m_selEndRow = row;
    m_selEndCol = col;
    m_hasSelection = ( m_selStartRow != m_selEndRow || m_selStartCol != m_selEndCol );

    Refresh();
}


void PTY_TERMINAL_PANEL::OnMouseWheel( wxMouseEvent& aEvent )
{
    if( !m_vterm )
        return;

    int rotation = aEvent.GetWheelRotation();
    int delta = aEvent.GetWheelDelta();
    int lines = rotation / delta;

    if( lines == 0 )
        lines = ( rotation > 0 ) ? 1 : -1;

    int maxScroll = m_vterm->GetScrollbackLines();

    m_scrollOffset = std::max( 0, std::min( m_scrollOffset - lines * 3, maxScroll ) );

    m_needsFullRedraw = true;
    ScheduleRender();
}


bool PTY_TERMINAL_PANEL::IsCellSelected( int aRow, int aCol ) const
{
    if( !m_hasSelection )
        return false;

    // Normalize selection range
    int startRow = m_selStartRow, startCol = m_selStartCol;
    int endRow = m_selEndRow, endCol = m_selEndCol;

    if( startRow > endRow || ( startRow == endRow && startCol > endCol ) )
    {
        std::swap( startRow, endRow );
        std::swap( startCol, endCol );
    }

    if( aRow < startRow || aRow > endRow )
        return false;

    if( startRow == endRow )
        return aCol >= startCol && aCol <= endCol;

    if( aRow == startRow )
        return aCol >= startCol;

    if( aRow == endRow )
        return aCol <= endCol;

    return true; // Between start and end rows
}


// ---- Focus ----

void PTY_TERMINAL_PANEL::OnSetFocus( wxFocusEvent& aEvent )
{
    m_cursorBlinkOn = true;

    // Only refresh the cursor area, not the whole terminal
    if( m_vterm )
    {
        int cursorRow, cursorCol;
        m_vterm->GetCursorPos( cursorRow, cursorCol );
        RefreshRect( wxRect( cursorCol * m_cellWidth, cursorRow * m_cellHeight,
                             m_cellWidth, m_cellHeight ) );
    }

    aEvent.Skip();
}


void PTY_TERMINAL_PANEL::OnKillFocus( wxFocusEvent& aEvent )
{
    m_cursorBlinkOn = true; // Show cursor (hollow) when unfocused

    if( m_vterm )
    {
        int cursorRow, cursorCol;
        m_vterm->GetCursorPos( cursorRow, cursorCol );
        RefreshRect( wxRect( cursorCol * m_cellWidth, cursorRow * m_cellHeight,
                             m_cellWidth, m_cellHeight ) );
    }

    aEvent.Skip();
}


// ---- PTY Events ----

void PTY_TERMINAL_PANEL::OnPtyData( wxThreadEvent& aEvent )
{
    if( !m_vterm )
        return;

    std::string data = aEvent.GetPayload<std::string>();

    // Feed data to vterm
    m_vterm->ProcessInput( data.c_str(), data.size() );

    // Process agent capture if active
    if( m_agentCapturing )
        ProcessAgentCapture( data );

    // Schedule a coalesced render instead of rendering on every data chunk
    ScheduleRender();
}


void PTY_TERMINAL_PANEL::ScheduleRender()
{
    if( !m_renderPending )
    {
        m_renderPending = true;
        m_renderTimer.StartOnce( RENDER_INTERVAL_MS );
    }
}


void PTY_TERMINAL_PANEL::OnRenderTimer( wxTimerEvent& aEvent )
{
    m_renderPending = false;
    Refresh();
}


void PTY_TERMINAL_PANEL::OnPtyExit( wxThreadEvent& aEvent )
{
    wxLogInfo( "PTY_TERMINAL: Shell process exited" );

    m_cursorBlinkTimer.Stop();

    // If agent is waiting, notify with error
    if( m_agentCapturing && m_agentCallback )
    {
        m_agentTimeoutTimer.Stop();
        m_agentCapturing = false;
        m_agentCallback( "Error: Shell process exited", false );
        m_agentCallback = nullptr;
    }
}


void PTY_TERMINAL_PANEL::OnCursorBlink( wxTimerEvent& aEvent )
{
    m_cursorBlinkOn = !m_cursorBlinkOn;

    // Only invalidate the cursor cell area, not the entire terminal
    if( m_vterm )
    {
        int cursorRow, cursorCol;
        m_vterm->GetCursorPos( cursorRow, cursorCol );

        wxRect cursorRect( cursorCol * m_cellWidth, cursorRow * m_cellHeight,
                           m_cellWidth, m_cellHeight );
        RefreshRect( cursorRect );
    }
}


// ---- Clipboard ----

void PTY_TERMINAL_PANEL::CopySelection()
{
    std::string text = GetSelectedText();

    if( text.empty() )
        return;

    if( wxTheClipboard->Open() )
    {
        wxTheClipboard->SetData( new wxTextDataObject( wxString::FromUTF8( text ) ) );
        wxTheClipboard->Close();
    }
}


void PTY_TERMINAL_PANEL::Paste()
{
    if( !m_pty )
        return;

    if( wxTheClipboard->Open() )
    {
        if( wxTheClipboard->IsSupported( wxDF_TEXT ) )
        {
            wxTextDataObject data;
            wxTheClipboard->GetData( data );

            std::string text = data.GetText().ToStdString();

            if( !text.empty() )
                m_pty->Write( text );
        }

        wxTheClipboard->Close();
    }
}


std::string PTY_TERMINAL_PANEL::GetSelectedText() const
{
    if( !m_hasSelection || !m_vterm )
        return "";

    int startRow = m_selStartRow, startCol = m_selStartCol;
    int endRow = m_selEndRow, endCol = m_selEndCol;

    if( startRow > endRow || ( startRow == endRow && startCol > endCol ) )
    {
        std::swap( startRow, endRow );
        std::swap( startCol, endCol );
    }

    std::string result;
    int         cols = m_vterm->GetCols();

    for( int row = startRow; row <= endRow; row++ )
    {
        int colStart = ( row == startRow ) ? startCol : 0;
        int colEnd = ( row == endRow ) ? endCol : cols - 1;

        for( int col = colStart; col <= colEnd; col++ )
        {
            TerminalCell cell = m_vterm->GetCell( row, col );

            if( cell.codepoint > 0 )
            {
                char utf8[4];
                int  len = 0;

                if( cell.codepoint < 0x80 )
                {
                    utf8[0] = (char) cell.codepoint;
                    len = 1;
                }
                else if( cell.codepoint < 0x800 )
                {
                    utf8[0] = (char)( 0xC0 | ( cell.codepoint >> 6 ) );
                    utf8[1] = (char)( 0x80 | ( cell.codepoint & 0x3F ) );
                    len = 2;
                }
                else if( cell.codepoint < 0x10000 )
                {
                    utf8[0] = (char)( 0xE0 | ( cell.codepoint >> 12 ) );
                    utf8[1] = (char)( 0x80 | ( ( cell.codepoint >> 6 ) & 0x3F ) );
                    utf8[2] = (char)( 0x80 | ( cell.codepoint & 0x3F ) );
                    len = 3;
                }
                else
                {
                    utf8[0] = (char)( 0xF0 | ( cell.codepoint >> 18 ) );
                    utf8[1] = (char)( 0x80 | ( ( cell.codepoint >> 12 ) & 0x3F ) );
                    utf8[2] = (char)( 0x80 | ( ( cell.codepoint >> 6 ) & 0x3F ) );
                    utf8[3] = (char)( 0x80 | ( cell.codepoint & 0x3F ) );
                    len = 4;
                }

                result.append( utf8, len );
            }
        }

        if( row < endRow )
            result += '\n';
    }

    return result;
}


// ---- Agent Integration ----

void PTY_TERMINAL_PANEL::WriteToShell( const std::string& aData )
{
    if( m_pty )
        m_pty->Write( aData );
}


void PTY_TERMINAL_PANEL::DisplayAgentCommand( const wxString& aCmd, const wxString& aMode )
{
    // Write a visible header to the terminal via the PTY
    // This shows up in the terminal as regular text from the shell
    // We don't write directly because the PTY/vterm handles all rendering
    wxString header;

    if( aMode == "shell" )
        header = "# [agent:shell] " + aCmd + "\n";
    else
        header = "# [agent:" + aMode + "] " + aCmd + "\n";

    // Write as a comment so it doesn't execute
    // The actual command will be sent separately via ExecuteForAgent
}


void PTY_TERMINAL_PANEL::ExecuteForAgent( const std::string& aCmd, AgentCallback aCallback )
{
    if( !m_pty || !m_pty->IsRunning() )
    {
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


void PTY_TERMINAL_PANEL::ProcessAgentCapture( const std::string& aData )
{
    m_agentCaptureBuffer += aData;

    // Look for start sentinel
    if( !m_agentSentinelStartFound )
    {
        size_t startPos = m_agentCaptureBuffer.find( m_agentSentinelStart );

        if( startPos != std::string::npos )
        {
            m_agentSentinelStartFound = true;

            // Remove everything up to and including the start sentinel + newline
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
            // Extract the output between sentinels
            std::string output = m_agentCaptureBuffer.substr( 0, endPos );

            // Trim trailing newline
            while( !output.empty() && output.back() == '\n' )
                output.pop_back();

            // Stop capturing
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


void PTY_TERMINAL_PANEL::OnAgentTimeout( wxTimerEvent& aEvent )
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
