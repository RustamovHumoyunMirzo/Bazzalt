#pragma once
#import <Cocoa/Cocoa.h>
struct Cover {
    NSView* View=nil;NSImageView* Image=nil;NSTextField* Label=nil;
    void Create(SDL_Window* window,bool brand,int size){
        auto* owner=(NSWindow*)SDL_GetPointerProperty(SDL_GetWindowProperties(window),SDL_PROP_WINDOW_COCOA_WINDOW_POINTER,nullptr);Require(owner,"No Cocoa game window");
        View=[[NSView alloc] initWithFrame:owner.contentView.bounds];View.wantsLayer=YES;View.layer.backgroundColor=NSColor.blackColor.CGColor;View.autoresizingMask=NSViewWidthSizable|NSViewHeightSizable;[owner.contentView addSubview:View];
        if(brand){auto* bitmap=[[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nullptr pixelsWide:GameLogoWidth pixelsHigh:GameLogoHeight bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:GameLogoWidth*4 bitsPerPixel:32];
            for(std::size_t i=0;i<GameLogoWidth*GameLogoHeight;++i){auto* to=bitmap.bitmapData+i*4;to[0]=GameLogoPixels[i*4+2];to[1]=GameLogoPixels[i*4+1];to[2]=GameLogoPixels[i*4];to[3]=GameLogoPixels[i*4+3];}
            auto* image=[[NSImage alloc] initWithSize:NSMakeSize(GameLogoWidth,GameLogoHeight)];[image addRepresentation:bitmap];[bitmap release];Image=[[NSImageView alloc] initWithFrame:NSMakeRect(0,0,size*GameLogoWidth/GameLogoHeight,size)];Image.image=image;[image release];[View addSubview:Image];
            Label=[[NSTextField labelWithString:@"Powered by Bazzalt"] retain];Label.textColor=NSColor.whiteColor;Label.alignment=NSTextAlignmentCenter;[View addSubview:Label];
        }Paint(0,0);
    }
    void Paint(int,int){if(!View)return;auto bounds=View.bounds;if(Image){auto frame=Image.frame;frame.origin=NSMakePoint((bounds.size.width-frame.size.width)*.5,(bounds.size.height-frame.size.height)*.5+20);Image.frame=frame;Label.frame=NSMakeRect(0,frame.origin.y-40,bounds.size.width,24);}[View displayIfNeeded];}
    void Finish(){if(View){[View removeFromSuperview];[View release];View=nil;[Image release];Image=nil;[Label release];Label=nil;}}
    ~Cover(){Finish();}
};
