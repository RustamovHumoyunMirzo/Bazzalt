#pragma once
#include <wayland-client.h>
#include <sys/mman.h>
#include <unistd.h>
#include <array>
#include <cstring>
#include <limits>

namespace Bazzalt::Game {
// SDL owns the shell/window and event dispatch. Present only wl_shm buffers;
// never create a second OpenGL/Vulkan renderer beside Filament.
class WaylandPresenter {
    struct Buffer {
        wl_buffer* Handle=nullptr;void* Pixels=MAP_FAILED;std::size_t Bytes=0;
        int Width=0,Height=0;bool Busy=false;
        void Reset(){if(Handle)wl_buffer_destroy(Handle);if(Pixels!=MAP_FAILED)munmap(Pixels,Bytes);Handle=nullptr;Pixels=MAP_FAILED;Bytes=0;Busy=false;}
        ~Buffer(){Reset();}
    };
    wl_display* Display=nullptr;wl_surface* Surface=nullptr;wl_shm* SharedMemory=nullptr;
    std::array<Buffer,3> Buffers;
    static void Global(void* data,wl_registry* registry,std::uint32_t name,const char* interface,std::uint32_t){
        auto& self=*static_cast<WaylandPresenter*>(data);
        if(std::strcmp(interface,"wl_shm")==0)self.SharedMemory=static_cast<wl_shm*>(wl_registry_bind(registry,name,&wl_shm_interface,1));
    }
    static void Removed(void*,wl_registry*,std::uint32_t){}
    static void Released(void* data,wl_buffer*){static_cast<Buffer*>(data)->Busy=false;}
public:
    explicit WaylandPresenter(SDL_Window* window){
        const auto props=SDL_GetWindowProperties(window);
        Display=static_cast<wl_display*>(SDL_GetPointerProperty(props,SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER,nullptr));
        Surface=static_cast<wl_surface*>(SDL_GetPointerProperty(props,SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER,nullptr));
        Require(Display&&Surface,"Missing native Wayland surface");
        auto* registry=wl_display_get_registry(Display);Require(registry,"Missing Wayland registry");
        static const wl_registry_listener listener{Global,Removed};wl_registry_add_listener(registry,&listener,this);
        const int result=wl_display_roundtrip(Display);wl_registry_destroy(registry);
        if(result<0||!SharedMemory){if(SharedMemory)wl_shm_destroy(SharedMemory);SharedMemory=nullptr;throw std::runtime_error("Wayland shared-memory presentation is unavailable");}
    }
    ~WaylandPresenter(){for(auto& buffer:Buffers)buffer.Reset();if(SharedMemory)wl_shm_destroy(SharedMemory);}
    bool Present(const unsigned char* pixels,int width,int height,bool bottomOriginRGBA){
        Require(width>0&&height>0&&width<=16384&&height<=16384,"Invalid Wayland frame size");
        Buffer* available=nullptr;for(auto& buffer:Buffers)if(!buffer.Busy){available=&buffer;break;}
        if(!available)return false; // Compositor backpressure: never overwrite an in-use buffer.
        auto& buffer=*available;
        if(buffer.Width!=width||buffer.Height!=height||!buffer.Handle){
            buffer.Reset();const auto bytes=std::size_t(width)*height*4;
            Require(bytes<=std::size_t(std::numeric_limits<int>::max()),"Wayland frame allocation exceeds protocol limits");
            char name[]="/tmp/bazzalt-frame-XXXXXX";const int fd=mkstemp(name);Require(fd>=0,"Could not allocate a Wayland frame");unlink(name);
            if(ftruncate(fd,static_cast<off_t>(bytes))<0){close(fd);throw std::runtime_error("Could not size a Wayland frame");}
            buffer.Pixels=mmap(nullptr,bytes,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);buffer.Bytes=bytes;
            if(buffer.Pixels==MAP_FAILED){close(fd);throw std::runtime_error("Could not map a Wayland frame");}
            auto* pool=wl_shm_create_pool(SharedMemory,fd,static_cast<int>(bytes));close(fd);Require(pool,"Could not create a Wayland pool");
            buffer.Handle=wl_shm_pool_create_buffer(pool,0,width,height,width*4,WL_SHM_FORMAT_ARGB8888);wl_shm_pool_destroy(pool);Require(buffer.Handle,"Could not create a Wayland buffer");
            static const wl_buffer_listener listener{Released};wl_buffer_add_listener(buffer.Handle,&listener,&buffer);buffer.Width=width;buffer.Height=height;
        }
        auto* output=static_cast<unsigned char*>(buffer.Pixels);
        if(bottomOriginRGBA){
            for(int y=0;y<height;++y){const auto* input=pixels+std::size_t(height-1-y)*width*4;auto* row=output+std::size_t(y)*width*4;
                for(int x=0;x<width;++x){row[x*4]=input[x*4+2];row[x*4+1]=input[x*4+1];row[x*4+2]=input[x*4];row[x*4+3]=255;}}
        }else std::memcpy(output,pixels,buffer.Bytes);
        buffer.Busy=true;wl_surface_attach(Surface,buffer.Handle,0,0);
        if(wl_surface_get_version(Surface)>=4)wl_surface_damage_buffer(Surface,0,0,width,height);else wl_surface_damage(Surface,0,0,INT32_MAX,INT32_MAX);
        wl_surface_commit(Surface);wl_display_flush(Display);return true;
    }
};
}
