#include "PosixIntegrity.h"
#include "game_logo.h"
#include "Runtime/Engine.h"
#include "Runtime/InputAccess.h"
#include "Runtime/ApplicationEventsAccess.h"
#include "Runtime/ApplicationAccess.h"
#include <SDL3/SDL.h>
#ifndef __APPLE__
#include "WaylandPresenter.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#else
#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#endif
#include <chrono>
#include <thread>
#include <iostream>
#include <cstdlib>

using namespace Bazzalt::Game;
namespace {
struct TemporaryContent {
    std::filesystem::path Path;int Session=-1;FileLeases* Leases=nullptr;
    void Create(){
        #ifdef __APPLE__
        const char* home=std::getenv("HOME");Require(home,"No user cache directory");auto root=std::filesystem::path(home)/"Library/Caches";
#else
        const char* xdg=std::getenv("XDG_CACHE_HOME");const char* home=std::getenv("HOME");Require(xdg||home,"No user cache directory");auto root=xdg?std::filesystem::path(xdg):std::filesystem::path(home)/".cache";
#endif
        root/="BazzaltGames";std::filesystem::create_directories(root);Require(!std::filesystem::is_symlink(root),"Linked game cache");chmod(root.c_str(),0700);
        std::string pattern=(root/"session-XXXXXX").string();auto* directory=mkdtemp(pattern.data());Require(directory,"Could not create private game cache");Path=directory;Session=open((Path/".bazzalt-session").c_str(),O_CREAT|O_EXCL|O_RDWR|O_CLOEXEC|O_NOFOLLOW,0600);Require(Session>=0&&flock(Session,LOCK_EX|LOCK_NB)==0,"Could not lock game session");
    }
    ~TemporaryContent(){if(Leases)Leases->clear();if(Session>=0)close(Session);if(!Path.empty()){std::error_code error;std::filesystem::remove_all(Path,error);}}
};
#ifdef __APPLE__
#include "MacCover.h"
#else
struct Cover {
    SDL_Window* WaylandWindow=nullptr;std::unique_ptr<WaylandPresenter> Presenter;
    Display* DisplayHandle=nullptr;Window Handle=0;int Size=144;bool Brand=true;
    void Create(SDL_Window* window,bool brand,int size){
        if(std::string(SDL_GetCurrentVideoDriver())=="wayland"){WaylandWindow=window;Presenter=std::make_unique<WaylandPresenter>(window);Size=size;Brand=brand;return;}
        auto properties=SDL_GetWindowProperties(window);DisplayHandle=static_cast<Display*>(SDL_GetPointerProperty(properties,SDL_PROP_WINDOW_X11_DISPLAY_POINTER,nullptr));auto owner=Window(SDL_GetNumberProperty(properties,SDL_PROP_WINDOW_X11_WINDOW_NUMBER,0));Require(DisplayHandle&&owner,"Linux presentation requires X11/XWayland");Size=size;Brand=brand;int width,height;SDL_GetWindowSizeInPixels(window,&width,&height);Handle=XCreateSimpleWindow(DisplayHandle,owner,0,0,width,height,0,0,0);XMapRaised(DisplayHandle,Handle);Paint(width,height);
    }
    void Paint(int width,int height){if(WaylandWindow){SDL_GetWindowSizeInPixels(WaylandWindow,&width,&height);std::vector<unsigned char> pixels(std::size_t(width)*height*4);auto* target=SDL_CreateSurfaceFrom(width,height,SDL_PIXELFORMAT_BGRA32,pixels.data(),width*4);Require(target,SDL_GetError());SDL_FillSurfaceRect(target,nullptr,SDL_MapSurfaceRGB(target,0,0,0));if(Brand){auto* logo=SDL_CreateSurfaceFrom(GameIntroWidth,GameIntroHeight,SDL_PIXELFORMAT_BGRA32,const_cast<unsigned char*>(GameIntroPixels),GameIntroWidth*4);Require(logo,SDL_GetError());const int h=std::min(Size+64,std::max(32,height/2)),w=h*GameIntroWidth/GameIntroHeight;SDL_Rect rect{(width-w)/2,(height-h)/2,w,h};const bool blitted=SDL_BlitSurfaceScaled(logo,nullptr,target,&rect,SDL_SCALEMODE_LINEAR);SDL_DestroySurface(logo);if(!blitted){SDL_DestroySurface(target);throw std::runtime_error(SDL_GetError());}}SDL_DestroySurface(target);Presenter->Present(pixels.data(),width,height,false);return;}if(!Handle)return;XResizeWindow(DisplayHandle,Handle,width,height);GC gc=XCreateGC(DisplayHandle,Handle,0,nullptr);XSetForeground(DisplayHandle,gc,0);XFillRectangle(DisplayHandle,Handle,gc,0,0,width,height);if(Brand){const int size=std::min(Size,std::max(32,(height-80)/2)),w=size*GameLogoWidth/GameLogoHeight,top=(height-size-48)/2;auto* data=static_cast<char*>(calloc(std::size_t(w)*size,4));Require(data,"Intro image allocation failed");auto* image=XCreateImage(DisplayHandle,DefaultVisual(DisplayHandle,DefaultScreen(DisplayHandle)),DefaultDepth(DisplayHandle,DefaultScreen(DisplayHandle)),ZPixmap,0,data,w,size,32,0);Require(image,"Intro image unavailable");for(int y=0;y<size;++y)for(int x=0;x<w;++x){const auto at=((y*GameLogoHeight/size)*GameLogoWidth+x*GameLogoWidth/w)*4;XPutPixel(image,x,y,(static_cast<unsigned long>(GameLogoPixels[at+2])<<16)|(static_cast<unsigned long>(GameLogoPixels[at+1])<<8)|GameLogoPixels[at]);}XPutImage(DisplayHandle,Handle,gc,image,0,0,(width-w)/2,top,w,size);XDestroyImage(image);XSetForeground(DisplayHandle,gc,0xffffff);const char* text="Powered by Bazzalt";XDrawString(DisplayHandle,Handle,gc,(width-int(std::strlen(text))*6)/2,top+size+32,text,std::strlen(text));}XFreeGC(DisplayHandle,gc);XFlush(DisplayHandle);}
    void Finish(){WaylandWindow=nullptr;if(Handle){XDestroyWindow(DisplayHandle,Handle);XFlush(DisplayHandle);Handle=0;}}
    ~Cover(){Finish();}
};
#endif
}
extern "C" __attribute__((visibility("default"))) int BazzaltRunLinuxPlayer(int argc,char** argv){
    bool verify=false,smoke=false;for(int i=1;i<argc;++i){verify|=std::string(argv[i])=="--verify-package";smoke|=std::string(argv[i])=="--smoke-test";}
    SDL_Window* window=nullptr;
    try{
        umask(0077);
#ifdef __APPLE__
        KeychainProvisionAllowed=!verify;const auto executable=ExecutablePath();
#else
        std::array<char,32768> executableBytes{};const auto count=readlink("/proc/self/exe",executableBytes.data(),executableBytes.size()-1);Require(count>0&&std::size_t(count)<executableBytes.size()-1,"Could not locate game executable");const std::filesystem::path executable(std::string(executableBytes.data(),count));
#endif
        auto verified=VerifyInventory(executable);auto tree=ryml::parse_in_arena(ryml::csubstr(verified.Configuration.data(),verified.Configuration.size()));auto config=tree.rootref();auto folder=executable.parent_path();TemporaryContent content;auto data=folder/"Data";
        if(Text(config["format"])=="pak"){content.Create();std::vector<std::string> packages;for(auto name:config["packages"].children())packages.push_back(Text(name));Require(!packages.empty()&&packages.size()<=65536,"Invalid package list");Extract(folder,content.Path,packages,verified.PackageKey);content.Leases=&verified.ExtractedLeases;VerifyExtracted(verified,content.Path);data=content.Path;}else Require(Text(config["format"])=="loose","Unsupported package format");
        Bazzalt::Runtime::Engine engine;const auto check=[&](bool success){if(!success)throw std::runtime_error(engine.GetLastError());};check(engine.LoadProject(data/"Game.bproject",false));auto startup=engine.GetProject().StartupScene;Require(!startup.empty()&&std::filesystem::is_regular_file(data/SafePath(startup.generic_string())),"Missing startup scene");if(verify)return 0;
        check(engine.PrepareApplication());Bazzalt::Runtime::ApplicationEventsAccess::Emit({Bazzalt::ApplicationEventType::OnBeforeWindowCreate});Bazzalt::Runtime::ApplicationEventsAccess::ThrowIfFailed();
        #ifndef __APPLE__
        const auto windowSettings=config["window"];const auto displayServer=windowSettings.has_child("display_server")?Text(windowSettings["display_server"]):"automatic";Require(displayServer=="automatic"||displayServer=="x11"||displayServer=="wayland","Invalid Linux display server");const bool wantWayland=displayServer=="wayland"||(displayServer=="automatic"&&std::getenv("WAYLAND_DISPLAY")&&Text(windowSettings["backend"])!="opengl");SDL_SetHint(SDL_HINT_VIDEO_DRIVER,wantWayland?"wayland":"x11");
#endif
        bool videoReady=SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS);
#ifndef __APPLE__
        if(!videoReady&&displayServer=="automatic"&&std::getenv("DISPLAY")){SDL_Quit();SDL_SetHint(SDL_HINT_VIDEO_DRIVER,"x11");videoReady=SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS);}
#endif
        Require(videoReady,SDL_GetError());auto settings=config["window"];int width=std::stoi(Text(settings["width"])),height=std::stoi(Text(settings["height"]));Require(width>=320&&height>=200&&width<=16384&&height<=16384,"Invalid window size");const auto mode=Text(settings["mode"]);Require(mode=="windowed"||mode=="maximized"||mode=="borderless"||mode=="fullscreen","Invalid window mode");SDL_WindowFlags flags=SDL_WINDOW_HIDDEN|SDL_WINDOW_HIGH_PIXEL_DENSITY;
#ifdef __APPLE__
        flags|=SDL_WINDOW_METAL|SDL_WINDOW_HIGH_PIXEL_DENSITY;
#endif
        if(Text(settings["resizable"])=="true")flags|=SDL_WINDOW_RESIZABLE;if(mode=="borderless")flags|=SDL_WINDOW_BORDERLESS;window=SDL_CreateWindow(Text(settings["title"]).c_str(),width,height,flags);Require(window,SDL_GetError());SDL_SetWindowPosition(window,SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED);if(mode=="maximized")SDL_MaximizeWindow(window);if(mode=="fullscreen")Require(SDL_SetWindowFullscreen(window,true),SDL_GetError());
        auto intro=config["intro"];const bool brand=Text(intro["enabled"])=="true",allowSkip=Text(intro["allow_skip"])=="true";int duration=std::stoi(Text(intro["duration_ms"])),logo=std::stoi(Text(intro["logo_size"]));Require(duration>=0&&duration<=30000&&logo>=48&&logo<=512,"Invalid intro settings");Cover cover;cover.Create(window,brand,logo);Require(SDL_ShowWindow(window),SDL_GetError());cover.Paint(width,height);auto started=std::chrono::steady_clock::now();
#ifndef __APPLE__
        const bool wayland=std::string(SDL_GetCurrentVideoDriver())=="wayland";
#else
        const bool wayland=false;
#endif
        auto requestedBackend=Text(settings["backend"]);if(wayland){Require(requestedBackend=="automatic"||requestedBackend=="vulkan","Native Wayland presentation requires the Vulkan backend");requestedBackend="vulkan";}
        Require(engine.ConfigureRenderingBackend(requestedBackend),"Unsupported rendering backend");check(engine.Init(false,false));check(engine.LoadScene(data/startup));SDL_GetWindowSizeInPixels(window,&width,&height);
#ifdef __APPLE__
        auto* cocoa=(NSWindow*)SDL_GetPointerProperty(SDL_GetWindowProperties(window),SDL_PROP_WINDOW_COCOA_WINDOW_POINTER,nullptr);const auto native=std::uintptr_t(cocoa.contentView);
#else
        const auto native=SDL_GetNumberProperty(SDL_GetWindowProperties(window),SDL_PROP_WINDOW_X11_WINDOW_NUMBER,0);
#endif
        Require(wayland?engine.CreateCapturedViewport(1,false,width,height):(native&&engine.CreateEditorViewport(1,std::uintptr_t(native),false,width,height)),"Could not create game surface");
        std::uint64_t captureSequence=0;
        const auto present=[&](){if(!wayland)return;auto frame=engine.GetCapturedViewportFrame(1,captureSequence);if(!frame)return;captureSequence=frame->Sequence;
#ifndef __APPLE__
            cover.Presenter->Present(frame->Pixels.data(),frame->Width,frame->Height,true);
#endif
        };
        using Events=Bazzalt::Runtime::ApplicationEventsAccess;using Input=Bazzalt::Runtime::InputAccess;Events::Emit({Bazzalt::ApplicationEventType::OnPostWindowCreate});Bazzalt::ApplicationEvent resize{Bazzalt::ApplicationEventType::OnWindowResize};resize.Width=width;resize.Height=height;Events::Emit(resize);bool focused=SDL_GetWindowFlags(window)&SDL_WINDOW_INPUT_FOCUS;Bazzalt::ApplicationEvent focus{Bazzalt::ApplicationEventType::OnWindowFocusChanged};focus.IsFocused=focused;Events::Emit(focus);Events::ThrowIfFailed();bool playing=false,skip=false;int frames=0;
        while(!engine.ShouldClose()){
            const auto start=std::chrono::steady_clock::now();SDL_Event event;while(SDL_PollEvent(&event)){
                if(event.type==SDL_EVENT_QUIT||event.type==SDL_EVENT_WINDOW_CLOSE_REQUESTED){Events::Emit({Bazzalt::ApplicationEventType::OnWindowCloseRequested});engine.RequestClose();}
                else if(event.type==SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED){SDL_GetWindowSizeInPixels(window,&width,&height);if(width>0&&height>0){engine.ResizeEditorViewport(1,width,height);cover.Paint(width,height);resize.Width=width;resize.Height=height;Events::Emit(resize);}}
                else if(event.type==SDL_EVENT_WINDOW_FOCUS_GAINED||event.type==SDL_EVENT_WINDOW_FOCUS_LOST){focused=event.type==SDL_EVENT_WINDOW_FOCUS_GAINED;Input::SetActive(focused&&playing);focus.IsFocused=focused;Events::Emit(focus);}
                else if(event.type==SDL_EVENT_KEY_DOWN||event.type==SDL_EVENT_KEY_UP){Input::KeyEvent(int(event.key.scancode),event.type==SDL_EVENT_KEY_DOWN,event.key.repeat);if(allowSkip&&event.type==SDL_EVENT_KEY_DOWN)skip=true;}
                else if(event.type==SDL_EVENT_MOUSE_BUTTON_DOWN||event.type==SDL_EVENT_MOUSE_BUTTON_UP){Input::ButtonEvent(event.button.button,event.type==SDL_EVENT_MOUSE_BUTTON_DOWN);if(allowSkip)skip=true;}
                else if(event.type==SDL_EVENT_MOUSE_MOTION){int logicalWidth,logicalHeight;SDL_GetWindowSize(window,&logicalWidth,&logicalHeight);const float sx=float(width)/std::max(1,logicalWidth),sy=float(height)/std::max(1,logicalHeight);Input::MotionEvent(event.motion.x*sx,event.motion.y*sy,event.motion.xrel*sx,event.motion.yrel*sy);}
                else if(event.type==SDL_EVENT_MOUSE_WHEEL){const float direction=event.wheel.direction==SDL_MOUSEWHEEL_FLIPPED?-1.f:1.f;Input::ScrollEvent(event.wheel.x*direction,event.wheel.y*direction);}
                else if(event.type==SDL_EVENT_TEXT_INPUT)Input::TextEvent(event.text.text);
            }
            if(engine.ShouldClose())break;const bool minimized=SDL_GetWindowFlags(window)&SDL_WINDOW_MINIMIZED;
            if(!playing&&!minimized){engine.RenderEditorFrame();const bool ready=engine.WaitForViewportFrame(1);const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(start-started).count();Require(ready||elapsed<60000,"Game renderer startup timed out");if(ready&&(smoke||!brand||skip||elapsed>=duration)){check(engine.StartScripts());engine.Update();Require(engine.WaitForViewportFrame(1),"First gameplay frame did not finish");cover.Finish();present();Input::SetActive(focused&&!smoke);SDL_StartTextInput(window);playing=true;}}
            else if(playing&&(smoke||focused||Bazzalt::Application::GetRunInBackground())){engine.Update(!minimized);if(!minimized)present();if(smoke&&++frames>=3)break;}else engine.ResetFrameClock();
            int rate=Bazzalt::Runtime::ApplicationAccess::GetEffectiveFrameRate();if(rate>0)std::this_thread::sleep_until(start+std::chrono::duration<double>(1.0/rate));else if(minimized)std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        Events::ThrowIfFailed();engine.BeginShutdown();engine.Shutdown(true);cover.Finish();
#ifndef __APPLE__
        cover.Presenter.reset();
#endif
        SDL_DestroyWindow(window);window=nullptr;engine.EndApplication();Events::ThrowIfFailed();SDL_Quit();return Bazzalt::Application::GetExitCode();
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';if(!verify&&!smoke)SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Bazzalt game error",error.what(),window);if(window)SDL_DestroyWindow(window);SDL_Quit();return 1;}
}
