#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#include <stddef.h>
#include <string.h>

@interface MoonMacPasswordField : NSSecureTextField
- (void)pasteFromClipboard:(id)sender;
@end

@implementation MoonMacPasswordField
- (void)pasteFromClipboard:(id)sender {
    (void)sender;
    NSString *value = [[NSPasteboard generalPasteboard] stringForType:NSPasteboardTypeString];
    if (!value) {
        NSBeep();
        return;
    }
    self.stringValue = value;
    [self.window makeFirstResponder:self];
    NSText *editor = [self.window fieldEditor:NO forObject:self];
    if (editor) [editor setSelectedRange:NSMakeRange(value.length, 0)];
}
@end

int MoonMac_PromptPassword(char *output, size_t capacity) {
    if (!output || capacity < 2) return -1;
    output[0] = '\0';
    @autoreleasepool {
        NSAlert *alert = [[NSAlert alloc] init];
        alert.messageText = @"Moon Sweepers server password";
        alert.informativeText = @"Paste the password from the host, or type it below.";
        [alert addButtonWithTitle:@"Join"];
        [alert addButtonWithTitle:@"Cancel"];
        MoonMacPasswordField *field = [[MoonMacPasswordField alloc] initWithFrame:NSMakeRect(0, 4, 296, 26)];
        field.bezelStyle = NSTextFieldRoundedBezel;
        field.placeholderString = @"Server password";
        field.font = [NSFont systemFontOfSize:14];
        NSButton *pasteButton = [NSButton buttonWithTitle:@"Paste"
                                                   target:field
                                                   action:@selector(pasteFromClipboard:)];
        pasteButton.frame = NSMakeRect(304, 4, 72, 26);
        pasteButton.bezelStyle = NSBezelStyleRounded;
        NSView *row = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 376, 34)];
        [row addSubview:field];
        [row addSubview:pasteButton];
        alert.accessoryView = row;
        alert.window.initialFirstResponder = field;
        if ([alert runModal] != NSAlertFirstButtonReturn) return 0;
        const char *value = field.stringValue.UTF8String;
        if (!value || strlen(value) >= capacity) return -1;
        memcpy(output, value, strlen(value) + 1);
        field.stringValue = @"";
        return 1;
    }
}

void MoonMac_ShowNotice(const char *message) {
    if (!message) return;
    @autoreleasepool {
        NSAlert *alert = [[NSAlert alloc] init];
        alert.messageText = @"Could not join Moon Sweepers";
        alert.informativeText = [NSString stringWithUTF8String:message] ?: @"The connection failed.";
        [alert addButtonWithTitle:@"OK"];
        [alert runModal];
    }
}
