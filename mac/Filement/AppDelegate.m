#define _BSD_SOURCE
#define OS_BSD
#define OS_MAC
#define DEVICE

#include <stdlib.h>
#include <unistd.h>

#include <pthread.h>		// libpthread

#import "AppDelegate.h"
#import "filement.h"

// TODO: is the icon for retina display OK?

static bool mac_startup_add(const struct string *file)
{
	// Get application path
	NSString *path;
	if (file) path = [NSString stringWithUTF8String:file->data];
	else path = [[NSBundle mainBundle] bundlePath];
	CFURLRef url = (CFURLRef)[NSURL fileURLWithPath:path];

	// Create a reference to the shared file list.
	// We are adding it for the current user only.
	LSSharedFileListRef startup = LSSharedFileListCreate(0, kLSSharedFileListSessionLoginItems, 0);
	if (startup)
	{
		// Insert an item to the list.
		LSSharedFileListItemRef item = LSSharedFileListInsertItemURL(startup, kLSSharedFileListItemLast, 0, 0, url, 0, 0);
		if (item) CFRelease(item);
	}

	CFRelease(startup);

	return true;
}

static bool mac_startup_remove(const struct string *file)
{
	// Get application path
	NSString *path;
	if (file) path = [NSString stringWithUTF8String:file->data];
	else path = [[NSBundle mainBundle] bundlePath];
	CFURLRef url = (CFURLRef)[NSURL fileURLWithPath:path]; 

	// Create a reference to the shared file list.
	LSSharedFileListRef loginItems = LSSharedFileListCreate(NULL, kLSSharedFileListSessionLoginItems, NULL);

	if (loginItems)
	{
		UInt32 seedValue;
		//Retrieve the list of Login Items and cast them to
		// a NSArray so that it will be easier to iterate.
		NSArray *loginItemsArray = (NSArray *)LSSharedFileListCopySnapshot(loginItems, &seedValue);
		size_t i;
		for(i = 0; i < [loginItemsArray count]; i++)
		{
			LSSharedFileListItemRef itemRef = (LSSharedFileListItemRef)[loginItemsArray objectAtIndex:i];
			//Resolve the item with URL
			if (LSSharedFileListItemResolve(itemRef, 0, (CFURLRef*) &url, NULL) == noErr)
			{
				if ([[(NSURL*)url path] compare:path] == NSOrderedSame)
				{
					LSSharedFileListItemRemove(loginItems, itemRef);
					break;
				}
			}
		}
		[loginItemsArray release];
	}

	return true;
}

static bool input_string_init(NSTextField *field, struct string *restrict string)
{
	NSString *value = [field stringValue];

	string->length = [value lengthOfBytesUsingEncoding: NSUTF8StringEncoding];
	if (!string->length) return false;

	string->data = malloc(sizeof(char) * (string->length + 1));
	if (!string->data) return false;
	memcpy(string->data, [value cStringUsingEncoding: NSUTF8StringEncoding], string->length);
	string->data[string->length] = 0;

	return true;
}

static void *main_server(void *arg)
{
	filement_serve();
	return 0;
}
static void serve(NSMenu *menu)
{
	NSMenuItem *reset = [menu itemWithTitle:@"Reset Filement"];
	[reset setHidden:NO];

	pthread_t thread;
	pthread_create(&thread, 0, &main_server, 0);
	pthread_detach(thread);
}

@implementation AppDelegate

- (void)dealloc
{
	[super dealloc];
}

- (IBAction)quit
{
	_exit(0);
}

// TODO:error 2013-04-27 17:06:26.360 Filement[9965:203] Could not connect the action quit1: to target of class AppDelegate
// maybe this happens when the menu is not loaded
/*- (IBAction)quit1
{
	_exit(0);
}*/

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification
{
	extern bool registered;

	NSStatusItem *theItem = [[NSStatusBar systemStatusBar] statusItemWithLength:NSSquareStatusItemLength];
	[theItem retain];

	NSImage *icon = [NSImage imageNamed:@"menu"];
	[theItem setImage:icon];
	[theItem setHighlightMode:YES];

	// Create application menu.
	menu = [[NSMenu alloc] init];
	[menu addItemWithTitle:@"Reset Filement" action:@selector(reset) keyEquivalent:@""];
	[menu addItemWithTitle:@"Quit Filement" action:@selector(quit) keyEquivalent:@""];
	NSMenuItem *reset = [menu itemWithTitle:@"Reset Filement"];
	[reset setHidden:YES];
	[theItem setMenu:menu];
	[menu release];

	if (registered) serve(menu);
	else
	{
		[window setBackgroundColor:[NSColor colorWithPatternImage:[NSImage imageNamed:@"background.png"]]];

		[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

		[NSApp activateIgnoringOtherApps:YES];

		// Set default device name to the hostname
		[device_name_text setStringValue:[[NSHost currentHost] localizedName]];

		// Set input focus
		[window makeFirstResponder:client_id_text];

		// Pop window to foreground
		[window makeKeyAndOrderFront:self];
	}
}

- (IBAction)connect: (id)sender
{
	struct string email, device_name, password;
	bool status;

	[connect setEnabled: false];
	[connect_progress startAnimation: self];

	status =
		input_string_init(client_id_text, &email) &&
		input_string_init(device_name_text, &device_name) &&
		input_string_init(password_text, &password);

	// TODO: memory leak here. strings are not always freed
	if (status)
	{
		status = filement_register(&email, &device_name, &password);
		free(email.data);
		free(device_name.data);
		free(password.data);

		[connect_progress stopAnimation: self];

		if (status)
		{
			// Device registered successfully

			struct string path = string("/Applications/Filement.app");
			mac_startup_add(&path);

			[NSApp beginSheet:finish_sheet modalForWindow:(NSWindow *)window modalDelegate:self didEndSelector:nil contextInfo:nil];
			return;
		}
	}
	else [connect_progress stopAnimation: self];

	[error_text setStringValue:@"Invalid registration information"];
	[NSApp beginSheet:error_sheet modalForWindow:(NSWindow *)window modalDelegate:self didEndSelector:nil contextInfo:nil];
}
- (IBAction)finish: (id)sender
{
	[NSApp endSheet:finish_sheet];
	[finish_sheet orderOut:sender];
	[window close];
	[NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
	serve(menu);
}
- (IBAction)okay: (id)sender
{	
	[NSApp endSheet:error_sheet];
	[error_sheet orderOut:sender];
	[connect setEnabled: true];
}

- (IBAction)dialog_reset_no: (id)sender
{
	[reset_window orderOut:self];
}
- (IBAction)dialog_reset_yes: (id)sender
{
	struct string path = string("/Applications/Filement.app");
	mac_startup_remove(&path);

	filement_reset();
}
- (void)reset
{
	[NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];

	[NSApp activateIgnoringOtherApps:YES];

	// Pop window to foreground
	[reset_window makeKeyAndOrderFront:self];
}
@end
