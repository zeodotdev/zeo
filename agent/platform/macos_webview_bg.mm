#import <Cocoa/Cocoa.h>
#include "macos_webview_bg.h"

void SetWebViewDarkBackground( void* aNativeHandle )
{
    if( !aNativeHandle )
        return;

    NSView* view = (__bridge NSView*) aNativeHandle;

    // Use NSClassFromString to avoid linking WebKit framework directly
    Class wkWebViewClass = NSClassFromString( @"WKWebView" );

    if( wkWebViewClass && [view isKindOfClass:wkWebViewClass] )
    {
        [view setValue:@NO forKey:@"drawsBackground"];
    }
}
