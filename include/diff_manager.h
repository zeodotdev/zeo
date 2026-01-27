#ifndef DIFF_MANAGER_H
#define DIFF_MANAGER_H

#include <memory>
#include <mutex>
#include <functional>
#include <unordered_map>
#include <wx/string.h>
#include <math/box2.h>
#include <math/vector2d.h>

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
 * Per-view diff overlay state
 */
struct DIFF_VIEW_STATE
{
    bool                               active = false;
    BOX2I                              currentBBox;
    KIGFX::PREVIEW::DIFF_OVERLAY_ITEM* item = nullptr;
    DIFF_CALLBACKS                     callbacks;
};

class DIFF_MANAGER
{
public:
    static DIFF_MANAGER& GetInstance();

    // Show diff on the currently active view (set via RegisterOverlay)
    void ShowDiff( const BOX2I& aBBox );

    // Register a view to draw the overlay on, and callbacks for interaction
    // This sets the "current" view for subsequent ShowDiff/ClearDiff calls
    void RegisterOverlay( KIGFX::VIEW* aView, DIFF_CALLBACKS aCallbacks );

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
