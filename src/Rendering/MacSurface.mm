#include "Rendering/MacSurface.h"
#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>
#import <QuartzCore/CATransaction.h>
#import <Metal/Metal.h>
namespace Bazzalt::Runtime {
namespace {struct Surface {NSView* View;CALayer* Previous;CAMetalLayer* Layer;BOOL HadLayer;};}
void ResizeMacSurface(void* handle,std::uint32_t width,std::uint32_t height,float ratio){if(!handle)return;auto* s=static_cast<Surface*>(handle);[CATransaction begin];[CATransaction setDisableActions:YES];s->Layer.frame=s->View.bounds;s->Layer.contentsScale=ratio;s->Layer.drawableSize=CGSizeMake(width,height);[CATransaction commit];}
void* CreateMacSurface(std::uintptr_t native,std::uint32_t width,std::uint32_t height,float ratio){
    if(!native||![NSThread isMainThread])return nullptr;NSView* view=(NSView*)native;
    auto* s=new Surface{[view retain],[view.layer retain],[CAMetalLayer new],view.wantsLayer};
    s->Layer.pixelFormat=MTLPixelFormatBGRA8Unorm;s->Layer.opaque=YES;s->Layer.framebufferOnly=YES;
    id<MTLDevice> device=MTLCreateSystemDefaultDevice();s->Layer.device=device;[device release];
    view.wantsLayer=YES;view.layer=s->Layer;ResizeMacSurface(s,width,height,ratio);return s;
}
void* MacSurfaceLayer(void* handle){return handle?static_cast<Surface*>(handle)->Layer:nullptr;}
void DestroyMacSurface(void* handle){if(!handle)return;auto* s=static_cast<Surface*>(handle);if(s->View.layer==s->Layer){s->View.layer=s->Previous;s->View.wantsLayer=s->HadLayer;}[s->Layer release];[s->Previous release];[s->View release];delete s;}
}
