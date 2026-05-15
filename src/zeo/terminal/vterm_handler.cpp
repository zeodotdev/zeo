#include "vterm_handler.h"

#include <cstring>
#include <algorithm>


VTERM_HANDLER::VTERM_HANDLER( int aRows, int aCols ) :
        m_vterm( nullptr ),
        m_screen( nullptr ),
        m_rows( aRows ),
        m_cols( aCols ),
        m_cursorRow( 0 ),
        m_cursorCol( 0 ),
        m_cursorVisible( true ),
        m_defaultFgR( 255 ), m_defaultFgG( 255 ), m_defaultFgB( 255 ),
        m_defaultBgR( 30 ),  m_defaultBgG( 30 ),  m_defaultBgB( 30 )
{
    m_vterm = vterm_new( aRows, aCols );

    vterm_set_utf8( m_vterm, 1 );

    // Set up output callback (for terminal responses like cursor position reports)
    vterm_output_set_callback( m_vterm, &VTERM_HANDLER::OnOutput, this );

    // Get the screen and set up callbacks
    m_screen = vterm_obtain_screen( m_vterm );

    static VTermScreenCallbacks screenCallbacks;
    memset( &screenCallbacks, 0, sizeof( screenCallbacks ) );
    screenCallbacks.damage      = &VTERM_HANDLER::OnDamage;
    screenCallbacks.movecursor  = &VTERM_HANDLER::OnMoveCursor;
    screenCallbacks.bell        = &VTERM_HANDLER::OnBell;
    screenCallbacks.settermprop = &VTERM_HANDLER::OnSetTermProp;
    screenCallbacks.sb_pushline = &VTERM_HANDLER::OnScrollbackPushLine;
    screenCallbacks.sb_popline  = &VTERM_HANDLER::OnScrollbackPopLine;

    vterm_screen_set_callbacks( m_screen, &screenCallbacks, this );
    vterm_screen_set_damage_merge( m_screen, VTERM_DAMAGE_SCROLL );

    // Enable alternate screen
    vterm_screen_enable_altscreen( m_screen, 1 );

    // Enable scrollback
    vterm_screen_enable_reflow( m_screen, 1 );

    vterm_screen_reset( m_screen, 1 );
}


VTERM_HANDLER::~VTERM_HANDLER()
{
    if( m_vterm )
        vterm_free( m_vterm );
}


void VTERM_HANDLER::ProcessInput( const char* aData, size_t aLen )
{
    if( m_vterm && aLen > 0 )
        vterm_input_write( m_vterm, aData, aLen );
}


TerminalCell VTERM_HANDLER::GetCell( int aRow, int aCol ) const
{
    VTermPos pos;
    pos.row = aRow;
    pos.col = aCol;

    VTermScreenCell cell;
    vterm_screen_get_cell( m_screen, pos, &cell );

    return ConvertCell( cell );
}


void VTERM_HANDLER::GetCursorPos( int& aRow, int& aCol ) const
{
    aRow = m_cursorRow;
    aCol = m_cursorCol;
}


bool VTERM_HANDLER::IsCursorVisible() const
{
    return m_cursorVisible;
}


void VTERM_HANDLER::Resize( int aRows, int aCols )
{
    if( m_vterm )
    {
        m_rows = aRows;
        m_cols = aCols;
        vterm_set_size( m_vterm, aRows, aCols );
    }
}


int VTERM_HANDLER::GetScrollbackLines() const
{
    return (int) m_scrollback.size();
}


TerminalCell VTERM_HANDLER::GetScrollbackCell( int aLine, int aCol ) const
{
    if( aLine < 0 || aLine >= (int) m_scrollback.size() )
    {
        TerminalCell empty = {};
        empty.codepoint = ' ';
        empty.fg_r = m_defaultFgR; empty.fg_g = m_defaultFgG; empty.fg_b = m_defaultFgB;
        empty.bg_r = m_defaultBgR; empty.bg_g = m_defaultBgG; empty.bg_b = m_defaultBgB;
        empty.width = 1;
        return empty;
    }

    const auto& row = m_scrollback[aLine];

    if( aCol < 0 || aCol >= (int) row.size() )
    {
        TerminalCell empty = {};
        empty.codepoint = ' ';
        empty.fg_r = m_defaultFgR; empty.fg_g = m_defaultFgG; empty.fg_b = m_defaultFgB;
        empty.bg_r = m_defaultBgR; empty.bg_g = m_defaultBgG; empty.bg_b = m_defaultBgB;
        empty.width = 1;
        return empty;
    }

    return row[aCol];
}


void VTERM_HANDLER::DamageAll()
{
    DamageRect rect;
    rect.startRow = 0;
    rect.startCol = 0;
    rect.endRow = m_rows;
    rect.endCol = m_cols;
    m_damage.push_back( rect );
}


void VTERM_HANDLER::KeyboardKey( VTermKey aKey, VTermModifier aMods )
{
    if( m_vterm )
        vterm_keyboard_key( m_vterm, aKey, aMods );
}


void VTERM_HANDLER::KeyboardUnichar( uint32_t aChar, VTermModifier aMods )
{
    if( m_vterm )
        vterm_keyboard_unichar( m_vterm, aChar, aMods );
}


void VTERM_HANDLER::ColorToRGB( VTermColor aColor, uint8_t& aR, uint8_t& aG, uint8_t& aB ) const
{
    // Ensure color is resolved to RGB
    vterm_screen_convert_color_to_rgb( m_screen, &aColor );

    aR = aColor.rgb.red;
    aG = aColor.rgb.green;
    aB = aColor.rgb.blue;
}


TerminalCell VTERM_HANDLER::ConvertCell( const VTermScreenCell& aCell ) const
{
    TerminalCell tc = {};

    // Get the character
    if( aCell.chars[0] == 0 )
        tc.codepoint = ' ';
    else
        tc.codepoint = aCell.chars[0];

    // Get colors
    ColorToRGB( aCell.fg, tc.fg_r, tc.fg_g, tc.fg_b );
    ColorToRGB( aCell.bg, tc.bg_r, tc.bg_g, tc.bg_b );

    // If colors are default (index 0), use our defaults
    if( VTERM_COLOR_IS_DEFAULT_FG( &aCell.fg ) )
    {
        tc.fg_r = m_defaultFgR;
        tc.fg_g = m_defaultFgG;
        tc.fg_b = m_defaultFgB;
    }

    if( VTERM_COLOR_IS_DEFAULT_BG( &aCell.bg ) )
    {
        tc.bg_r = m_defaultBgR;
        tc.bg_g = m_defaultBgG;
        tc.bg_b = m_defaultBgB;
    }

    // Attributes
    tc.bold          = aCell.attrs.bold;
    tc.italic        = aCell.attrs.italic;
    tc.underline     = aCell.attrs.underline != 0;
    tc.strikethrough = aCell.attrs.strike;
    tc.inverse       = aCell.attrs.reverse;
    tc.width         = aCell.width;

    // Handle inverse
    if( tc.inverse )
    {
        std::swap( tc.fg_r, tc.bg_r );
        std::swap( tc.fg_g, tc.bg_g );
        std::swap( tc.fg_b, tc.bg_b );
    }

    return tc;
}


// libvterm callbacks

int VTERM_HANDLER::OnDamage( VTermRect rect, void* user )
{
    auto* handler = static_cast<VTERM_HANDLER*>( user );

    DamageRect dmg;
    dmg.startRow = rect.start_row;
    dmg.startCol = rect.start_col;
    dmg.endRow   = rect.end_row;
    dmg.endCol   = rect.end_col;
    handler->m_damage.push_back( dmg );

    return 0;
}


int VTERM_HANDLER::OnMoveCursor( VTermPos pos, VTermPos oldpos, int visible, void* user )
{
    auto* handler = static_cast<VTERM_HANDLER*>( user );
    handler->m_cursorRow = pos.row;
    handler->m_cursorCol = pos.col;
    handler->m_cursorVisible = ( visible != 0 );
    return 0;
}


int VTERM_HANDLER::OnBell( void* user )
{
    auto* handler = static_cast<VTERM_HANDLER*>( user );

    if( handler->m_bellCb )
        handler->m_bellCb();

    return 0;
}


int VTERM_HANDLER::OnSetTermProp( VTermProp prop, VTermValue* val, void* user )
{
    auto* handler = static_cast<VTERM_HANDLER*>( user );

    switch( prop )
    {
    case VTERM_PROP_TITLE:
        if( handler->m_titleCb && val->string.str )
            handler->m_titleCb( std::string( val->string.str, val->string.len ) );
        break;

    case VTERM_PROP_CURSORVISIBLE:
        handler->m_cursorVisible = val->boolean;
        break;

    default:
        break;
    }

    return 0;
}


int VTERM_HANDLER::OnScrollbackPushLine( int cols, const VTermScreenCell* cells, void* user )
{
    auto* handler = static_cast<VTERM_HANDLER*>( user );

    std::vector<TerminalCell> row;
    row.reserve( cols );

    for( int i = 0; i < cols; i++ )
        row.push_back( handler->ConvertCell( cells[i] ) );

    handler->m_scrollback.push_back( std::move( row ) );

    // Limit scrollback buffer
    while( (int) handler->m_scrollback.size() > MAX_SCROLLBACK )
        handler->m_scrollback.erase( handler->m_scrollback.begin() );

    return 0;
}


int VTERM_HANDLER::OnScrollbackPopLine( int cols, VTermScreenCell* cells, void* user )
{
    auto* handler = static_cast<VTERM_HANDLER*>( user );

    if( handler->m_scrollback.empty() )
        return 0;

    // Pop the last line from scrollback
    auto& row = handler->m_scrollback.back();

    for( int i = 0; i < cols; i++ )
    {
        memset( &cells[i], 0, sizeof( VTermScreenCell ) );

        if( i < (int) row.size() )
        {
            cells[i].chars[0] = row[i].codepoint;
            cells[i].width = row[i].width;

            // Set colors - they need to be RGB type
            cells[i].fg.type = VTERM_COLOR_RGB;
            cells[i].fg.rgb.red   = row[i].fg_r;
            cells[i].fg.rgb.green = row[i].fg_g;
            cells[i].fg.rgb.blue  = row[i].fg_b;

            cells[i].bg.type = VTERM_COLOR_RGB;
            cells[i].bg.rgb.red   = row[i].bg_r;
            cells[i].bg.rgb.green = row[i].bg_g;
            cells[i].bg.rgb.blue  = row[i].bg_b;

            cells[i].attrs.bold      = row[i].bold;
            cells[i].attrs.italic    = row[i].italic;
            cells[i].attrs.underline = row[i].underline ? 1 : 0;
            cells[i].attrs.strike    = row[i].strikethrough;
            cells[i].attrs.reverse   = row[i].inverse;
        }
        else
        {
            cells[i].chars[0] = ' ';
            cells[i].width = 1;
        }
    }

    handler->m_scrollback.pop_back();

    return 1;
}


void VTERM_HANDLER::OnOutput( const char* bytes, size_t len, void* user )
{
    auto* handler = static_cast<VTERM_HANDLER*>( user );

    if( handler->m_outputCb )
        handler->m_outputCb( bytes, len );
}
