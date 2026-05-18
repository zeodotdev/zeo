/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2023 Mark Roszko <mark.roszko@gmail.com>
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 * Copyright (C) 2026, Zeo <team@zeo.dev>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#include <wx/button.h>
#include <wx/statusbr.h>
#include <wx/gauge.h>
#include <wx/stattext.h>
#include <wx/statbmp.h>
#include <wx/tokenzr.h>
#include <fmt/format.h>
#include <array>
#include <ranges>
#include <vector>
#include <widgets/kistatusbar.h>
#include <widgets/wx_html_report_box.h>
#include <widgets/bitmap_button.h>
#include <widgets/ui_common.h>
#include <pgm_base.h>
#include <background_jobs_monitor.h>
#include <notifications_manager.h>
#include <bitmaps.h>
#include <reporter.h>
#include <dialog_HTML_reporter_base.h>
#include <trace_helpers.h>
#include <wx/dcclient.h>


class STATUSBAR_WARNING_REPORTER_DIALOG : public DIALOG_HTML_REPORTER
{
public:
    STATUSBAR_WARNING_REPORTER_DIALOG( wxWindow* aParent, KISTATUSBAR* aStatusBar ) :
            DIALOG_HTML_REPORTER( aParent, wxID_ANY, _( "Messages" ) ),
            m_statusBar( aStatusBar )
    {
        m_clearButton = new wxButton( this, wxID_CLEAR, _( "Clear" ) );
        m_clearButton->Bind( wxEVT_BUTTON,
                             &STATUSBAR_WARNING_REPORTER_DIALOG::onClearButtonClick, this );

        m_sdbSizer->Insert( 0, m_clearButton, 0, wxALL, 5 );
        GetSizer()->Layout();
        GetSizer()->Fit( this );
    }

private:
    void onClearButtonClick( wxCommandEvent& aEvent )
    {
        if( m_statusBar )
            m_statusBar->ClearWarningMessages();

        EndModal( wxID_CLEAR );
    }

private:
    KISTATUSBAR* m_statusBar;
    wxButton*    m_clearButton;
};


KISTATUSBAR::KISTATUSBAR( int aNumberFields, wxWindow* parent, wxWindowID id, STYLE_FLAGS aFlags ) :
        wxStatusBar( parent, id ),
        m_backgroundStopButton( nullptr ),
        m_notificationsButton( nullptr ),
        m_warningButton( nullptr ),
        m_labelButton( nullptr ),
        m_profileBitmap( nullptr ),
        m_profileLogicalSize( 18, 18 ),
        m_normalFieldsCount( aNumberFields ),
        m_styleFlags( aFlags )
{
#ifdef __WXOSX__
    // we need +1 extra field on OSX to offset from the rounded corner on the right
    // OSX doesn't use resize grippers like the other platforms and the statusbar field
    // includes the rounded part
    int extraFields = 3;
#else
    int extraFields = 2;
#endif

    bool showNotification = ( m_styleFlags & NOTIFICATION_ICON );
    bool showCancel = ( m_styleFlags & CANCEL_BUTTON );
    bool showWarning = ( m_styleFlags & WARNING_ICON );
    bool showLabel = ( m_styleFlags & LABEL_BUTTON );

    if( showCancel )
        extraFields++;

    if( showWarning )
        extraFields++;

    if( showNotification )
        extraFields++;

    if( showLabel )
        extraFields++;

    SetFieldsCount( aNumberFields + extraFields );

    m_fieldWidths.assign( aNumberFields + extraFields, -1 );

    if( std::optional<int> idx = fieldIndex( FIELD::LABEL ) )
        m_fieldWidths[aNumberFields + *idx] = 35;  // Zeo session label button (18px logical + 17px padding)

#ifdef __WXOSX__
    // offset from the right edge
    m_fieldWidths[aNumberFields + extraFields - 1] = 10;
#endif

    SetStatusWidths( aNumberFields + extraFields, m_fieldWidths.data() );

    int* styles = new int[aNumberFields + extraFields];

    for( int i = 0; i < aNumberFields + extraFields; i++ )
        styles[i] = wxSB_FLAT;

    SetStatusStyles( aNumberFields + extraFields, styles );
    delete[] styles;

    m_backgroundTxt = new wxStaticText( this, wxID_ANY, wxT( "" ), wxDefaultPosition,
                                        wxDefaultSize, wxALIGN_RIGHT | wxST_NO_AUTORESIZE );

    m_backgroundProgressBar = new wxGauge( this, wxID_ANY, 100, wxDefaultPosition, wxDefaultSize,
                                           wxGA_HORIZONTAL | wxGA_SMOOTH );

    if( showCancel )
    {
        m_backgroundStopButton = new wxButton( this, wxID_ANY, "X", wxDefaultPosition,
                                               wxDefaultSize, wxBU_EXACTFIT );
    }

    if( showNotification )
    {
        m_notificationsButton = new BITMAP_BUTTON( this, wxID_ANY, wxNullBitmap, wxDefaultPosition,
                                                   wxDefaultSize, wxBU_EXACTFIT );

        m_notificationsButton->SetPadding( 0 );
        m_notificationsButton->SetBitmap( KiBitmapBundle( BITMAPS::notifications ) );
        m_notificationsButton->SetShowBadge( true );
        m_notificationsButton->SetBitmapCentered( true );

        m_notificationsButton->Bind( wxEVT_BUTTON, &KISTATUSBAR::onNotificationsIconClick, this );
    }

    if( showWarning )
    {
        m_warningButton = new BITMAP_BUTTON( this, wxID_ANY, wxNullBitmap, wxDefaultPosition,
                                             wxDefaultSize, wxBU_EXACTFIT );

        m_warningButton->SetPadding( 0 );
        m_warningButton->SetBitmap( KiBitmapBundle( BITMAPS::small_warning ) );
        m_warningButton->SetBitmapCentered( true );
        m_warningButton->SetToolTip( _( "View load messages" ) );
        m_warningButton->Hide();

        m_warningButton->Bind( wxEVT_BUTTON, &KISTATUSBAR::onLoadWarningsIconClick, this );
    }

    if( showLabel )
    {
        m_labelButton = new wxButton( this, wxID_ANY, _( "Sign In" ), wxDefaultPosition,
                                      wxDefaultSize, wxBU_EXACTFIT | wxBORDER_NONE );

        m_profileBitmap = new wxStaticBitmap( this, wxID_ANY, wxNullBitmap );
        m_profileBitmap->Hide();
    }

    Bind( wxEVT_SIZE, &KISTATUSBAR::onSize, this );
    m_backgroundProgressBar->Bind( wxEVT_LEFT_DOWN, &KISTATUSBAR::onBackgroundProgressClick, this );

    HideBackgroundProgressBar();
    Layout();
}


KISTATUSBAR::~KISTATUSBAR()
{
    if( m_notificationsButton )
        m_notificationsButton->Unbind( wxEVT_BUTTON, &KISTATUSBAR::onNotificationsIconClick, this );

    if( m_warningButton )
        m_warningButton->Unbind( wxEVT_BUTTON, &KISTATUSBAR::onLoadWarningsIconClick, this );

    Unbind( wxEVT_SIZE, &KISTATUSBAR::onSize, this );
    m_backgroundProgressBar->Unbind( wxEVT_LEFT_DOWN, &KISTATUSBAR::onBackgroundProgressClick,
                                     this );
}


void KISTATUSBAR::onNotificationsIconClick( wxCommandEvent& aEvent )
{
    wxCHECK( m_notificationsButton, /* void */ );
    wxPoint pos = m_notificationsButton->GetScreenPosition();

    wxRect r;
    if( std::optional<int> idx = fieldIndex( FIELD::NOTIFICATION ) )
    {
        GetFieldRect( m_normalFieldsCount + *idx, r );
        pos.x += r.GetWidth();
    }

    Pgm().GetNotificationsManager().ShowList( this, pos );
}


void KISTATUSBAR::onBackgroundProgressClick( wxMouseEvent& aEvent )
{
    wxPoint pos = m_backgroundProgressBar->GetScreenPosition();

    wxRect r;
    if( std::optional<int> idx = fieldIndex( FIELD::BGJOB_GAUGE ) )
    {
        GetFieldRect( m_normalFieldsCount + *idx, r );
        pos.x += r.GetWidth();
    }

    Pgm().GetBackgroundJobMonitor().ShowList( this, pos );
}


void KISTATUSBAR::onSize( wxSizeEvent& aEvent )
{
    layoutControls();
}


void KISTATUSBAR::layoutControls()
{
    constexpr int padding = 5;

    wxRect r;
    GetFieldRect( m_normalFieldsCount + *fieldIndex( FIELD::BGJOB_LABEL ), r );
    int x = r.GetLeft();
    int y = r.GetTop();
    int textHeight = KIUI::GetTextSize( wxT( "bp" ), this ).y;

    if( r.GetHeight() > textHeight )
        y += ( r.GetHeight() - textHeight ) / 2;

    m_backgroundTxt->SetPosition( { x, y } );
    m_backgroundTxt->SetSize( r.GetWidth(), textHeight );
    updateBackgroundText();

    GetFieldRect( m_normalFieldsCount + *fieldIndex( FIELD::BGJOB_GAUGE ), r );
    x = r.GetLeft();
    y = r.GetTop();
    int           w = r.GetWidth();
    int           h = r.GetHeight();
    wxSize buttonSize( 0, 0 );

    if( m_backgroundStopButton )
    {
        buttonSize = m_backgroundStopButton->GetEffectiveMinSize();
        m_backgroundStopButton->SetPosition( { x + w - buttonSize.GetWidth(), y } );
        m_backgroundStopButton->SetSize( buttonSize.GetWidth(), h );
        buttonSize.x += padding;
    }

    m_backgroundProgressBar->SetPosition( { x + padding, y } );
    m_backgroundProgressBar->SetSize( w - buttonSize.GetWidth() - padding, h );

    if( m_notificationsButton )
    {
        GetFieldRect( m_normalFieldsCount + *fieldIndex( FIELD::NOTIFICATION ), r );
        x = r.GetLeft();
        y = r.GetTop();
        h = r.GetHeight();
        buttonSize = m_notificationsButton->GetEffectiveMinSize();
        m_notificationsButton->SetPosition( { x, y } );
        m_notificationsButton->SetSize( buttonSize.GetWidth() + 6, h );
    }

    if( m_warningButton )
    {
        GetFieldRect( m_normalFieldsCount + *fieldIndex( FIELD::WARNING ), r );
        x = r.GetLeft();
        y = r.GetTop();
        h = r.GetHeight();
        buttonSize = m_warningButton->GetEffectiveMinSize();
        m_warningButton->SetPosition( { x, y } );
        m_warningButton->SetSize( buttonSize.GetWidth() + 6, h );
    }

    if( m_labelButton || m_profileBitmap )
    {
        GetFieldRect( m_normalFieldsCount + *fieldIndex( FIELD::LABEL ), r );
        x = r.GetLeft();
        y = r.GetTop();
        w = r.GetWidth();
        h = r.GetHeight();

        if( m_labelButton )
        {
            m_labelButton->SetPosition( { x, y } );
            m_labelButton->SetSize( w, h );
        }

        if( m_profileBitmap )
        {
            wxSize bmpSize = m_profileLogicalSize;
            m_profileBitmap->SetSize( bmpSize );
            int bmpX = x + ( w - bmpSize.GetWidth() ) / 2 + 4;
            int bmpY = y + ( h - bmpSize.GetHeight() ) / 2;
            m_profileBitmap->SetPosition( { bmpX, bmpY } );
        }
    }
}


void KISTATUSBAR::ShowBackgroundProgressBar( bool aCancellable )
{
    m_backgroundProgressBar->Show();

    if( m_backgroundStopButton )
        m_backgroundStopButton->Show( aCancellable );

    m_bgJobActive = true;
    rebuildFieldWidths();
    updateAuxFieldWidths();
}


void KISTATUSBAR::HideBackgroundProgressBar()
{
    m_backgroundProgressBar->Hide();

    if( m_backgroundStopButton )
        m_backgroundStopButton->Hide();

    m_bgJobActive = false;
    rebuildFieldWidths();
    updateAuxFieldWidths();
}


void KISTATUSBAR::SetBackgroundProgress( int aAmount )
{
    int range = m_backgroundProgressBar->GetRange();

    if( aAmount > range )
        aAmount = range;

    m_backgroundProgressBar->SetValue( aAmount );
}


void KISTATUSBAR::SetBackgroundProgressMax( int aAmount )
{
    m_backgroundProgressBar->SetRange( aAmount );
}


void KISTATUSBAR::SetBackgroundStatusText( const wxString& aTxt )
{
    m_backgroundRawText = aTxt;
    updateBackgroundText();

    // When there are multiple normal fields, the last normal field (typically used for
    // file watcher status on Windows) can visually overlap with the background job label
    // since both have variable width. Save and clear that field when showing background
    // text, and restore it when the background text is cleared.
    if( m_normalFieldsCount > 1 )
    {
        int adjacentField = m_normalFieldsCount - 1;

        if( !aTxt.empty() )
        {
            m_savedStatusText = GetStatusText( adjacentField );
            SetStatusText( wxEmptyString, adjacentField );
        }
        else if( !m_savedStatusText.empty() )
        {
            SetStatusText( m_savedStatusText, adjacentField );
            m_savedStatusText.clear();
        }
    }
}


void KISTATUSBAR::updateAuxFieldWidths()
{
    if( m_fieldWidths.empty() )
        return;

    if( std::optional<int> idx = fieldIndex( FIELD::BGJOB_LABEL ) )
        m_fieldWidths[m_normalFieldsCount + *idx] = -2;

    if( std::optional<int> idx = fieldIndex( FIELD::BGJOB_GAUGE ) )
        m_fieldWidths[m_normalFieldsCount + *idx] = 75;

    if( std::optional<int> idx = fieldIndex( FIELD::BGJOB_CANCEL ) )
    {
        m_fieldWidths[m_normalFieldsCount + *idx] =
                m_backgroundStopButton && m_backgroundStopButton->IsShown() ? 20 : 0;
    }

    if( std::optional<int> idx = fieldIndex( FIELD::WARNING ) )
    {
        m_fieldWidths[m_normalFieldsCount + *idx] =
                m_warningButton && m_warningButton->IsShown() ? 20 : 0;
    }

    if( std::optional<int> idx = fieldIndex( FIELD::NOTIFICATION ) )
    {
        m_fieldWidths[m_normalFieldsCount + *idx] =
                m_notificationsButton && m_notificationsButton->IsShown() ? 20 : 0;
    }

    SetStatusWidths( static_cast<int>( m_fieldWidths.size() ), m_fieldWidths.data() );
    layoutControls();
    updateBackgroundText();
}


void KISTATUSBAR::updateBackgroundText()
{
    wxRect r;

    if( !GetFieldRect( m_normalFieldsCount + *fieldIndex( FIELD::BGJOB_LABEL ), r ) )
        return;

    wxString text = m_backgroundRawText;

    if( !text.empty() && r.GetWidth() > 4 )
    {
        wxClientDC dc( this );
        int margin = KIUI::GetTextSize( wxT( "XX" ), this ).x;
        text = wxControl::Ellipsize( text, dc, wxELLIPSIZE_END, std::max( 0, r.GetWidth() - margin ) );
    }

    m_backgroundTxt->SetLabel( text );
}


void KISTATUSBAR::SetNotificationCount( int aCount )
{
    wxCHECK( m_notificationsButton, /* void */ );
    wxString cnt = "";

    if( aCount > 0 )
        cnt = fmt::format( "{}", aCount );

    m_notificationsButton->SetBadgeText( cnt );

    // force a repaint or it wont until it gets activity
    Refresh();
}


void KISTATUSBAR::AddWarningMessages( const wxString& aSource, const wxString& aMessages )
{
    {
        std::lock_guard<std::mutex> lock( m_warningMutex );

        wxStringTokenizer tokenizer( aMessages, wxS( "\n" ), wxTOKEN_STRTOK );

        while( tokenizer.HasMoreTokens() )
        {
            LOAD_MESSAGE msg;
            msg.message = tokenizer.GetNextToken();
            msg.severity = RPT_SEVERITY_WARNING;  // Default to warning for font substitutions
            m_warningMessages[aSource].push_back( msg );
        }
    }

    updateWarningUI();
}


void KISTATUSBAR::AddWarningMessages( const wxString& aSource, const std::vector<LOAD_MESSAGE>& aMessages )
{
    wxLogTrace( traceLibraries, "KISTATUSBAR::AddWarningMessages: this=%p, count=%zu",
                this, aMessages.size() );

    if( aMessages.empty() )
        return;

    size_t totalMessageCount = 0;

    {
        std::lock_guard<std::mutex> lock( m_warningMutex );
        m_warningMessages[aSource].insert( m_warningMessages[aSource].end(), aMessages.begin(), aMessages.end() );

        for( const auto& [source, messages] : m_warningMessages )
            totalMessageCount += messages.size();
    }

    wxLogTrace( traceLibraries, "  -> total messages now=%zu", totalMessageCount );

    // Update UI on main thread
    wxLogTrace( traceLibraries, "  -> calling CallAfter for updateWarningUI" );
    CallAfter( [this]() { updateWarningUI(); } );
}


size_t KISTATUSBAR::GetLoadWarningCount() const
{
    std::lock_guard<std::mutex> lock( m_warningMutex );

    size_t count = 0;

    for( const auto& [source, messages] : m_warningMessages )
        count += messages.size();

    return count;
}


void KISTATUSBAR::updateWarningUI()
{
    wxLogTrace( traceLibraries, "KISTATUSBAR::updateWarningUI: this=%p, m_warningButton=%p",
                this, m_warningButton );

    if( !m_warningButton )
    {
        wxLogTrace( traceLibraries, "  -> no warning button, returning early" );
        return;
    }

    size_t messageCount;
    {
        std::lock_guard<std::mutex> lock( m_warningMutex );

        messageCount = 0;

        for( const std::vector<LOAD_MESSAGE>& messages : m_warningMessages | std::views::values )
            messageCount += messages.size();
    }

    wxLogTrace( traceLibraries, "  -> message count=%zu, showing button=%s",
                messageCount, messageCount > 0 ? "true" : "false" );

    m_warningButton->Show( messageCount > 0 );
    m_warningButton->SetShowBadge( messageCount > 0 );
    updateAuxFieldWidths();

    if( messageCount > 0 )
    {
        m_warningButton->SetToolTip( wxString::Format( _( "View %zu message(s)" ), messageCount ) );

        // Show count badge on the warning button
        wxString badgeText = messageCount > 99
                ? wxString( "99+" )
                : wxString::Format( wxS( "%zu" ), messageCount );
        m_warningButton->SetBadgeText( badgeText );

        wxLogTrace( traceLibraries, "  -> badge set to '%s'", badgeText );
    }
    else
    {
        m_warningButton->SetBadgeText( wxEmptyString );
        m_warningButton->SetToolTip( _( "View messages" ) );
    }

    Layout();
    Refresh();
}


void KISTATUSBAR::ClearWarningMessages( const wxString& aSource )
{
    {
        std::lock_guard<std::mutex> lock( m_warningMutex );

        if( aSource.IsEmpty() )
            m_warningMessages.clear();
        else if( auto it = m_warningMessages.find( aSource ); it != m_warningMessages.end() )
                m_warningMessages.erase( it );
    }

    updateWarningUI();
}


void KISTATUSBAR::onLoadWarningsIconClick( wxCommandEvent& aEvent )
{
    // Copy messages under lock to avoid holding lock during modal dialog
    std::unordered_map<wxString, std::vector<LOAD_MESSAGE>> messages;
    {
        std::lock_guard<std::mutex> lock( m_warningMutex );
        messages = m_warningMessages;
    }

    if( messages.empty() )
        return;

    STATUSBAR_WARNING_REPORTER_DIALOG dlg( GetParent(), this );

    for( const std::vector<LOAD_MESSAGE>& source : std::views::values( messages ) )
        for( const LOAD_MESSAGE& msg : source )
            dlg.m_Reporter->Report( msg.message, msg.severity );

    dlg.m_Reporter->Flush();
    dlg.ShowModal();
}

void KISTATUSBAR::SetFieldWeight( int aFieldId, int aWeight )
{
    const int count = GetFieldsCount();
    wxCHECK_RET( aFieldId >= 0 && aFieldId < count, wxS( "Field id out of range" ) );
    wxCHECK_RET( aWeight > 0, wxS( "Weight must be positive" ) );

    m_primaryFieldId = aFieldId;
    m_primaryFieldWeight = aWeight;
    rebuildFieldWidths();
}


void KISTATUSBAR::rebuildFieldWidths()
{
    const int count = GetFieldsCount();

    // Derive aNumberFields (user fields) from total minus extras.
    int extraCount = 0;
#ifdef __WXOSX__
    extraCount += 3;
#else
    extraCount += 2;
#endif

    if( m_styleFlags & CANCEL_BUTTON )
        extraCount++;
    if( m_styleFlags & WARNING_ICON )
        extraCount++;
    if( m_styleFlags & NOTIFICATION_ICON )
        extraCount++;
    if( m_styleFlags & LABEL_BUTTON )
        extraCount++;

    const int userFields = count - extraCount;
    wxCHECK_RET( userFields >= 0, wxS( "Corrupt field count" ) );

    std::vector<int> widths( count, -1 );

    for( int i = 0; i < userFields; ++i )
        widths[i] = -1;

    auto setExtra = [&]( FIELD aField, int aWidth )
    {
        std::optional<int> idx = fieldIndex( aField );

        if( idx && *idx >= 0 )
            widths[userFields + *idx] = aWidth;
    };

    // Background-job fields collapse to zero when idle so they stop
    // hogging ~95 px of width during normal use. They re-expand when a
    // job becomes active (Show/HideBackgroundProgressBar toggle this).
    if( m_bgJobActive )
    {
        setExtra( FIELD::BGJOB_LABEL, -1 );
        setExtra( FIELD::BGJOB_GAUGE, 75 );
        setExtra( FIELD::BGJOB_CANCEL, 20 );
    }
    else
    {
        setExtra( FIELD::BGJOB_LABEL, 0 );
        setExtra( FIELD::BGJOB_GAUGE, 0 );
        setExtra( FIELD::BGJOB_CANCEL, 0 );
    }

    setExtra( FIELD::WARNING, 20 );
    setExtra( FIELD::NOTIFICATION, 20 );
    setExtra( FIELD::LABEL, 35 );

#ifdef __WXOSX__
    widths[count - 1] = 10;
#endif

    if( m_primaryFieldId >= 0 && m_primaryFieldId < count )
        widths[m_primaryFieldId] = -m_primaryFieldWeight;

    SetStatusWidths( count, widths.data() );
}


void KISTATUSBAR::SetEllipsedTextField( const wxString& aText, int aFieldId )
{
    wxRect       fieldRect;
    int          width = -1;
    wxString     etext = aText;

    // Only GetFieldRect() returns the current size for variable size fields
    // Other methods return -1 for the width of these fields.
    if( GetFieldRect( aFieldId, fieldRect ) )
        width = fieldRect.GetWidth();

    if( width > 20 )
    {
        wxClientDC dc( this );

        // Gives a margin to the text to be sure it is not clamped at its end
        int margin = KIUI::GetTextSize( wxT( "XX" ), this ).x;
        etext = wxControl::Ellipsize( etext, dc, wxELLIPSIZE_MIDDLE, width - margin );
    }

    SetStatusText( etext, aFieldId );
}


std::optional<int> KISTATUSBAR::fieldIndex( FIELD aField ) const
{
    switch( aField )
    {
    case FIELD::BGJOB_LABEL:  return 0;
    case FIELD::BGJOB_GAUGE:  return 1;
    case FIELD::BGJOB_CANCEL:
    {
        if( m_styleFlags & CANCEL_BUTTON )
            return 2;

        break;
    }
    case FIELD::WARNING:
    {
        if( m_styleFlags & WARNING_ICON )
        {
            int offset = 2;

            if( m_styleFlags & CANCEL_BUTTON )
                offset++;

            return offset;
        }

        break;
    }
    case FIELD::NOTIFICATION:
    {
        if( m_styleFlags & NOTIFICATION_ICON )
        {
            int offset = 2;

            if( m_styleFlags & CANCEL_BUTTON )
                offset++;

            if( m_styleFlags & WARNING_ICON )
                offset++;

            return offset;
        }

        break;
    }
    case FIELD::LABEL:
    {
        if( m_styleFlags & LABEL_BUTTON )
        {
            int offset = 2;

            if( m_styleFlags & CANCEL_BUTTON )
                offset++;

            if( m_styleFlags & WARNING_ICON )
                offset++;

            if( m_styleFlags & NOTIFICATION_ICON )
                offset++;

            return offset;
        }

        break;
    }
    }

    return std::nullopt;
}


void KISTATUSBAR::updateLabelFieldWidth( int aNewWidth )
{
    auto idx = fieldIndex( FIELD::LABEL );
    if( !idx )
        return;

    // Reconstruct the full widths array — must match the layout from the constructor.
    int extraFields = 2;  // BGJOB_LABEL + BGJOB_GAUGE always present
#ifdef __WXOSX__
    extraFields++;  // rounded corner spacer
#endif
    if( m_styleFlags & CANCEL_BUTTON )     extraFields++;
    if( m_styleFlags & WARNING_ICON )      extraFields++;
    if( m_styleFlags & NOTIFICATION_ICON ) extraFields++;
    if( m_styleFlags & LABEL_BUTTON )      extraFields++;

    int totalFields = m_normalFieldsCount + extraFields;
    int* widths = new int[totalFields];

    for( int i = 0; i < m_normalFieldsCount; i++ )
        widths[i] = -1;

    if( auto i = fieldIndex( FIELD::BGJOB_LABEL ) )  widths[m_normalFieldsCount + *i] = -1;
    if( auto i = fieldIndex( FIELD::BGJOB_GAUGE ) )  widths[m_normalFieldsCount + *i] = 75;
    if( auto i = fieldIndex( FIELD::BGJOB_CANCEL ) ) widths[m_normalFieldsCount + *i] = 20;
    if( auto i = fieldIndex( FIELD::WARNING ) )      widths[m_normalFieldsCount + *i] = 20;
    if( auto i = fieldIndex( FIELD::NOTIFICATION ) ) widths[m_normalFieldsCount + *i] = 20;
    widths[m_normalFieldsCount + *idx] = aNewWidth;

#ifdef __WXOSX__
    widths[totalFields - 1] = 10;  // rounded corner offset
#endif

    SetStatusWidths( totalFields, widths );
    delete[] widths;

    // Trigger onSize to reposition all widgets
    wxSizeEvent evt( GetSize(), GetId() );
    evt.SetEventObject( this );
    GetEventHandler()->ProcessEvent( evt );
}


void KISTATUSBAR::SetProfileBitmap( const wxBitmapBundle& aBundle )
{
    if( !m_profileBitmap )
        return;

    m_profileBitmap->SetBitmap( aBundle );

    wxSize defaultSize = aBundle.GetDefaultSize();
    if( defaultSize.IsFullySpecified() )
    {
        m_profileLogicalSize = defaultSize;
        updateLabelFieldWidth( defaultSize.GetWidth() + 17 );
    }
}


void KISTATUSBAR::SetLabelButtonText( const wxString& aText )
{
    if( !m_labelButton )
        return;

    m_labelButton->SetLabel( aText );

    wxClientDC dc( m_labelButton );
    dc.SetFont( m_labelButton->GetFont() );
    int newWidth = dc.GetTextExtent( aText ).x + 30;
    updateLabelFieldWidth( newWidth );
}
