#ifndef SCH_DIFF_FRAME_H
#define SCH_DIFF_FRAME_H

/**
 * SCH_DIFF_FRAME - a read-only schematic viewer for before/after diff of agent changes.
 *
 * Launched via FRAME_SCH_DIFF when the user clicks "View in Editor" in the agent panel.
 * Holds two schematic states in memory (before = disk file, after = serialized in-memory)
 * and lets the user toggle between them with Before/After links in the infobar.
 * Changed items are highlighted via per-item colored bounding box overlays:
 *   green  = added, red = deleted, amber = modified.
 */

#include <sch_edit_frame.h>
#include <kiid.h>
#include <set>

class KIWAY;
class SCH_SCREEN;
class SCHEMATIC;

namespace KIGFX { class VIEW_ITEM; }


class SCH_DIFF_FRAME : public SCH_EDIT_FRAME
{
public:
    SCH_DIFF_FRAME( KIWAY* aKiway, wxWindow* aParent );
    ~SCH_DIFF_FRAME() override;

    /**
     * Set the before (disk file) and after (agent-modified) schematic file paths.
     * Loads both states into memory, displays "after" by default, and shows the frame.
     *
     * @param aBeforePath  Path to the pre-change .kicad_sch file (disk file)
     * @param aAfterPath   Path to temp .kicad_sch file with serialized after-state
     * @param aSheetPath   Human-readable sheet path shown in the title bar and infobar
     */
    void SetDiffContent( const wxString& aBeforePath,
                         const wxString& aAfterPath,
                         const wxString& aSheetPath );

    /// Switch the canvas to show the "before" state
    void ShowBefore();

    /// Switch the canvas to show the "after" state
    void ShowAfter();

    // KIWAY_PLAYER overrides
    bool OpenProjectFiles( const std::vector<wxString>& aFileSet, int aCtl = 0 ) override;
    void KiwayMailIn( KIWAY_MAIL_EVENT& aEvent ) override;

    /// Read-only frame — block all save operations.
    bool SaveProject( bool aSaveAs = false ) override { return false; }

    /// Read-only frame — never has unsaved changes.
    bool IsContentModified() const override { return false; }

    /// Read-only frame — discard any modification notifications silently.
    void OnModify() override {}

private:
    /// Set up the infobar once with a static read-only message and Before/After links.
    void setupInfoBar( const wxString& aSheetPath );

    bool loadSchematicFromFile( const wxString& aFilePath, SCHEMATIC* aSchematic );
    void onClose( wxCloseEvent& aEvent );

    /**
     * Compare before and after screens and populate the three UUID sets with UUIDs of
     * items that were added, removed, or modified.
     */
    void recomputeChangedUuids();

    /**
     * Build a per-item colored bounding box overlay for the given screen and add it to
     * the canvas view.  Call after DisplaySheet() since that clears the view.
     *
     * @param aScreen    The screen whose items should be highlighted.
     * @param aIsBefore  True → highlight deleted+modified (before view);
     *                   False → highlight added+modified (after view).
     */
    void buildAndAddHighlight( SCH_SCREEN* aScreen, bool aIsBefore );

    // --- state ---
    wxString          m_beforePath;                 ///< Disk-file path for "before"
    wxString          m_afterPath;                  ///< Temp-file path for "after"
    std::set<KIID>    m_addedUuids;                 ///< Items present in after but not before
    std::set<KIID>    m_deletedUuids;               ///< Items present in before but not after
    std::set<KIID>    m_modifiedUuids;              ///< Items present in both but changed

    SCHEMATIC*          m_schemBefore   = nullptr;  ///< Schematic loaded from before file (owned)
    bool                m_showingBefore = false;    ///< True if currently displaying before state
    KIGFX::VIEW_ITEM*   m_highlightItem = nullptr;  ///< Per-item diff highlight overlay (owned)

    DECLARE_EVENT_TABLE()
};

#endif // SCH_DIFF_FRAME_H
