#import <Cocoa/Cocoa.h>

extern bool startup_mac_add(const struct string *file);
extern bool startup_mac_remove(const struct string *file);

@interface AppDelegate : NSObject <NSApplicationDelegate>
{
    IBOutlet NSPanel *finish_sheet;
    IBOutlet NSPanel *error_sheet;

    IBOutlet NSProgressIndicator *connect_progress;

    IBOutlet NSButton *connect;
    
    IBOutlet NSWindow *reset_window;
    IBOutlet NSWindow *window;

    IBOutlet NSTextField *device_name_text;
    IBOutlet NSTextField *client_id_text;
    IBOutlet NSTextField *password_text;
    IBOutlet NSTextField *error_text;

    NSMenu *menu;
}

@end
