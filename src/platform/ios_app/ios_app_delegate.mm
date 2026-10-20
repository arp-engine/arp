#import "platform/ios_app/ios_app_delegate.h"

#import "platform/ios_app/ios_metal_view.h"
#include "platform/ios_runtime_contract.h"
#include "rhi/backends/metal/metal_runtime_smoke.h"

@interface ArpIosAppDelegate () {
    arp::platform::IosRuntimeState _runtimeState;
    BOOL _runtimeSmokeExecuted;
}

- (void)updateSafeAreaInsets:(UIEdgeInsets)insets;
- (void)orientationDidChange;

@end

@interface ArpIosViewController : UIViewController
@end

@implementation ArpIosViewController

- (void)loadView
{
    self.view = [[ArpIosMetalView alloc] initWithFrame:UIScreen.mainScreen.bounds];
}

- (void)viewSafeAreaInsetsDidChange
{
    [super viewSafeAreaInsetsDidChange];
    ArpIosAppDelegate* delegate = (ArpIosAppDelegate*)UIApplication.sharedApplication.delegate;
    [delegate updateSafeAreaInsets:self.view.safeAreaInsets];
}

- (void)viewWillTransitionToSize:(CGSize)size
       withTransitionCoordinator:(id<UIViewControllerTransitionCoordinator>)coordinator
{
    [super viewWillTransitionToSize:size withTransitionCoordinator:coordinator];
    ArpIosAppDelegate* delegate = (ArpIosAppDelegate*)UIApplication.sharedApplication.delegate;
    [delegate orientationDidChange];
}

@end

@implementation ArpIosAppDelegate

- (BOOL)application:(UIApplication*)application
    didFinishLaunchingWithOptions:(NSDictionary*)launchOptions
{
    (void)application;
    (void)launchOptions;
    _runtimeState = arp::platform::applyIosLifecycleEvent(
        _runtimeState, arp::platform::IosLifecycleEvent::LayerCreated);

    self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
    self.window.rootViewController = [[ArpIosViewController alloc] init];
    [self.window makeKeyAndVisible];
    return YES;
}

- (void)applicationDidBecomeActive:(UIApplication*)application
{
    (void)application;
    _runtimeState = arp::platform::applyIosLifecycleEvent(
        _runtimeState, arp::platform::IosLifecycleEvent::Active);
    if (_runtimeSmokeExecuted) {
        return;
    }

    _runtimeSmokeExecuted = YES;
    arp::rhi_metal::MetalRhiAppleRuntimeSmokeDesc desc;
    desc.surfaceName = "ArpIosSmokeLayer";
    desc.extent = {320u, 180u};
    const auto report = arp::rhi_metal::runMetalRhiAppleRuntimeSmoke(desc);
    if (report.success) {
        NSLog(@"ARP-IOS-METAL-SMOKE-SUCCEEDED hash=%llu",
            (unsigned long long)report.deterministicRenderOutputHash);
    } else {
        NSLog(@"ARP-IOS-METAL-SMOKE-FAILED %s", report.diagnostics.c_str());
    }
}

- (void)applicationWillResignActive:(UIApplication*)application
{
    (void)application;
    _runtimeState = arp::platform::applyIosLifecycleEvent(
        _runtimeState, arp::platform::IosLifecycleEvent::Inactive);
}

- (void)applicationDidEnterBackground:(UIApplication*)application
{
    (void)application;
    _runtimeState = arp::platform::applyIosLifecycleEvent(
        _runtimeState, arp::platform::IosLifecycleEvent::Background);
}

- (void)applicationWillEnterForeground:(UIApplication*)application
{
    (void)application;
    _runtimeState = arp::platform::applyIosLifecycleEvent(
        _runtimeState, arp::platform::IosLifecycleEvent::Foreground);
}

- (void)applicationDidReceiveMemoryWarning:(UIApplication*)application
{
    (void)application;
    _runtimeState = arp::platform::applyIosLifecycleEvent(
        _runtimeState, arp::platform::IosLifecycleEvent::MemoryWarning);
}

- (void)applicationWillTerminate:(UIApplication*)application
{
    (void)application;
    _runtimeState = arp::platform::applyIosLifecycleEvent(
        _runtimeState, arp::platform::IosLifecycleEvent::LayerLost);
}

- (void)updateSafeAreaInsets:(UIEdgeInsets)insets
{
    _runtimeState = arp::platform::applyIosSafeAreaChange(_runtimeState,
        arp::platform::IosSafeAreaInsets{
            (float)insets.top,
            (float)insets.left,
            (float)insets.bottom,
            (float)insets.right,
        });
}

- (void)orientationDidChange
{
    _runtimeState = arp::platform::applyIosLifecycleEvent(
        _runtimeState, arp::platform::IosLifecycleEvent::OrientationChanged);
}

@end
