#ifndef MACOS_KEY_MONITOR_H
#define MACOS_KEY_MONITOR_H

#include <functional>

/**
 * Keyboard shortcuts recognized by the native key monitor.
 */
enum class KEY_SHORTCUT
{
    SELECT_ALL,       // Cmd+A
    UNDO,             // Cmd+Z
    REDO,             // Cmd+Shift+Z
    FOCUS_INPUT,      // Cmd+L
    STOP_GENERATING,  // Escape
    NEW_CHAT,         // Cmd+N
    SEARCH_CHAT       // Cmd+F
};

/**
 * Install an NSEvent local monitor that intercepts keyboard shortcuts when the
 * first responder is a descendant of the given native view.  This is needed
 * because WKWebView handles Cmd+key through the Cocoa responder chain,
 * bypassing wxWidgets event processing entirely.
 */
void InstallKeyboardMonitor( void* aNativeView,
                             std::function<void( KEY_SHORTCUT )> aCallback );
void RemoveKeyboardMonitor();

#endif // MACOS_KEY_MONITOR_H
