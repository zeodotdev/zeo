#ifndef MACOS_TERMINAL_KEY_MONITOR_H
#define MACOS_TERMINAL_KEY_MONITOR_H

#include <functional>

/**
 * Keyboard shortcuts recognized by the terminal key monitor.
 */
enum class TERMINAL_KEY_SHORTCUT
{
    NEW_TAB,          // Cmd+T
    CLOSE_TAB,        // Cmd+W
    SWITCH_TAB_1,     // Cmd+1
    SWITCH_TAB_2,     // Cmd+2
    SWITCH_TAB_3,     // Cmd+3
    SWITCH_TAB_4,     // Cmd+4
    SWITCH_TAB_5,     // Cmd+5
    SWITCH_TAB_6,     // Cmd+6
    SWITCH_TAB_7,     // Cmd+7
    SWITCH_TAB_8,     // Cmd+8
    SWITCH_TAB_9      // Cmd+9
};

/**
 * Install an NSEvent local monitor that intercepts keyboard shortcuts when the
 * first responder is a descendant of the given native view. This is needed
 * because WKWebView handles Cmd+key through the Cocoa responder chain,
 * bypassing wxWidgets event processing entirely.
 */
void InstallTerminalKeyboardMonitor( void* aNativeView,
                                     std::function<void( TERMINAL_KEY_SHORTCUT )> aCallback );
void RemoveTerminalKeyboardMonitor();

#endif // MACOS_TERMINAL_KEY_MONITOR_H
