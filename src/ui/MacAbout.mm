// Show the macOS standard "About AppName" panel. The system reads
// Contents/Resources/Credits.rtf automatically and renders the body
// of the panel from it; the icon, app name, and version come from
// the bundle's Info.plist keys (CFBundleIconFile / CFBundleName /
// CFBundleShortVersionString / CFBundleVersion).

#if defined(__APPLE__)
#import <AppKit/AppKit.h>

namespace mmp {
void showStandardAboutPanel() {
    [[NSApplication sharedApplication] orderFrontStandardAboutPanel:nil];
    // Bring the panel to the front (orderFrontStandardAboutPanel doesn't
    // always activate the app on its own when invoked from a menu item).
    [[NSApplication sharedApplication] activateIgnoringOtherApps:YES];
}
} // namespace mmp
#endif
