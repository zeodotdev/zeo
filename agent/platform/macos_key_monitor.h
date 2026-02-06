#ifndef MACOS_KEY_MONITOR_H
#define MACOS_KEY_MONITOR_H

#include <functional>

/**
 * Install an NSEvent local monitor that intercepts Cmd+A when the first responder
 * is a descendant of the given native view, and calls the provided callback instead.
 * This is needed because WKWebView handles Cmd+A through the Cocoa responder chain,
 * bypassing wxWidgets event processing entirely.
 */
void InstallSelectAllMonitor( void* aInputNativeView, std::function<void()> aCallback );
void RemoveSelectAllMonitor();

#endif // MACOS_KEY_MONITOR_H
