#import "App.h"
int main(int argc, const char *argv[]) {
    @autoreleasepool {
        NSApplication *application=NSApplication.sharedApplication;
        __attribute__((objc_precise_lifetime)) JNApp *delegate=[JNApp new];
        BOOL smoke=NO;
        for (int i=1;i<argc;++i) {
            if (!strcmp(argv[i],"--portable")) delegate.portable=YES;
            else if (!strcmp(argv[i],"--startup")) delegate.startup=YES;
            else if (!strcmp(argv[i],"--smoke-test")) smoke=YES;
            else if (!strcmp(argv[i],"--archive") && i+1<argc) {
                NSString *path=[NSString stringWithUTF8String:argv[++i]];
                if (!path.isAbsolutePath) { fprintf(stderr,"--archive requires an absolute path\n"); return 2; }
                delegate.initialDirectory=[NSURL fileURLWithPath:path isDirectory:YES];
            } else if (strncmp(argv[i],"-psn_",5)) { fprintf(stderr,"Usage: Jotilla [--portable] [--startup] [--archive /path/to/notes]\n"); return 2; }
        }
        if (smoke && (!delegate.portable || !delegate.initialDirectory)) { fprintf(stderr,"Smoke test requires --portable --archive PATH\n"); return 2; }
        if (smoke) {
            delegate.startup=YES;
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW,2*NSEC_PER_SEC),dispatch_get_main_queue(),^{ [application terminate:nil]; });
        }
        application.delegate=delegate; [application run];
        (void)delegate;
    }
    return 0;
}
