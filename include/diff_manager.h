#ifndef DIFF_MANAGER_H
#define DIFF_MANAGER_H

#include <memory>
#include <mutex>
#include <functional>
#include <unordered_map>
#include <vector>
#include <wx/string.h>
#include <math/box2.h>
#include <math/vector2d.h>
#include <gal/color4d.h>
#include <kiid.h>

// Forward declarations
class AGENT_CHANGE_TRACKER;

namespace KIGFX
{
class VIEW;
}
namespace KIGFX
{
namespace PREVIEW
{
    class DIFF_OVERLAY_ITEM;
}
} // namespace KIGFX

/**
 * DIFF_MANAGER handles the "Pending Change" state and controls the display
 * of the Diff Overlay.
 */
struct DIFF_CALLBACKS
{
    std::function<void()> onViewDiff;  ///< Called when "View Diff" is clicked
    std::function<void()> onApprove;   ///< Called when "Approve" is clicked
    std::function<void()> onReject;    ///< Called when "Reject" is clicked
    std::function<void()> onRefresh;   ///< Called to trigger canvas refresh after view changes

    /// Per-item callbacks (for hover-based approve/reject on individual items)
    std::function<void( const std::vector<KIID>& )> onApproveItems;
    std::function<void( const std::vector<KIID>& )> onRejectItems;
};

/**
 * Callback function type for computing dynamic bounding boxes.
 * This allows the DIFF_MANAGER to request updated bounding boxes from the
 * frame classes that have access to SCHEMATIC or BOARD objects.
 */
using BBOX_COMPUTE_CALLBACK = std::function<BOX2I()>;

// Diff highlight style constants (shared across live overlay and diff viewer)
constexpr double DIFF_FILL_ALPHA   = 0.20;
constexpr double DIFF_BORDER_ALPHA = 0.65;

/**
 * Per-item highlight data for live diff overlays.
 */
struct DIFF_ITEM_HIGHLIGHT
{
    BOX2I              bbox;
    KIGFX::COLOR4D     color;
    bool               hasBorder = true;   ///< false for wires (fill only)
    std::vector<KIID>  itemIds;            ///< KIIDs covered by this highlight
};

using ITEM_HIGHLIGHTS_CALLBACK = std::function<std::vector<DIFF_ITEM_HIGHLIGHT>()>;

/**
 * Per-view diff overlay state
 */
struct DIFF_VIEW_STATE
{
    bool                               active = false;
    BOX2I                              currentBBox;
    KIGFX::PREVIEW::DIFF_OVERLAY_ITEM* item = nullptr;
    DIFF_CALLBACKS                     callbacks;
    AGENT_CHANGE_TRACKER*              tracker = nullptr;     ///< Item-based tracker (optional)
    wxString                           sheetPath;             ///< Sheet path for multi-sheet (optional)
    BBOX_COMPUTE_CALLBACK              bboxCallback;          ///< Dynamic bbox computation callback
};

class DIFF_MANAGER
{
public:
    static DIFF_MANAGER& GetInstance();

    // Show diff on the currently active view (set via RegisterOverlay).
    void ShowDiff( const BOX2I& aBBox );

    // Register a view to draw the overlay on, and callbacks for interaction
    // This sets the "current" view for subsequent ShowDiff/ClearDiff calls
    void RegisterOverlay( KIGFX::VIEW* aView, DIFF_CALLBACKS aCallbacks );

    /**
     * Register an overlay with an AGENT_CHANGE_TRACKER for item-based tracking.
     * The bounding box will be computed dynamically using the provided callback.
     * @param aView The view to register.
     * @param aTracker The change tracker (caller retains ownership).
     * @param aSheetPath The sheet path for multi-sheet support (empty for PCB).
     * @param aCallbacks User interaction callbacks.
     * @param aBBoxCallback Callback to compute dynamic bounding box.
     */
    void RegisterOverlay( KIGFX::VIEW* aView, AGENT_CHANGE_TRACKER* aTracker,
                          const wxString& aSheetPath, DIFF_CALLBACKS aCallbacks,
                          BBOX_COMPUTE_CALLBACK aBBoxCallback,
                          ITEM_HIGHLIGHTS_CALLBACK aHighlightsCallback = nullptr );

    /**
     * Null out the overlay item pointer without removing it from the view.
     * Call this before View::Clear() to prevent dangling pointer usage
     * when the view clears all items (e.g., during sheet switching).
     */
    void NullifyOverlayItem( KIGFX::VIEW* aView );

    // Unregister the current view
    void UnregisterOverlay();

    // Unregister a specific view (called when view is destroyed)
    void UnregisterOverlay( KIGFX::VIEW* aView );

    // Clear diff on the current view
    void ClearDiff();

    // Clear diff on a specific view
    void ClearDiff( KIGFX::VIEW* aView );

    bool IsDiffActive() const;
    bool IsDiffActive( KIGFX::VIEW* aView ) const;

    // Returns true if the click was handled by the overlay
    // Checks the current view
    bool HandleClick( const VECTOR2I& aPoint );

    // Handle click on a specific view
    bool HandleClick( KIGFX::VIEW* aView, const VECTOR2I& aPoint );

    /**
     * Refresh the diff overlay on the current view.
     * Recomputes the bounding box from the tracker and updates the overlay.
     * Call this when tracked items may have moved.
     */
    void RefreshOverlay();

    /**
     * Refresh the diff overlay on a specific view.
     * @param aView The view to refresh.
     */
    void RefreshOverlay( KIGFX::VIEW* aView );

    /**
     * Refresh all active diff overlays.
     * Useful when items across multiple sheets may have changed.
     */
    void RefreshAllOverlays();

    /**
     * Get the change tracker for a view.
     * @return The tracker, or nullptr if not using item-based tracking.
     */
    AGENT_CHANGE_TRACKER* GetTracker( KIGFX::VIEW* aView ) const;

    /**
     * Get the sheet path for a view's overlay.
     * @return The sheet path string.
     */
    wxString GetSheetPath( KIGFX::VIEW* aView ) const;

    /**
     * Handle mouse motion for per-item hover detection.
     * Checks if the cursor is inside any per-item highlight bbox and updates
     * the overlay's hover state. Call from the selection tool's motion handler.
     * @return true if the hover state changed (caller should refresh canvas).
     */
    bool HandleMouseMotion( KIGFX::VIEW* aView, const VECTOR2I& aPoint );

private:
    DIFF_MANAGER();
    ~DIFF_MANAGER();

    void OnApprove( KIGFX::VIEW* aView );
    void OnReject( KIGFX::VIEW* aView );
    void OnViewDiff( KIGFX::VIEW* aView );

    // Non-copyable
    DIFF_MANAGER( const DIFF_MANAGER& ) = delete;
    DIFF_MANAGER& operator=( const DIFF_MANAGER& ) = delete;

    mutable std::recursive_mutex m_mutex;

    // Per-view state
    std::unordered_map<KIGFX::VIEW*, DIFF_VIEW_STATE> m_viewStates;

    // Current active view (set by RegisterOverlay, used by parameterless methods)
    KIGFX::VIEW* m_currentView = nullptr;
};

#endif // DIFF_MANAGER_H
