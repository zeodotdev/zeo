#ifndef DIFF_MANAGER_H
#define DIFF_MANAGER_H

#include <memory>
#include <mutex>
#include <functional>
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
};

class DIFF_MANAGER
{
public:
    static DIFF_MANAGER& GetInstance();

    void ShowDiff( const BOX2I& aBBox );

    // Register a view to draw the overlay on, and callbacks for interaction
    void RegisterOverlay( KIGFX::VIEW* aView, DIFF_CALLBACKS aCallbacks );
    void UnregisterOverlay();

    void ClearDiff();

    bool IsDiffActive() const { return m_active; }

    // Returns true if the click was handled by the overlay
    bool HandleClick( const VECTOR2I& aPoint );

private:
    DIFF_MANAGER();
    ~DIFF_MANAGER();

    void OnApprove();
    void OnReject();
    void OnViewBefore();
    void OnViewAfter();

    // Non-copyable
    DIFF_MANAGER( const DIFF_MANAGER& ) = delete;
    DIFF_MANAGER& operator=( const DIFF_MANAGER& ) = delete;

    mutable std::recursive_mutex m_mutex;
    bool               m_active;
    BOX2I              m_currentBBox;

    KIGFX::VIEW*                       m_view;
    KIGFX::PREVIEW::DIFF_OVERLAY_ITEM* m_item;
    DIFF_CALLBACKS                     m_callbacks;
};

#endif // DIFF_MANAGER_H
