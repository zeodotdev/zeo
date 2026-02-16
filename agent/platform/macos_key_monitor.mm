#import <Cocoa/Cocoa.h>
#include "macos_key_monitor.h"

static id s_eventMonitor = nil;

void InstallKeyboardMonitor( void* aNativeView,
                             std::function<void( KEY_SHORTCUT )> aCallback )
{
    if( s_eventMonitor )
        return;

    s_eventMonitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown
                                                           handler:^NSEvent*( NSEvent* event )
    {
        NSUInteger flags = [event modifierFlags] & NSEventModifierFlagDeviceIndependentFlagsMask;
        bool       hasCmd   = ( flags & NSEventModifierFlagCommand ) != 0;
        bool       hasShift = ( flags & NSEventModifierFlagShift ) != 0;
        bool       hasCtrl  = ( flags & NSEventModifierFlagControl ) != 0;
        bool       hasOpt   = ( flags & NSEventModifierFlagOption ) != 0;
        NSString*  key      = [[event charactersIgnoringModifiers] lowercaseString];

        KEY_SHORTCUT shortcut;
        bool         matched = false;

        // Escape (no modifiers required)
        if( [event keyCode] == 53 && !hasCmd && !hasShift && !hasCtrl && !hasOpt )
        {
            shortcut = KEY_SHORTCUT::STOP_GENERATING;
            matched = true;
        }
        else if( hasCmd )
        {
            if( !hasShift && !hasCtrl && !hasOpt && [key isEqualToString:@"a"] )
            {
                shortcut = KEY_SHORTCUT::SELECT_ALL;
                matched = true;
            }
            else if( !hasShift && !hasCtrl && !hasOpt && [key isEqualToString:@"z"] )
            {
                shortcut = KEY_SHORTCUT::UNDO;
                matched = true;
            }
            else if( hasShift && !hasCtrl && !hasOpt && [key isEqualToString:@"z"] )
            {
                shortcut = KEY_SHORTCUT::REDO;
                matched = true;
            }
            else if( !hasShift && !hasCtrl && !hasOpt && [key isEqualToString:@"l"] )
            {
                shortcut = KEY_SHORTCUT::FOCUS_INPUT;
                matched = true;
            }
            else if( !hasShift && !hasCtrl && !hasOpt && [key isEqualToString:@"n"] )
            {
                shortcut = KEY_SHORTCUT::NEW_CHAT;
                matched = true;
            }
        }

        if( matched )
        {
            NSView*  nativeView  = (__bridge NSView*) aNativeView;
            NSWindow* eventWindow = [event window];

            // Only intercept events targeting the agent's own window
            if( eventWindow && eventWindow == [nativeView window] )
            {
                NSResponder* responder = [eventWindow firstResponder];

                if( [responder isKindOfClass:[NSView class]] &&
                    [(NSView*) responder isDescendantOf:nativeView] )
                {
                    aCallback( shortcut );
                    return nil; // Consume the event
                }
            }
        }

        return event;
    }];
}

void RemoveKeyboardMonitor()
{
    if( s_eventMonitor )
    {
        [NSEvent removeMonitor:s_eventMonitor];
        s_eventMonitor = nil;
    }
}
