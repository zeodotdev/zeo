#ifndef PTY_TERMINAL_PANEL_H
#define PTY_TERMINAL_PANEL_H

#include <wx/panel.h>
#include <wx/timer.h>
#include <wx/bitmap.h>
#include <wx/font.h>
#include <memory>
#include <string>
#include <functional>
#include <atomic>

class PTY_HANDLER;
class VTERM_HANDLER;

class PTY_TERMINAL_PANEL : public wxPanel
{
public:
    PTY_TERMINAL_PANEL( wxWindow* aParent );
    virtual ~PTY_TERMINAL_PANEL();

    bool StartShell();
    bool IsShellRunning() const;

    virtual wxString GetTitle() const;

    // For agent integration: execute command and capture output via sentinel
    using AgentCallback = std::function<void( const std::string&, bool )>;
    void ExecuteForAgent( const std::string& aCmd, AgentCallback aCallback );

    // Write raw data to PTY (for agent DisplayAgentCommand)
    void WriteToShell( const std::string& aData );

    // Display a command header in the terminal (for agent visibility)
    void DisplayAgentCommand( const wxString& aCmd, const wxString& aMode );

    int GetTermCols() const;
    int GetTermRows() const;

protected:
    std::unique_ptr<PTY_HANDLER>   m_pty;
    std::unique_ptr<VTERM_HANDLER> m_vterm;

    // Rendering
    wxBitmap m_backBuffer;
    wxFont   m_fontNormal;
    wxFont   m_fontBold;
    wxFont   m_fontItalic;
    wxFont   m_fontBoldItalic;
    int      m_cellWidth;
    int      m_cellHeight;
    int      m_fontAscent;
    double   m_scaleFactor;
    bool     m_needsFullRedraw;

    // Cached GDI state to avoid redundant SetFont/SetBrush/SetPen per cell
    wxColour m_lastBgColor;
    wxColour m_lastFgColor;
    int      m_lastFontIdx;  // 0=normal, 1=bold, 2=italic, 3=bolditalic

    // Cursor
    wxTimer m_cursorBlinkTimer;
    bool    m_cursorBlinkOn;

    // Render coalescing: instead of Refresh() on every PTY data event,
    // we set a dirty flag and a short timer triggers the actual repaint
    wxTimer m_renderTimer;
    bool    m_renderPending;
    static const int RENDER_INTERVAL_MS = 16; // ~60fps

    // Selection
    bool m_selecting;
    int  m_selStartRow, m_selStartCol;
    int  m_selEndRow, m_selEndCol;
    bool m_hasSelection;

    // Scrollback view offset (0 = live, >0 = viewing scrollback)
    int m_scrollOffset;

    // Terminal title (set by shell via escape sequences)
    wxString m_termTitle;

    // Agent capture state
    AgentCallback m_agentCallback;
    bool          m_agentCapturing;
    std::string   m_agentCaptureBuffer;
    std::string   m_agentSentinelStart;
    std::string   m_agentSentinelEnd;
    bool          m_agentSentinelStartFound;
    wxTimer       m_agentTimeoutTimer;

    // Calculate terminal dimensions from pixel size
    void RecalcTermSize();
    void MeasureFont();

    // Rendering
    void OnPaint( wxPaintEvent& aEvent );
    void OnSize( wxSizeEvent& aEvent );
    void RenderToBackBuffer();
    void RenderCell( wxMemoryDC& aDC, int aRow, int aCol );
    void RenderCursor( wxDC& aDC );

    // Input
    void OnKeyDown( wxKeyEvent& aEvent );
    void OnChar( wxKeyEvent& aEvent );
    void OnMouseDown( wxMouseEvent& aEvent );
    void OnMouseMove( wxMouseEvent& aEvent );
    void OnMouseUp( wxMouseEvent& aEvent );
    void OnMouseWheel( wxMouseEvent& aEvent );

    // Focus
    void OnSetFocus( wxFocusEvent& aEvent );
    void OnKillFocus( wxFocusEvent& aEvent );

    // PTY events
    void OnPtyData( wxThreadEvent& aEvent );
    void OnPtyExit( wxThreadEvent& aEvent );

    // Cursor blink
    void OnCursorBlink( wxTimerEvent& aEvent );

    // Render timer
    void OnRenderTimer( wxTimerEvent& aEvent );
    void ScheduleRender();

    // Agent timeout
    void OnAgentTimeout( wxTimerEvent& aEvent );

    // Clipboard
    void CopySelection();
    void Paste();
    std::string GetSelectedText() const;

    // Helpers
    void PixelToCell( int aPixelX, int aPixelY, int& aCol, int& aRow ) const;
    bool IsCellSelected( int aRow, int aCol ) const;

    // Process agent capture data
    void ProcessAgentCapture( const std::string& aData );
};

#endif // PTY_TERMINAL_PANEL_H
