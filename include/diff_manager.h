#ifndef DIFF_MANAGER_H
#define DIFF_MANAGER_H

#include <memory>
#include <mutex>
#include <functional>
#include <unordered_map>
#include <wx/string.h>
#include <math/box2.h>
#include <math/vector2d.h>

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
    std::function<void()> onUndo;      ///< Called when "View Before" is clicked
    std::function<void()> onRedo;      ///< Called when "View After" is clicked
    std::function<void()> onApprove;   ///< Called when "Approve" is clicked
    std::function<void()> onReject;    ///< Called when "Reject" is clicked
    std::function<void()> onRefresh;   ///< Called to trigger canvas refresh after view changes
};

/**
 * Callback function type for computing dynamic bounding boxes.
 * This allows the DIFF_MANAGER to request updated bounding boxes from the
 * frame classes that have access to SCHEMATIC or BOARD objects.
 */
using BBOX_COMPUTE_CALLBACK = std::function<BOX2I()>;

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
    // aDiffViewMode=true draws Before/After toggle buttons instead of Approve/Reject/Undo.
    void ShowDiff( const BOX2I& aBBox, bool aDiffViewMode = false );

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
                          BBOX_COMPUTE_CALLBACK aBBoxCallback );

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
     * Update the "showing before" flag on the overlay item for a specific view.
     * Used by SCH_DIFF_FRAME when the user switches between before/after states
     * without going through the canvas button click path.
     */
    void SetShowingBefore( KIGFX::VIEW* aView, bool aShowBefore );

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

private:
    DIFF_MANAGER();
    ~DIFF_MANAGER();

    void OnApprove( KIGFX::VIEW* aView );
    void OnReject( KIGFX::VIEW* aView );
    void OnViewBefore( KIGFX::VIEW* aView );
    void OnViewAfter( KIGFX::VIEW* aView );

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
