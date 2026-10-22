#import "platform/ios/app/ios_metal_view.h"

#import <QuartzCore/CAMetalLayer.h>

@implementation ArpIosMetalView

+ (Class)layerClass
{
    return [CAMetalLayer class];
}

- (instancetype)initWithFrame:(CGRect)frame
{
    self = [super initWithFrame:frame];
    if (self) {
        self.opaque = YES;
        self.backgroundColor = UIColor.blackColor;
        self.contentScaleFactor = UIScreen.mainScreen.nativeScale;
    }
    return self;
}

- (void)layoutSubviews
{
    [super layoutSubviews];
    CAMetalLayer* metalLayer = (CAMetalLayer*)self.layer;
    metalLayer.contentsScale = self.contentScaleFactor;
    metalLayer.drawableSize = CGSizeMake(CGRectGetWidth(self.bounds) * self.contentScaleFactor,
        CGRectGetHeight(self.bounds) * self.contentScaleFactor);
}

@end
