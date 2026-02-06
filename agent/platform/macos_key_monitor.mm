#import <Cocoa/Cocoa.h>
#include "macos_key_monitor.h"

static id s_eventMonitor = nil;

void InstallSelectAllMonitor( void* aInputNativeView, std::function<void()> aCallback )
{
    if( s_eventMonitor )
        return;

    s_eventMonitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown
                                                           handler:^NSEvent*( NSEvent* event )
    {
        // Check for Cmd+A (no other modifiers like Shift, Ctrl, Option)
        NSUInteger flags = [event modifierFlags] & NSEventModifierFlagDeviceIndependentFlagsMask;

        if( ( flags & NSEventModifierFlagCommand ) &&
            !( flags & ( NSEventModifierFlagShift | NSEventModifierFlagControl | NSEventModifierFlagOption ) ) &&
            [[event charactersIgnoringModifiers] isEqualToString:@"a"] )
        {
            NSView* inputView = (__bridge NSView*) aInputNativeView;
            NSResponder* responder = [[inputView window] firstResponder];

            // Check if the first responder is the input view or a descendant of it
            if( [responder isKindOfClass:[NSView class]] &&
                [(NSView*) responder isDescendantOf:inputView] )
            {
                aCallback();
                return nil; // Consume the event
            }
        }

        return event;
    }];
}

void RemoveSelectAllMonitor()
{
    if( s_eventMonitor )
    {
        [NSEvent removeMonitor:s_eventMonitor];
        s_eventMonitor = nil;
    }
}
