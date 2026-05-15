#ifndef VTERM_HANDLER_H
#define VTERM_HANDLER_H

#include <vterm.h>
#include <functional>
#include <vector>
#include <string>
#include <cstdint>

struct TerminalCell
{
    uint32_t codepoint;     // Unicode codepoint (0 = empty)
    uint8_t  fg_r, fg_g, fg_b;
    uint8_t  bg_r, bg_g, bg_b;
    bool     bold;
    bool     italic;
    bool     underline;
    bool     inverse;
    bool     strikethrough;
    int      width;         // 1 for normal, 2 for wide chars
};

struct DamageRect
{
    int startRow, startCol, endRow, endCol;
};

class VTERM_HANDLER
{
public:
    VTERM_HANDLER( int aRows, int aCols );
    ~VTERM_HANDLER();

    // Feed raw bytes from PTY into the terminal state machine
    void ProcessInput( const char* aData, size_t aLen );

    // Get a cell from the current screen buffer
    TerminalCell GetCell( int aRow, int aCol ) const;

    // Get cursor position and visibility
    void GetCursorPos( int& aRow, int& aCol ) const;
    bool IsCursorVisible() const;

    // Resize terminal
    void Resize( int aRows, int aCols );

    int GetRows() const { return m_rows; }
    int GetCols() const { return m_cols; }

    // Scrollback
    int          GetScrollbackLines() const;
    TerminalCell GetScrollbackCell( int aLine, int aCol ) const;

    // Damage tracking
    const std::vector<DamageRect>& GetDamage() const { return m_damage; }
    void ClearDamage() { m_damage.clear(); }

    // Mark the entire screen as damaged (for full redraws)
    void DamageAll();

    // Encode keyboard input for writing to PTY
    void KeyboardKey( VTermKey aKey, VTermModifier aMods );
    void KeyboardUnichar( uint32_t aChar, VTermModifier aMods );

    // Callbacks
    using OutputCallback = std::function<void( const char*, size_t )>;
    void SetOutputCallback( OutputCallback aCb ) { m_outputCb = aCb; }

    using BellCallback = std::function<void()>;
    void SetBellCallback( BellCallback aCb ) { m_bellCb = aCb; }

    using TitleCallback = std::function<void( const std::string& )>;
    void SetTitleCallback( TitleCallback aCb ) { m_titleCb = aCb; }

private:
    VTerm*       m_vterm;
    VTermScreen* m_screen;
    int          m_rows;
    int          m_cols;
    int          m_cursorRow;
    int          m_cursorCol;
    bool         m_cursorVisible;

    std::vector<DamageRect> m_damage;

    // Scrollback buffer
    static constexpr int MAX_SCROLLBACK = 10000;
    std::vector<std::vector<TerminalCell>> m_scrollback;

    OutputCallback m_outputCb;
    BellCallback   m_bellCb;
    TitleCallback  m_titleCb;

    // Default colors
    uint8_t m_defaultFgR, m_defaultFgG, m_defaultFgB;
    uint8_t m_defaultBgR, m_defaultBgG, m_defaultBgB;

    // Convert VTermColor to RGB
    void ColorToRGB( VTermColor aColor, uint8_t& aR, uint8_t& aG, uint8_t& aB ) const;

    // Convert VTermScreenCell to our TerminalCell
    TerminalCell ConvertCell( const VTermScreenCell& aCell ) const;

    // libvterm callbacks
    static int  OnDamage( VTermRect rect, void* user );
    static int  OnMoveCursor( VTermPos pos, VTermPos oldpos, int visible, void* user );
    static int  OnBell( void* user );
    static int  OnSetTermProp( VTermProp prop, VTermValue* val, void* user );
    static int  OnScrollbackPushLine( int cols, const VTermScreenCell* cells, void* user );
    static int  OnScrollbackPopLine( int cols, VTermScreenCell* cells, void* user );
    static void OnOutput( const char* bytes, size_t len, void* user );
};

#endif // VTERM_HANDLER_H
