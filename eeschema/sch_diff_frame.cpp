/*
 * SCH_DIFF_FRAME - Read-only schematic diff viewer for agent before/after changes.
 *
 * Shows two snapshots of a schematic sheet (before = disk file, after = in-memory agent changes)
 * and lets the user toggle between them using Before / After links in the infobar.
 * Changed items are highlighted via per-item colored bounding box overlays drawn on
 * LAYER_GP_OVERLAY:
 *   green  = added (exists in after, not in before)
 *   red    = deleted (exists in before, not in after)
 *   amber  = modified (exists in both but bbox or reference/value changed)
 */

#include "sch_diff_frame.h"
#include "sch_diff_highlight.h"
#include <diff_manager.h>

#include <gal/color4d.h>
#include <gal/graphics_abstraction_layer.h>
#include <kiid.h>
#include <kiway.h>
#include <kiway_mail.h>
#include <layer_ids.h>
#include <mail_type.h>
#include <nlohmann/json.hpp>
#include <schematic.h>
#include <sch_edit_frame.h>
#include <sch_io/sch_io_mgr.h>
#include <sch_io/sch_io.h>
#include <sch_line.h>
#include <sch_screen.h>
#include <sch_symbol.h>
#include <view/view.h>
#include <view/view_item.h>
#include <wx/hyperlink.h>
#include <wx/log.h>
#include <widgets/wx_infobar.h>
#include <unordered_map>
#include <unordered_set>

using json = nlohmann::json;


// ---------------------------------------------------------------------------
// SCH_DIFF_HIGHLIGHT_ITEM
//
// A lightweight VIEW_ITEM that draws per-item colored bounding boxes as a
// canvas overlay.  One instance is created per ShowBefore() / ShowAfter()
// call and added to LAYER_GP_OVERLAY so it renders on top of the schematic.
// ---------------------------------------------------------------------------
class SCH_DIFF_HIGHLIGHT_ITEM : public KIGFX::VIEW_ITEM
{
public:
    struct DiffBox
    {
        BOX2I          bbox;
        KIGFX::COLOR4D color;
        bool           hasBorder = true;
    };

    SCH_DIFF_HIGHLIGHT_ITEM() = default;

    void AddBox( const BOX2I& aBBox, const KIGFX::COLOR4D& aColor, bool aHasBorder = true )
    {
        m_boxes.push_back( { aBBox, aColor, aHasBorder } );
    }

    bool IsEmpty() const { return m_boxes.empty(); }

    // VIEW_ITEM interface
    wxString GetClass() const override { return wxT( "SCH_DIFF_HIGHLIGHT_ITEM" ); }

    const BOX2I ViewBBox() const override
    {
        // Return maximum box so the overlay is never culled by the view frustum.
        BOX2I box;
        box.SetMaximum();
        return box;
    }

    std::vector<int> ViewGetLayers() const override
    {
        return { LAYER_GP_OVERLAY };
    }

    void ViewDraw( int /*aLayer*/, KIGFX::VIEW* aView ) const override
    {
        KIGFX::GAL* gal = aView->GetGAL();
        if( !gal || m_boxes.empty() )
            return;

        gal->Save();

        // Three-pass rendering so borders are never obscured:
        //   1. Bordered item fills (components, labels)
        //   2. Borderless item fills (wires, junctions)
        //   3. Borders — always on top

        // Pass 1: bordered item fills
        gal->SetIsStroke( false );
        gal->SetIsFill( true );

        for( const DiffBox& b : m_boxes )
        {
            if( !b.hasBorder )
                continue;
            gal->SetFillColor( b.color.WithAlpha( DIFF_FILL_ALPHA ) );
            gal->DrawRectangle( VECTOR2D( b.bbox.GetLeft(),  b.bbox.GetTop() ),
                                VECTOR2D( b.bbox.GetRight(), b.bbox.GetBottom() ) );
        }

        // Pass 2: borderless item fills
        for( const DiffBox& b : m_boxes )
        {
            if( b.hasBorder )
                continue;
            gal->SetFillColor( b.color.WithAlpha( DIFF_FILL_ALPHA ) );
            gal->DrawRectangle( VECTOR2D( b.bbox.GetLeft(),  b.bbox.GetTop() ),
                                VECTOR2D( b.bbox.GetRight(), b.bbox.GetBottom() ) );
        }

        // Pass 3: borders (screen-space width)
        // 10 mils in schematic IU: 10 * 254 = 2540
        double borderWidth = 2540;

        gal->SetIsFill( false );
        gal->SetIsStroke( true );
        gal->SetLineWidth( borderWidth );

        for( const DiffBox& b : m_boxes )
        {
            if( !b.hasBorder )
                continue;
            gal->SetStrokeColor( b.color.WithAlpha( DIFF_BORDER_ALPHA ) );
            gal->DrawRectangle( VECTOR2D( b.bbox.GetLeft(),  b.bbox.GetTop() ),
                                VECTOR2D( b.bbox.GetRight(), b.bbox.GetBottom() ) );
        }

        gal->Restore();
    }

private:
    std::vector<DiffBox> m_boxes;
};


// ---------------------------------------------------------------------------
// SCH_DIFF_FRAME
// ---------------------------------------------------------------------------

BEGIN_EVENT_TABLE( SCH_DIFF_FRAME, SCH_EDIT_FRAME )
    EVT_CLOSE( SCH_DIFF_FRAME::onClose )
END_EVENT_TABLE()


SCH_DIFF_FRAME::SCH_DIFF_FRAME( KIWAY* aKiway, wxWindow* aParent )
    : SCH_EDIT_FRAME( aKiway, aParent, FRAME_SCH_DIFF )
{
    SetTitle( _( "Schematic Diff Viewer" ) );

    // This frame is read-only — disable auto-save and modify-on-close prompts.
    m_supportsAutoSave = false;

    wxLogDebug( "SCH_DIFF_FRAME: Created" );
}


SCH_DIFF_FRAME::~SCH_DIFF_FRAME()
{
    // Remove the highlight overlay from the view before tearing down.
    if( m_highlightItem && GetCanvas() && GetCanvas()->GetView() )
        GetCanvas()->GetView()->Remove( m_highlightItem );
    delete m_highlightItem;
    m_highlightItem = nullptr;

    // If showing "before" state, clear the view so canvas items no longer
    // reference m_schemBefore's screens before we delete it.
    if( m_showingBefore && GetCanvas() )
    {
        GetCanvas()->GetView()->Clear();
        m_showingBefore = false;
    }

    // m_schemBefore is owned by us; the "after" schematic is owned by the base class
    // (transferred via SetSchematic()) and will be freed there.
    delete m_schemBefore;
    m_schemBefore = nullptr;
    wxLogDebug( "SCH_DIFF_FRAME: Destroyed" );
}


bool SCH_DIFF_FRAME::OpenProjectFiles( const std::vector<wxString>& /*aFileSet*/, int /*aCtl*/ )
{
    // Content-driven via SetDiffContent() / KiwayMailIn(); ignore direct file opens.
    return false;
}


void SCH_DIFF_FRAME::KiwayMailIn( KIWAY_MAIL_EVENT& aEvent )
{
    if( aEvent.GetId() == MAIL_SCH_DIFF_CONTENT )
    {
        wxLogDebug( "SCH_DIFF_FRAME: received MAIL_SCH_DIFF_CONTENT" );
        try
        {
            json j = json::parse( aEvent.GetPayload() );
            wxString beforePath = wxString::FromUTF8( j.value( "before_path", "" ) );
            wxString afterPath  = wxString::FromUTF8( j.value( "after_path",  "" ) );
            wxString sheetPath  = wxString::FromUTF8( j.value( "sheet_path",  "" ) );

            // changed_uuids from the frontend is ignored — we recompute from the actual
            // before/after schematics for accuracy.

            SetDiffContent( beforePath, afterPath, sheetPath );
        }
        catch( ... )
        {
            wxLogWarning( "SCH_DIFF_FRAME: failed to parse MAIL_SCH_DIFF_CONTENT payload" );
        }
        return;
    }
    // Intentionally do NOT forward agent change mails to the base class — this frame is read-only.
}


void SCH_DIFF_FRAME::SetDiffContent( const wxString& aBeforePath,
                                      const wxString& aAfterPath,
                                      const wxString& aSheetPath )
{
    wxLogDebug( "SCH_DIFF_FRAME::SetDiffContent: before='%s' after='%s' sheet='%s'",
                aBeforePath, aAfterPath, aSheetPath );

    m_beforePath = aBeforePath;
    m_afterPath  = aAfterPath;

    // --- Load "before" state (owned by this frame) ---
    delete m_schemBefore;
    m_schemBefore = nullptr;

    if( !aBeforePath.IsEmpty() && wxFileName::FileExists( aBeforePath ) )
    {
        SCHEMATIC* schemBefore = new SCHEMATIC( nullptr );
        if( loadSchematicFromFile( aBeforePath, schemBefore ) )
            m_schemBefore = schemBefore;
        else
            delete schemBefore;
    }

    // --- Load "after" state and transfer ownership to the base SCH_EDIT_FRAME ---
    if( !aAfterPath.IsEmpty() && wxFileName::FileExists( aAfterPath ) )
    {
        SCHEMATIC* schemAfter = new SCHEMATIC( nullptr );
        if( loadSchematicFromFile( aAfterPath, schemAfter ) )
        {
            if( GetCanvas() )
                GetCanvas()->GetView()->Clear();

            SetSchematic( schemAfter );
        }
        else
        {
            delete schemAfter;
        }
    }

    // Classify changed items into added / deleted / modified UUID sets.
    recomputeChangedUuids();

    // Default to showing "after".  buildAndAddHighlight() is called inside ShowAfter().
    ShowAfter();

    wxString sheetLabel = aSheetPath.IsEmpty() ? _( "Root Sheet" ) : aSheetPath;
    SetTitle( wxString::Format( _( "Schematic Diff Viewer \u2014 %s" ), sheetLabel ) );
    setupInfoBar( sheetLabel );

    Show( true );
    Raise();
}


void SCH_DIFF_FRAME::setupInfoBar( const wxString& aSheetPath )
{
    WX_INFOBAR* infoBar = GetInfoBar();
    if( !infoBar )
        return;

    infoBar->RemoveAllButtons();

    auto* beforeLink = new wxHyperlinkCtrl( infoBar, wxID_ANY, _( "Before" ), wxEmptyString );
    auto* afterLink  = new wxHyperlinkCtrl( infoBar, wxID_ANY, _( "After" ),  wxEmptyString );

    beforeLink->SetVisitedColour( beforeLink->GetNormalColour() );
    beforeLink->SetHoverColour( beforeLink->GetNormalColour() );
    afterLink->SetVisitedColour( afterLink->GetNormalColour() );
    afterLink->SetHoverColour( afterLink->GetNormalColour() );

    beforeLink->Bind( wxEVT_HYPERLINK,
                      [this]( wxHyperlinkEvent& aEvt ) { aEvt.Skip( false ); ShowBefore(); } );
    afterLink->Bind(  wxEVT_HYPERLINK,
                      [this]( wxHyperlinkEvent& aEvt ) { aEvt.Skip( false ); ShowAfter(); } );

    infoBar->AddButton( beforeLink );
    infoBar->AddButton( afterLink );

    infoBar->ShowMessage( wxString::Format( _( "Read-only diff view \u2014 %s" ), aSheetPath ),
                          wxICON_INFORMATION );
}


bool SCH_DIFF_FRAME::loadSchematicFromFile( const wxString& aFilePath, SCHEMATIC* aSchematic )
{
    if( aFilePath.IsEmpty() || !wxFileName::FileExists( aFilePath ) )
    {
        wxLogWarning( "SCH_DIFF_FRAME::loadSchematicFromFile: file not found: %s", aFilePath );
        return false;
    }

    wxLogDebug( "SCH_DIFF_FRAME::loadSchematicFromFile: loading '%s'", aFilePath );

    try
    {
        aSchematic->SetProject( &Prj() );
        aSchematic->CreateDefaultScreens();

        IO_RELEASER<SCH_IO> pi( SCH_IO_MGR::FindPlugin( SCH_IO_MGR::SCH_KICAD ) );
        SCH_SHEET* rootSheet = pi->LoadSchematicFile( aFilePath, aSchematic );

        if( !rootSheet )
        {
            wxLogWarning( "SCH_DIFF_FRAME: LoadSchematicFile returned null for '%s'", aFilePath );
            return false;
        }

        aSchematic->SetTopLevelSheets( { rootSheet } );

        SCH_SCREENS screens( aSchematic->Root() );
        for( SCH_SCREEN* screen = screens.GetFirst(); screen; screen = screens.GetNext() )
            screen->UpdateLocalLibSymbolLinks();

        SCH_SHEET_LIST sheetList = aSchematic->BuildSheetListSortedByPageNumbers();
        if( !sheetList.empty() )
            aSchematic->SetCurrentSheet( sheetList[0] );

        wxLogDebug( "SCH_DIFF_FRAME: loaded '%s' successfully (%zu sheets)",
                    aFilePath, sheetList.size() );
        return true;
    }
    catch( const IO_ERROR& ioe )
    {
        wxLogWarning( "SCH_DIFF_FRAME: IO_ERROR loading '%s': %s", aFilePath, ioe.What() );
        return false;
    }
    catch( ... )
    {
        wxLogWarning( "SCH_DIFF_FRAME: unknown exception loading '%s'", aFilePath );
        return false;
    }
}


void SCH_DIFF_FRAME::recomputeChangedUuids()
{
    m_addedUuids.clear();
    m_deletedUuids.clear();
    m_modifiedUuids.clear();

    SCH_SCREEN* afterScreen = nullptr;
    {
        SCH_SHEET_LIST afterSheets = Schematic().BuildSheetListSortedByPageNumbers();
        if( !afterSheets.empty() )
            afterScreen = afterSheets[0].LastScreen();
    }

    if( !afterScreen )
    {
        wxLogDebug( "SCH_DIFF_FRAME::recomputeChangedUuids: no after screen" );
        return;
    }

    if( !m_schemBefore )
    {
        for( SCH_ITEM* item : afterScreen->Items() )
            m_addedUuids.insert( item->m_Uuid );
        wxLogDebug( "SCH_DIFF_FRAME::recomputeChangedUuids: no before state, "
                    "%zu item(s) marked as added", m_addedUuids.size() );
        return;
    }

    SCH_SCREEN* beforeScreen = nullptr;
    {
        SCH_SHEET_LIST beforeSheets = m_schemBefore->BuildSheetListSortedByPageNumbers();
        if( !beforeSheets.empty() )
            beforeScreen = beforeSheets[0].LastScreen();
    }

    if( !beforeScreen )
    {
        wxLogDebug( "SCH_DIFF_FRAME::recomputeChangedUuids: no before screen" );
        return;
    }

    std::unordered_map<KIID, SCH_ITEM*> beforeMap;
    for( SCH_ITEM* item : beforeScreen->Items() )
        beforeMap[item->m_Uuid] = item;

    std::unordered_set<KIID> afterUuids;
    for( SCH_ITEM* item : afterScreen->Items() )
        afterUuids.insert( item->m_Uuid );

    // Items in after: classify as added or modified.
    for( SCH_ITEM* afterItem : afterScreen->Items() )
    {
        auto it = beforeMap.find( afterItem->m_Uuid );
        if( it == beforeMap.end() )
        {
            m_addedUuids.insert( afterItem->m_Uuid );
        }
        else
        {
            if( it->second->GetBoundingBox() != afterItem->GetBoundingBox() )
            {
                m_modifiedUuids.insert( afterItem->m_Uuid );
            }
            else if( afterItem->Type() == SCH_SYMBOL_T )
            {
                SCH_SYMBOL* symBefore = static_cast<SCH_SYMBOL*>( it->second );
                SCH_SYMBOL* symAfter  = static_cast<SCH_SYMBOL*>( afterItem );

                SCH_FIELD* refB = symBefore->GetField( FIELD_T::REFERENCE );
                SCH_FIELD* refA = symAfter->GetField( FIELD_T::REFERENCE );
                SCH_FIELD* valB = symBefore->GetField( FIELD_T::VALUE );
                SCH_FIELD* valA = symAfter->GetField( FIELD_T::VALUE );

                bool refChanged = refB && refA && refB->GetText() != refA->GetText();
                bool valChanged = valB && valA && valB->GetText() != valA->GetText();

                if( refChanged || valChanged )
                    m_modifiedUuids.insert( afterItem->m_Uuid );
            }
        }
    }

    // Items in before that no longer exist in after = deleted.
    for( SCH_ITEM* beforeItem : beforeScreen->Items() )
    {
        if( !afterUuids.count( beforeItem->m_Uuid ) )
            m_deletedUuids.insert( beforeItem->m_Uuid );
    }

    wxLogDebug( "SCH_DIFF_FRAME::recomputeChangedUuids: added=%zu deleted=%zu modified=%zu",
                m_addedUuids.size(), m_deletedUuids.size(), m_modifiedUuids.size() );
}


void SCH_DIFF_FRAME::buildAndAddHighlight( SCH_SCREEN* aScreen, bool aIsBefore )
{
    KIGFX::VIEW* view = GetCanvas() ? GetCanvas()->GetView() : nullptr;
    if( !view )
        return;

    // The previous highlight item was removed from the view by DisplaySheet() already,
    // but delete the object to avoid a leak.
    delete m_highlightItem;
    m_highlightItem = nullptr;

    if( !aScreen )
        return;

    const std::set<KIID>& primarySet   = aIsBefore ? m_deletedUuids  : m_addedUuids;
    const KIGFX::COLOR4D& primaryColor = aIsBefore ? SCH_DIFF::COLOR_DELETED
                                                     : SCH_DIFF::COLOR_ADDED;

    auto* highlight = new SCH_DIFF_HIGHLIGHT_ITEM();

    // Build highlight boxes using shared utility
    for( const auto& db : SCH_DIFF::BuildHighlightBoxes( aScreen, primarySet, primaryColor ) )
        highlight->AddBox( db.bbox, db.color, db.hasBorder );

    for( const auto& db : SCH_DIFF::BuildHighlightBoxes( aScreen, m_modifiedUuids,
                                                           SCH_DIFF::COLOR_MODIFIED ) )
        highlight->AddBox( db.bbox, db.color, db.hasBorder );

    m_highlightItem = highlight;
    view->Add( m_highlightItem );
    view->SetVisible( m_highlightItem, true );
    view->Update( m_highlightItem );
    view->MarkDirty();

    wxLogDebug( "SCH_DIFF_FRAME::buildAndAddHighlight: %s, boxes=%zu",
                aIsBefore ? "before" : "after",
                static_cast<SCH_DIFF_HIGHLIGHT_ITEM*>( m_highlightItem )->IsEmpty() ? 0 : 1 );
}


void SCH_DIFF_FRAME::ShowBefore()
{
    wxLogDebug( "SCH_DIFF_FRAME::ShowBefore: m_schemBefore=%p", m_schemBefore );
    m_showingBefore = true;

    if( !m_schemBefore )
    {
        wxLogDebug( "SCH_DIFF_FRAME::ShowBefore: no before state available" );
        return;
    }

    SCH_SCREEN* screen = m_schemBefore->CurrentSheet().LastScreen();
    if( screen && GetCanvas() )
    {
        GetCanvas()->DisplaySheet( screen );
        buildAndAddHighlight( screen, true /* aIsBefore */ );
        GetCanvas()->ForceRefresh();
    }
}


void SCH_DIFF_FRAME::ShowAfter()
{
    wxLogDebug( "SCH_DIFF_FRAME::ShowAfter" );
    m_showingBefore = false;

    SCH_SCREEN* screen = Schematic().CurrentSheet().LastScreen();
    if( screen && GetCanvas() )
    {
        GetCanvas()->DisplaySheet( screen );
        buildAndAddHighlight( screen, false /* aIsBefore */ );
        GetCanvas()->ForceRefresh();
    }
}


void SCH_DIFF_FRAME::onClose( wxCloseEvent& aEvent )
{
    aEvent.Skip();
}
