#ifndef MACOS_WEBVIEW_BG_H
#define MACOS_WEBVIEW_BG_H

/**
 * Disable the default white background on WKWebView so the dark parent panel
 * shows through while HTML content is loading, preventing the white flash.
 */
void SetWebViewDarkBackground( void* aNativeHandle );

/**
 * Check if macOS system appearance is set to dark mode.
 * @return true if dark mode, false if light mode
 */
bool IsSystemDarkMode();

#endif // MACOS_WEBVIEW_BG_H
