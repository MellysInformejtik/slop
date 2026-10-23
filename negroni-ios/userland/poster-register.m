// Register Apple's existing wallpaper providers with LaunchServices on the iOS 16 bring-up root.
// API signatures are also documented by ProcursusTeam/uikittools-ng (uicache.m).
#import <Foundation/Foundation.h>
#include <dlfcn.h>
#include <stdio.h>
#include <unistd.h>

@interface LSPlugInKitProxy : NSObject
- (NSString *)bundleIdentifier;
@end

@interface LSApplicationWorkspace : NSObject
+ (instancetype)defaultWorkspace;
- (NSArray *)installedPlugins;
- (BOOL)registerPlugin:(NSURL *)url;
@end

static NSSet<NSString *> *installed(LSApplicationWorkspace *workspace)
{
    NSMutableSet *result = [NSMutableSet set];
    for (LSPlugInKitProxy *proxy in [workspace installedPlugins]) {
        if ([proxy respondsToSelector:@selector(bundleIdentifier)]) {
            NSString *identifier = [proxy bundleIdentifier];
            if ([identifier isKindOfClass:NSString.class]) [result addObject:identifier];
        }
    }
    return result;
}

int main(int argc, const char *argv[])
{
    @autoreleasepool {
        @try {
            if (argc != 2) return 64;
            NSData *manifest = [NSData dataWithContentsOfFile:[NSString stringWithUTF8String:argv[1]]];
            id payload = manifest ? [NSPropertyListSerialization propertyListWithData:manifest options:0 format:NULL error:NULL] : nil;
            NSArray *providers = [payload isKindOfClass:NSArray.class] ? payload : nil;
            if (!providers.count) {
                fprintf(stderr, "NEGRONI POSTER: missing provider manifest\n");
                return 1;
            }
            dlopen("/System/Library/Frameworks/CoreServices.framework/CoreServices", RTLD_NOW);
            LSApplicationWorkspace *workspace = [NSClassFromString(@"LSApplicationWorkspace") defaultWorkspace];
            if (![workspace respondsToSelector:@selector(registerPlugin:)] ||
                ![workspace respondsToSelector:@selector(installedPlugins)]) {
                fprintf(stderr, "NEGRONI POSTER: LaunchServices plugin API unavailable\n");
                return 1;
            }
            NSMutableSet *expected = [NSMutableSet set];
            // Validate the entire manifest before making a registration request.
            for (NSDictionary *provider in providers) {
                NSString *path = provider[@"Path"], *identifier = provider[@"BundleIdentifier"];
                if (![path isKindOfClass:NSString.class] || ![identifier isKindOfClass:NSString.class] ||
                    ![path hasSuffix:@".appex"] || ![identifier hasPrefix:@"com.apple."] ||
                    (![path hasPrefix:@"/Applications/"] && ![path hasPrefix:@"/System/Library/"]) ||
                    ![path isEqualToString:path.stringByStandardizingPath]) return 1;
                NSDictionary *info = [NSDictionary dictionaryWithContentsOfFile:[path stringByAppendingPathComponent:@"Info.plist"]];
                NSString *executable = info[@"CFBundleExecutable"];
                if (![info[@"CFBundleIdentifier"] isEqual:identifier] ||
                    ![info[@"NSExtension"][@"NSExtensionPointIdentifier"] isEqual:@"com.apple.posterkit.provider"] ||
                    ![executable isKindOfClass:NSString.class] ||
                    ![executable isEqualToString:executable.lastPathComponent] ||
                    ![[NSFileManager defaultManager] isExecutableFileAtPath:[path stringByAppendingPathComponent:executable]]) {
                    fprintf(stderr, "NEGRONI POSTER: invalid provider %s\n", path.UTF8String);
                    return 1;
                }
                [expected addObject:identifier];
            }
            // Give lsd's boot-time migration time to finish; no database deletion or blanket rebuild.
            sleep(30);
            NSSet *before = installed(workspace);
            for (NSDictionary *provider in providers) {
                NSString *identifier = provider[@"BundleIdentifier"];
                if ([before containsObject:identifier]) continue;
                BOOL ok = [workspace registerPlugin:[NSURL fileURLWithPath:provider[@"Path"] isDirectory:YES]];
                fprintf(stderr, "NEGRONI POSTER: register %s = %s\n", identifier.UTF8String, ok ? "accepted" : "rejected");
            }
            for (unsigned attempt = 0; attempt < 20; attempt++) {
                NSSet *visible = installed(workspace);
                if ([expected isSubsetOfSet:visible]) {
                    fprintf(stderr, "NEGRONI POSTER: verified %lu/%lu providers in LaunchServices\n",
                            (unsigned long)expected.count, (unsigned long)expected.count);
                    return 0;
                }
                if (attempt == 19) {
                    NSMutableSet *missing = [expected mutableCopy];
                    [missing minusSet:visible];
                    fprintf(stderr, "NEGRONI POSTER: still missing %s\n", missing.description.UTF8String);
                } else sleep(1);
            }
        } @catch (NSException *exception) {
            fprintf(stderr, "NEGRONI POSTER: %s: %s\n", exception.name.UTF8String, exception.reason.UTF8String);
        }
        return 1;
    }
}
