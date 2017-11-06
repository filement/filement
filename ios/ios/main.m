//
//  main.m
//  ios
//
//  Created by Martin Kunev on 8.4.14.
//  Copyright (c) 2014 Sofia university. All rights reserved.
//

#import <UIKit/UIKit.h>

#import <MediaPlayer/MediaPlayer.h>

#import "AppDelegate.h"
#import "types.h"
#import "filement.h"

bool registered;

/*NSAutoreleasePool *p = [[NSAutoreleasePool alloc] init];
NSMutableString *outText = [[NSMutableString alloc] initWithString:@"Albums:"];
[outText appendFormat:@"\r\n count:%i",[[[MPMediaQuery albumsQuery] collections] count]];
for (MPMediaItemCollection *collection in [[MPMediaQuery albumsQuery] collections]) {
        [outText appendFormat:@"\r\n -%@",[[collection representativeItem] valueForProperty:MPMediaItemPropertyAlbumTitle]];
}

[outText appendString:@"\r\n\r\n Artist:"];

for (MPMediaItemCollection *collection in [[MPMediaQuery artistsQuery] collections]) {
        [outText appendFormat:@"\r\n -%@",[[collection representativeItem] valueForProperty:MPMediaItemPropertyArtist]];
}
NSLog(@"%@",[outText autorelease]);
	[p release];

	return 0;*/

int main(int argc, char *argv[])
{
    filement_daemon();
    registered = filement_init();

	NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
	int status = UIApplicationMain(argc, argv, nil, NSStringFromClass([AppDelegate class]));
	[pool release];
	return status;
}