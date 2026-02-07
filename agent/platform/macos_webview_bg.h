#ifndef MACOS_WEBVIEW_BG_H
#define MACOS_WEBVIEW_BG_H

/**
 * Disable the default white background on WKWebView so the dark parent panel
 * shows through while HTML content is loading, preventing the white flash.
 */
void SetWebViewDarkBackground( void* aNativeHandle );

#endif // MACOS_WEBVIEW_BG_H
