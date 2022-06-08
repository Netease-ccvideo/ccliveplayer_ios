//
//  MLiveCCRenderVTB.h
//  MLiveCCPlayer
//
//  Created by jlubobo on 2017/5/26.
//  Copyright © 2017年 cc. All rights reserved.
//

#import <Foundation/Foundation.h>
#import "MLiveCCRender.h"

@interface MLiveCCRenderVTB : NSObject<MLiveCCRender>

-(id)initWithTextureCache:(CVOpenGLESTextureCacheRef) textureCache;

@end
