//
//  ViewController.m
//  ios
//
//  Created by Martin Kunev on 8.4.14.
//  Copyright (c) 2014 Sofia university. All rights reserved.
//

#include <pthread.h>

#import <AssetsLibrary/ALAssetsLibrary.h>

#import "types.h"
#import "filement.h"
#import "ViewController.h"

extern bool registered;

// Attempts to use the assets library so that the user will be asked to give the application access to it.
static void *ios_register(void *arg)
{
	// TODO think how to set status; it is a NULL pointer (it can't be something in the stack because it will go out of scope)

    __block int *status = arg;
    //*status = ERROR_PROGRESS;

    __block pthread_mutex_t *mutex = malloc(sizeof(*mutex));
    if (!mutex)
    {
        *status = ERROR_MEMORY;
        return 0;
    }
    pthread_mutex_init(mutex, 0);

    ALAssetsLibrary *library = [[ALAssetsLibrary alloc] init];
    pthread_mutex_lock(mutex);

	[library enumerateGroupsWithTypes: ALAssetsGroupAll usingBlock:
        ^(ALAssetsGroup *group, BOOL *stop)
        {
            //*status = 0;
			*stop = YES;
			pthread_mutex_unlock(mutex);
		}
	failureBlock:
		^(NSError *error)
		{
			//*status = ERROR_ACCESS;
			pthread_mutex_unlock(mutex);
		}
	];

    // Wait until the enumeration finishes.
    pthread_mutex_lock(mutex);
    pthread_mutex_unlock(mutex);

    [library release];
    pthread_mutex_destroy(mutex);
    free(mutex);

    return 0;
}

static bool input_string_init(UITextField *field, struct string *restrict string)
{
	NSString *value = [field text];

	string->length = [value lengthOfBytesUsingEncoding: NSUTF8StringEncoding];
	if (!string->length) return false;

	string->data = malloc(string->length + 1);
	if (!string->data) return false;
	if (string->length) memcpy(string->data, [value cStringUsingEncoding: NSUTF8StringEncoding], string->length);
	string->data[string->length] = 0;

	return true;
}

static void *main_server(void *arg)
{
    filement_serve();
    return 0;
}
static void serve(void)
{
    pthread_t thread;
    pthread_create(&thread, 0, &main_server, 0);
    pthread_detach(thread);
}

@implementation ViewController

- (void)didReceiveMemoryWarning
{
    [super didReceiveMemoryWarning];
    // Release any cached data, images, etc that aren't in use.
}

#pragma mark - View lifecycle

- (void)viewDidLoad
{
    [super viewDidLoad];
	// Do any additional setup after loading the view, typically from a nib.

	// Set default device name to the hostname.
	device_name_text.text = [[UIDevice currentDevice] name];

	if (registered) [connect setEnabled: false];
	else
	{
		// Make sure we are allowed to access the assets library.
		pthread_t thread;
		pthread_create(&thread, 0, ios_register, 0);
		pthread_detach(thread);
	}
}

- (void)viewDidUnload
{
    [super viewDidUnload];
    // Release any retained subviews of the main view.
    // e.g. self.myOutlet = nil;
}

- (void)viewWillAppear:(BOOL)animated
{
    [super viewWillAppear:animated];
}

- (void)viewDidAppear:(BOOL)animated
{
    [super viewDidAppear:animated];
}

- (void)viewWillDisappear:(BOOL)animated
{
	[super viewWillDisappear:animated];
}

- (void)viewDidDisappear:(BOOL)animated
{
	[super viewDidDisappear:animated];
}

- (BOOL)shouldAutorotateToInterfaceOrientation:(UIInterfaceOrientation)interfaceOrientation
{
    // Return YES for supported orientations
    if ([[UIDevice currentDevice] userInterfaceIdiom] == UIUserInterfaceIdiomPhone) {
        return (interfaceOrientation != UIInterfaceOrientationPortraitUpsideDown);
    } else {
        return YES;
    }
}

- (IBAction)connect: (id)sender
{
	struct string email, device_name, password;
	int status;

	[connect setEnabled: false];
	//[connect_progress startAnimation: self];

	status =
		input_string_init(device_name_text, &device_name) &&
		input_string_init(email_text, &email) &&
		input_string_init(device_password_text, &password);

	// TODO: memory leak here. strings are not always freed
	if (status)
	{
		status = filement_register(&email, &device_name, &password);
		free(email.data);
		free(device_name.data);
		free(password.data);

		//[connect_progress stopAnimation: self];

		if (status)
		{
			// Device registered successfully
			serve();

			//struct string path = string("/Applications/Filement.app");
			//mac_startup_add(&path);

			//[NSApp beginSheet:finish_sheet modalForWindow:(NSWindow *)window modalDelegate:self didEndSelector:nil contextInfo:nil];
			return;
		}
	}
	//else [connect_progress stopAnimation: self];

	//[error_text setStringValue:@"Invalid registration information"];
	//[NSApp beginSheet:error_sheet modalForWindow:(NSWindow *)window modalDelegate:self didEndSelector:nil contextInfo:nil];

	[connect setEnabled: true];
}

@end
