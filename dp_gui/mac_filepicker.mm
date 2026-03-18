// mac_filepicker.mm — Native macOS file picker using NSOpenPanel/NSSavePanel

#import <Cocoa/Cocoa.h>
#include <string>

// Run a panel as a sheet attached to the frontmost application window,
// so it always appears on top of the FLTK window that launched it.
static NSWindow* get_key_window() {
    // Use the currently active/key window so the sheet attaches correctly
    NSWindow* w = [[NSApplication sharedApplication] keyWindow];
    if (!w) w = [[NSApplication sharedApplication] mainWindow];
    return w;
}

std::string mac_pick_open(const char* title, const char* /*glob*/) {
    __block std::string result;

    void (^block)(void) = ^{
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        [panel setTitle:[NSString stringWithUTF8String:title]];
        [panel setCanChooseFiles:YES];
        [panel setCanChooseDirectories:NO];
        [panel setAllowsMultipleSelection:NO];
        [panel setAllowedFileTypes:@[@"bin", @"hex", @"img",
                                     @"s19", @"srec", @"mot", @"rom"]];
        [panel setLevel:NSModalPanelWindowLevel];

        NSWindow* parent = get_key_window();
        if (parent) {
            // Run as sheet attached to parent window — always on top
            [panel beginSheetModalForWindow:parent completionHandler:^(NSInteger response) {
                if (response == NSModalResponseOK) {
                    NSURL* url = [[panel URLs] firstObject];
                    if (url) result = [[url path] UTF8String];
                }
                [NSApp stopModal];
            }];
            [NSApp runModalForWindow:panel];
        } else {
            // Fallback: run as standalone modal
            if ([panel runModal] == NSModalResponseOK) {
                NSURL* url = [[panel URLs] firstObject];
                if (url) result = [[url path] UTF8String];
            }
        }
    };

    if ([NSThread isMainThread]) block();
    else dispatch_sync(dispatch_get_main_queue(), block);
    return result;
}

std::string mac_pick_save(const char* title) {
    __block std::string result;

    void (^block)(void) = ^{
        NSSavePanel* panel = [NSSavePanel savePanel];
        [panel setTitle:[NSString stringWithUTF8String:title]];
        [panel setAllowedFileTypes:@[@"bin"]];
        [panel setCanCreateDirectories:YES];
        [panel setLevel:NSModalPanelWindowLevel];

        NSWindow* parent = get_key_window();
        if (parent) {
            [panel beginSheetModalForWindow:parent completionHandler:^(NSInteger response) {
                if (response == NSModalResponseOK) {
                    NSURL* url = [panel URL];
                    if (url) result = [[url path] UTF8String];
                }
                [NSApp stopModal];
            }];
            [NSApp runModalForWindow:panel];
        } else {
            if ([panel runModal] == NSModalResponseOK) {
                NSURL* url = [panel URL];
                if (url) result = [[url path] UTF8String];
            }
        }
    };

    if ([NSThread isMainThread]) block();
    else dispatch_sync(dispatch_get_main_queue(), block);
    return result;
}
