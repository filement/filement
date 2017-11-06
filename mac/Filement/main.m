#define _BSD_SOURCE
#define OS_BSD
#define OS_MAC
#define DEVICE

#import <Cocoa/Cocoa.h>
#import <string.h>

#import "filement.h"

bool registered;

int main(int argc, char *argv[])
{
    filement_daemon();
    registered = filement_init();

    // Check for new version of the Filement device software.
    // TODO do this after the interface is started (so the user has some indication that something is happening)
    //if (!filement_upgrade()) error(logs("Upgrade failed"));
    //if (registered) filement_upgrade();

    return NSApplicationMain(argc, (const char **)argv);
}
