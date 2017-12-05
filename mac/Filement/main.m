#define _BSD_SOURCE

#import <Cocoa/Cocoa.h>
#import <string.h>

#import "filement.h"

bool registered;

int main(int argc, char *argv[])
{
	extern bool startup_mac_add(const struct string *file);
	extern bool startup_mac_remove(const struct string *file);

	extern bool (*startup_add)(const struct string *);
	extern bool (*startup_remove)(const struct string *);

	startup_add = &startup_mac_add;
	startup_remove = &startup_mac_remove;

    filement_daemon();
    registered = filement_init();

#if defined(FILEMENT_UPGRADE)
	// Check for new version of the Filement device software.
	// TODO maybe do this after the interface is started
	if (registered && !filement_upgrade(PREFIX "/Contents/MacOS/Filement"))
		error(logs("Upgrade failed"));
#endif

    return NSApplicationMain(argc, (const char **)argv);
}
