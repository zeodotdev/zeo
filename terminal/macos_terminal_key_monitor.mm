#import <Cocoa/Cocoa.h>
#include "macos_terminal_key_monitor.h"

static id s_terminalEventMonitor = nil;
static NSWindow* s_terminalWindow = nil;

void InstallTerminalKeyboardMonitor( void* aNativeView,
                                     std::function<void( TERMINAL_KEY_SHORTCUT )> aCallback )
{
    if( s_terminalEventMonitor )
        return;

    // Get the window from the passed view (GetHandle() returns the frame's content view)
    NSView* nativeView = (__bridge NSView*) aNativeView;
    s_terminalWindow = [nativeView window];

    if( !s_terminalWindow )
        return;  // Window not yet available; monitor won't be installed

    s_terminalEventMonitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown
                                                                   handler:^NSEvent*( NSEvent* event )
    {
        // Only intercept events targeting the terminal's window
        NSWindow* eventWindow = [event window];
        if( !eventWindow || eventWindow != s_terminalWindow )
            return event;

        NSUInteger flags = [event modifierFlags] & NSEventModifierFlagDeviceIndependentFlagsMask;
        bool       hasCmd   = ( flags & NSEventModifierFlagCommand ) != 0;
        bool       hasShift = ( flags & NSEventModifierFlagShift ) != 0;
        bool       hasCtrl  = ( flags & NSEventModifierFlagControl ) != 0;
        bool       hasOpt   = ( flags & NSEventModifierFlagOption ) != 0;
        NSString*  key      = [[event charactersIgnoringModifiers] lowercaseString];

        TERMINAL_KEY_SHORTCUT shortcut;
        bool                  matched = false;

        if( hasCmd && !hasShift && !hasCtrl && !hasOpt )
        {
            if( [key isEqualToString:@"t"] )
            {
                shortcut = TERMINAL_KEY_SHORTCUT::NEW_TAB;
                matched = true;
            }
            else if( [key isEqualToString:@"w"] )
            {
                shortcut = TERMINAL_KEY_SHORTCUT::CLOSE_TAB;
                matched = true;
            }
            else if( [key isEqualToString:@"1"] )
            {
                shortcut = TERMINAL_KEY_SHORTCUT::SWITCH_TAB_1;
                matched = true;
            }
            else if( [key isEqualToString:@"2"] )
            {
                shortcut = TERMINAL_KEY_SHORTCUT::SWITCH_TAB_2;
                matched = true;
            }
            else if( [key isEqualToString:@"3"] )
            {
                shortcut = TERMINAL_KEY_SHORTCUT::SWITCH_TAB_3;
                matched = true;
            }
            else if( [key isEqualToString:@"4"] )
            {
                shortcut = TERMINAL_KEY_SHORTCUT::SWITCH_TAB_4;
                matched = true;
            }
            else if( [key isEqualToString:@"5"] )
            {
                shortcut = TERMINAL_KEY_SHORTCUT::SWITCH_TAB_5;
                matched = true;
            }
            else if( [key isEqualToString:@"6"] )
            {
                shortcut = TERMINAL_KEY_SHORTCUT::SWITCH_TAB_6;
                matched = true;
            }
            else if( [key isEqualToString:@"7"] )
            {
                shortcut = TERMINAL_KEY_SHORTCUT::SWITCH_TAB_7;
                matched = true;
            }
            else if( [key isEqualToString:@"8"] )
            {
                shortcut = TERMINAL_KEY_SHORTCUT::SWITCH_TAB_8;
                matched = true;
            }
            else if( [key isEqualToString:@"9"] )
            {
                shortcut = TERMINAL_KEY_SHORTCUT::SWITCH_TAB_9;
                matched = true;
            }
        }

        if( matched )
        {
            aCallback( shortcut );
            return nil; // Consume the event
        }

        return event;
    }];
}

void RemoveTerminalKeyboardMonitor()
{
    if( s_terminalEventMonitor )
    {
        [NSEvent removeMonitor:s_terminalEventMonitor];
        s_terminalEventMonitor = nil;
    }
    s_terminalWindow = nil;
}
