/*
 Copyright (C) 2015 Apple Inc. All Rights Reserved.
 See LICENSE.txt for this sample’s licensing information
 
 Abstract:
 Utility class for creating a quad.
 */

#import "CCVMetalDef.h"
typedef enum MetalOrientation
{
    MetalOrientationUnknown,
    MetalOrientationPortrait,            // Device oriented vertically, home button on the bottom
    MetalOrientationPortraitUpsideDown,  // Device oriented vertically, home button on the top
    MetalOrientationLandscapeLeft,       // Device oriented horizontally, home button on the right
    MetalOrientationLandscapeRight,      // Device oriented horizontally, home button on the left
    MetalOrientationFaceUp,              // Device oriented flat, face up
    MetalOrientationFaceDown,             // Device oriented flat, face down
    MetalOrientation180,             // Device oriented flat, face down
}
METAL_ORIENTATION;

#ifdef MTL_ENABLE
#import <QuartzCore/QuartzCore.h>
#import <Metal/Metal.h>



@interface AAPLQuad : NSObject

// Indices
@property (nonatomic, readwrite) NSUInteger  vertexIndex;
@property (nonatomic, readwrite) NSUInteger  texCoordIndex;
@property (nonatomic, readwrite) NSUInteger  samplerIndex;

// Dimensions
@property (nonatomic, readwrite) CGSize  inputSize;
@property (nonatomic, readwrite) CGSize  outputSize;
@property (nonatomic, readwrite) METAL_ORIENTATION     rotation;
// Designated initializer
- (instancetype) initWithDevice:(id <MTLDevice>)device;

// Encoder
- (void) encode:(id <MTLRenderCommandEncoder>)renderEncoder;

@end
#endif
