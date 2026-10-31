/* Migration tests use temporary folders, never the real user's Library. */
#import <Cocoa/Cocoa.h>
#include "game_data.h"
#include "bios.h"
#include <stdlib.h>
#include <stdio.h>

uint8_t ds_base[SEGMENT_SIZE];

int main(int argc, char **argv)
{
    if (argc != 2)
        return 2;
    @autoreleasepool {
        char pattern[] = "/tmp/alleycat-paths-XXXXXX";
        char *path = mkdtemp(pattern);
        if (!path)
            return 1;
        NSString *root = [NSString stringWithUTF8String:path];
        NSString *current = [root stringByAppendingPathComponent:@"Alley Cat"];
        NSString *legacy = [root stringByAppendingPathComponent:@"RetroShopPT/Alley Cat"];
        NSString *oldFile = [legacy stringByAppendingPathComponent:@"CAT.EXE"];
        NSString *newFile = [current stringByAppendingPathComponent:@"CAT.EXE"];
        NSFileManager *files = NSFileManager.defaultManager;
        int ok = !game_data_try_saved(current.fileSystemRepresentation, legacy.fileSystemRepresentation);
        [files createDirectoryAtPath:legacy withIntermediateDirectories:YES attributes:nil error:nil];
        NSData *original = [NSData dataWithContentsOfFile:[NSString stringWithUTF8String:argv[1]]];
        ok &= original && [original writeToFile:oldFile atomically:YES];
        ok &= game_data_try_saved(current.fileSystemRepresentation, legacy.fileSystemRepresentation);
        ok &= [[NSData dataWithContentsOfFile:newFile] isEqualToData:original];
        ok &= [[NSData dataWithContentsOfFile:oldFile] isEqualToData:original];
        [@"bad original" writeToFile:oldFile atomically:YES encoding:NSUTF8StringEncoding error:nil];
        ok &= game_data_try_saved(current.fileSystemRepresentation, legacy.fileSystemRepresentation);
        [@"bad current" writeToFile:newFile atomically:YES encoding:NSUTF8StringEncoding error:nil];
        ok &= !game_data_try_saved(current.fileSystemRepresentation, legacy.fileSystemRepresentation);
        [original writeToFile:oldFile atomically:YES];
        ok &= game_data_try_saved(current.fileSystemRepresentation, legacy.fileSystemRepresentation);
        ok &= [[NSData dataWithContentsOfFile:newFile] isEqualToData:original];
        ok &= game_data_try_saved("/dev/null/Alley Cat", legacy.fileSystemRepresentation);
        [files removeItemAtPath:root error:nil];
        if (ok)
            puts("paths: new folder, legacy migration, preserved original, invalid copies and save-failure fallback passed");
        return ok ? 0 : 1;
    }
}
