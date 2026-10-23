#include "Package.h"
#include "Integrity.h"
#include "StartupCover.h"
#include "Runtime/Engine.h"
#include "Runtime/InputAccess.h"
#include <shellapi.h>
#include <sddl.h>
#include <objbase.h>
#include <chrono>
#include <thread>
#include <iostream>

using namespace Bazzalt::Game;
namespace {
Bazzalt::Runtime::Engine* Engine=nullptr;
struct LibraryDirectory {DLL_DIRECTORY_COOKIE Value{};~LibraryDirectory(){if(Value)RemoveDllDirectory(Value);}};
int PhysicalKey(WPARAM key,LPARAM flags){
    const int scan=int((flags>>16)&255);
    if(key==VK_PAUSE)return 72;if(key==VK_SNAPSHOT)return 70;
    if(flags&(1<<24)){switch(scan){case 28:return 88;case 29:return 228;case 53:return 84;case 56:return 230;case 71:return 74;case 72:return 82;case 73:return 75;case 75:return 80;case 77:return 79;case 79:return 77;case 80:return 81;case 81:return 78;case 82:return 73;case 83:return 76;case 91:return 227;case 92:return 231;case 93:return 101;default:return 0;}}
    // Windows set-1 scan codes -> public SDL physical key identifiers, not
    // virtual keys: movement remains physical on non-QWERTY keyboard layouts.
    static constexpr int codes[]={0,41,30,31,32,33,34,35,36,37,38,39,45,46,42,43,20,26,8,21,23,28,24,12,18,19,47,48,40,224,4,22,7,9,10,11,13,14,15,51,52,53,225,49,29,27,6,25,5,17,16,54,55,56,229,85,226,44,57,58,59,60,61,62,63,64,65,66,67,83,71,95,96,97,86,92,93,94,87,89,90,91,98,99,0,0,100,68,69};
    return scan>=0&&scan<int(std::size(codes))?codes[scan]:0;
}
LRESULT CALLBACK WindowProcedure(HWND window,UINT message,WPARAM w,LPARAM l){
    using Bazzalt::Runtime::InputAccess;
    static POINT previous{};static bool hasPosition=false;static wchar_t surrogate=0;
    switch(message){
    case WM_ERASEBKGND:if(!Startup::Ready){RECT bounds{};GetClientRect(window,&bounds);FillRect(reinterpret_cast<HDC>(w),&bounds,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));}return 1;
    case WM_PAINT:{PAINTSTRUCT paint;auto dc=BeginPaint(window,&paint);if(!Startup::Ready){RECT bounds{};GetClientRect(window,&bounds);FillRect(dc,&bounds,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));}EndPaint(window,&paint);return 0;}
    case WM_MOVE:Startup::Position(window);return 0;
    case WM_CLOSE:DestroyWindow(window);return 0;
    case WM_DESTROY:PostQuitMessage(0);return 0;
    case WM_SIZE:Startup::Position(window);if(Engine&&Engine->IsInitialized()&&w!=SIZE_MINIMIZED)Engine->ResizeEditorViewport(1,LOWORD(l),HIWORD(l));return 0;
    case WM_SETFOCUS:InputAccess::SetActive(true);hasPosition=false;return 0;
    case WM_KILLFOCUS:InputAccess::SetActive(false);hasPosition=false;surrogate=0;return 0;
    case WM_KEYDOWN:case WM_SYSKEYDOWN:case WM_KEYUP:case WM_SYSKEYUP:
        if(Startup::Cover&&Startup::AllowSkip&&(message==WM_KEYDOWN||message==WM_SYSKEYDOWN))Startup::Skip=true;
        InputAccess::KeyEvent(PhysicalKey(w,l),message==WM_KEYDOWN||message==WM_SYSKEYDOWN,(message==WM_KEYDOWN||message==WM_SYSKEYDOWN)&&bool(l&(1u<<30)));break;
    case WM_LBUTTONDOWN:case WM_MBUTTONDOWN:case WM_RBUTTONDOWN:SetCapture(window);InputAccess::ButtonEvent(message==WM_LBUTTONDOWN?1:message==WM_MBUTTONDOWN?2:3,true);return 0;
    case WM_LBUTTONUP:case WM_MBUTTONUP:case WM_RBUTTONUP:InputAccess::ButtonEvent(message==WM_LBUTTONUP?1:message==WM_MBUTTONUP?2:3,false);if(!(w&(MK_LBUTTON|MK_MBUTTON|MK_RBUTTON)))ReleaseCapture();return 0;
    case WM_XBUTTONDOWN:case WM_XBUTTONUP:InputAccess::ButtonEvent(HIWORD(w)==XBUTTON1?4:5,message==WM_XBUTTONDOWN);return TRUE;
    case WM_MOUSEMOVE:{POINT next{short(LOWORD(l)),short(HIWORD(l))};InputAccess::MotionEvent(float(next.x),float(next.y),hasPosition?float(next.x-previous.x):0,hasPosition?float(next.y-previous.y):0);previous=next;hasPosition=true;return 0;}
    case WM_MOUSEWHEEL:InputAccess::ScrollEvent(0,float(short(HIWORD(w)))/WHEEL_DELTA);return 0;
    case WM_MOUSEHWHEEL:InputAccess::ScrollEvent(float(short(HIWORD(w)))/WHEEL_DELTA,0);return 0;
    case WM_CHAR:{wchar_t text[2]{wchar_t(w),0};int count=1;if(w>=0xd800&&w<=0xdbff){surrogate=wchar_t(w);return 0;}if(surrogate){text[0]=surrogate;text[1]=wchar_t(w);count=2;surrogate=0;}char utf8[8]{};auto n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text,count,utf8,8,nullptr,nullptr);if(n>0)InputAccess::TextEvent(std::string(utf8,n));return 0;}
    }
    return DefWindowProcW(window,message,w,l);
}
// Restrict plaintext extraction to the launching account and SYSTEM. A random
// directory name alone is not an access-control boundary on shared machines.
inline void CreatePrivateContentDirectory(const std::filesystem::path& path){
    HANDLE token=nullptr;Require(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)!=0,"Could not inspect player identity");
    DWORD bytes=0;GetTokenInformation(token,TokenUser,nullptr,0,&bytes);std::vector<unsigned char> storage(bytes);
    const bool ok=GetTokenInformation(token,TokenUser,storage.data(),bytes,&bytes)!=0;CloseHandle(token);Require(ok,"Could not inspect player identity");
    LPWSTR sid=nullptr;Require(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(storage.data())->User.Sid,&sid)!=0,"Could not encode player identity");
    std::wstring sddl=L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;";sddl+=sid;sddl+=L")";LocalFree(sid);
    PSECURITY_DESCRIPTOR descriptor=nullptr;Require(ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),SDDL_REVISION_1,&descriptor,nullptr)!=0,"Could not secure game cache");
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES),descriptor,FALSE};const bool created=CreateDirectoryW(path.c_str(),&attributes)!=0;LocalFree(descriptor);Require(created,"Could not create private game cache");
}
struct TemporaryContent {
    std::filesystem::path Path;
    FileLeases* Leases=nullptr;
    HANDLE Session=INVALID_HANDLE_VALUE;
    void Lock(){
        Session=CreateFileW((Path/".bazzalt-session").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_HIDDEN,nullptr);
        Require(Session!=INVALID_HANDLE_VALUE,"Could not lock game content session");DWORD written=0;
        Require(WriteFile(Session,"BZSESSION002",12,&written,nullptr)&&written==12&&FlushFileBuffers(Session),"Could not mark game content session");
    }
    static void ReclaimAbandoned(){
        std::error_code error;const auto temp=std::filesystem::temp_directory_path(error);if(error)return;
        for(std::filesystem::directory_iterator it(temp,std::filesystem::directory_options::skip_permission_denied,error),end;it!=end&&!error;it.increment(error)){
            const auto path=it->path();const auto name=path.filename().wstring();
            if(name.size()!=50||name.rfind(L"BazzaltGame-{",0)!=0||name.back()!=L'}')continue;
            GUID id{};if(FAILED(CLSIDFromString(name.c_str()+12,&id)))continue;
            const auto attributes=GetFileAttributesW(path.c_str());if(attributes==INVALID_FILE_ATTRIBUTES||(attributes&FILE_ATTRIBUTE_REPARSE_POINT)||!(attributes&FILE_ATTRIBUTE_DIRECTORY))continue;
            HANDLE session=CreateFileW((path/".bazzalt-session").c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);if(session==INVALID_HANDLE_VALUE)continue;
            char marker[12]{};DWORD count=0;const bool owned=ReadFile(session,marker,12,&count,nullptr)&&count==12&&std::string(marker,12)=="BZSESSION002";
            // An active session keeps its marker exclusively locked. Refuse
            // trees containing reparse points even for an abandoned session.
            bool safe=owned;std::error_code inspect;
            if(safe)for(std::filesystem::recursive_directory_iterator files(path,std::filesystem::directory_options::none,inspect),last;files!=last&&!inspect;files.increment(inspect)){
                const auto flags=GetFileAttributesW(files->path().c_str());if(flags==INVALID_FILE_ATTRIBUTES||(flags&FILE_ATTRIBUTE_REPARSE_POINT)){safe=false;break;}
            }
            CloseHandle(session);if(safe&&!inspect){std::error_code ignored;std::filesystem::remove_all(path,ignored);}
        }
    }
    ~TemporaryContent(){if(Leases)Leases->clear();if(Session!=INVALID_HANDLE_VALUE)CloseHandle(Session);if(!Path.empty()){std::error_code error;std::filesystem::remove_all(Path,error);}}
};
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){
    bool verify=false,smoke=false;int argumentCount=0;auto arguments=CommandLineToArgvW(GetCommandLineW(),&argumentCount);
    if(arguments){for(int i=1;i<argumentCount;++i){if(std::wstring(arguments[i])==L"--verify-package")verify=true;if(std::wstring(arguments[i])==L"--smoke-test")smoke=true;}LocalFree(arguments);}
    HWND window=nullptr;
    try{
        Require(SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32|LOAD_LIBRARY_SEARCH_USER_DIRS)!=0,"Could not restrict library search paths");
        SetProcessDPIAware();std::vector<wchar_t> path(32768);auto count=GetModuleFileNameW(nullptr,path.data(),DWORD(path.size()));Require(count>0&&count<path.size(),"Could not locate player executable");
        auto executable=std::filesystem::canonical(std::filesystem::path(std::wstring(path.data(),count)));auto folder=executable.parent_path();
        LibraryDirectory libraries;libraries.Value=AddDllDirectory(folder.c_str());Require(libraries.Value!=nullptr,"Could not register the verified runtime directory");
        auto verified=VerifyInventory(executable);
        auto tree=ryml::parse_in_arena(ryml::csubstr(verified.Configuration.data(),verified.Configuration.size()));auto config=tree.rootref();Require(Text(config["format_version"])=="3","Unsupported game manifest");
        Require(Text(config["encryption"])=="none"||Text(config["encryption"])=="aes-256-gcm","Unsupported game encryption");
        Require(Text(config["format"])=="pak"||Text(config["format"])=="loose","Unsupported content format");
        Require((Text(config["encryption"])=="aes-256-gcm")==!verified.PackageKey.empty(),"Game encryption settings do not match the bootstrap");
        Require(Text(config["format"])=="pak"||verified.PackageKey.empty(),"Encrypted content requires packages");
        int duration=2000,logoSize=144;bool introEnabled=true,allowSkip=false;
        if(config.has_child("intro")){auto intro=config["intro"];introEnabled=Text(intro["enabled"])=="true";duration=std::stoi(Text(intro["duration_ms"]));logoSize=std::stoi(Text(intro["logo_size"]));allowSkip=Text(intro["allow_skip"])=="true";}
        Require(duration>=0&&duration<=30000&&logoSize>=48&&logoSize<=512,"Invalid intro settings");
        const auto started=std::chrono::steady_clock::now();
        if(!verify){
            DisableProcessWindowsGhosting();auto settings=config["window"];auto width=std::stoi(Text(settings["width"])),height=std::stoi(Text(settings["height"]));Require(width>=320&&width<=16384&&height>=200&&height<=16384,"Invalid window size");
            WNDCLASSW cls{};cls.hInstance=instance;cls.lpfnWndProc=WindowProcedure;cls.lpszClassName=L"BazzaltGameWindow";cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));Require(RegisterClassW(&cls)!=0,"Could not register game window");
            const auto mode=Text(settings["mode"]);Require(mode=="windowed"||mode=="maximized"||mode=="borderless"||mode=="fullscreen","Unsupported window mode");
            DWORD style=(mode=="borderless"||mode=="fullscreen")?WS_POPUP:WS_OVERLAPPEDWINDOW;if(Text(settings["resizable"])!="true")style&=~(WS_THICKFRAME|WS_MAXIMIZEBOX);
            MONITORINFO monitor{};monitor.cbSize=sizeof(monitor);Require(GetMonitorInfoW(MonitorFromPoint(POINT{0,0},MONITOR_DEFAULTTOPRIMARY),&monitor),"Could not inspect primary display");
            RECT bounds{0,0,width,height};AdjustWindowRect(&bounds,style,FALSE);int x=monitor.rcWork.left+(monitor.rcWork.right-monitor.rcWork.left-(bounds.right-bounds.left))/2,y=monitor.rcWork.top+(monitor.rcWork.bottom-monitor.rcWork.top-(bounds.bottom-bounds.top))/2;
            if(mode=="fullscreen"){x=monitor.rcMonitor.left;y=monitor.rcMonitor.top;bounds={0,0,monitor.rcMonitor.right-monitor.rcMonitor.left,monitor.rcMonitor.bottom-monitor.rcMonitor.top};}
            auto title=std::filesystem::u8path(Text(settings["title"])).wstring();
            window=CreateWindowExW(0,cls.lpszClassName,title.c_str(),style,x,y,bounds.right-bounds.left,bounds.bottom-bounds.top,nullptr,nullptr,instance,nullptr);Require(window!=nullptr,"Could not create game window");
            // Show a black native surface before extraction, engine init, or shader work.
            if(!smoke){ShowWindow(window,mode=="maximized"?SW_SHOWMAXIMIZED:SW_SHOW);UpdateWindow(window);}
            Startup::Create(window,instance,introEnabled,logoSize,allowSkip&&!smoke,!smoke);
        }
        TemporaryContent content;auto data=folder/"Data";
        if(Text(config["format"])=="pak"){
            TemporaryContent::ReclaimAbandoned();
            GUID id;Require(SUCCEEDED(CoCreateGuid(&id)),"Could not allocate game cache");wchar_t guid[40];StringFromGUID2(id,guid,40);
            content.Path=std::filesystem::temp_directory_path()/(std::wstring(L"BazzaltGame-")+guid);CreatePrivateContentDirectory(content.Path);content.Lock();
            std::vector<std::string> packages;for(auto name:config["packages"].children())packages.push_back(Text(name));Require(!packages.empty()&&packages.size()<=65536,"Invalid game package list");
            Extract(folder,content.Path,packages,verified.PackageKey);content.Leases=&verified.ExtractedLeases;VerifyExtracted(verified,content.Path);data=content.Path;
            if(!verified.PackageKey.empty()){SecureZeroMemory(verified.PackageKey.data(),verified.PackageKey.size());verified.PackageKey.clear();}
        }
        Bazzalt::Runtime::Engine engine;Engine=&engine;Require(engine.LoadProject(data/"Game.bproject"),engine.GetLastError().c_str());
        const auto startup=engine.GetProject().StartupScene.generic_u8string();
        Require(!startup.empty()&&std::filesystem::is_regular_file(data/SafePath(std::string(reinterpret_cast<const char*>(startup.data()),startup.size()))),"The packaged startup scene is missing");
        if(verify){Engine=nullptr;return 0;}
        Require(engine.ConfigureRenderingBackend(Text(config["window"]["backend"])),"Unsupported rendering backend");Require(engine.Init(false,false),engine.GetLastError().c_str());
        RECT client{};GetClientRect(window,&client);Require(engine.CreateEditorViewport(1,reinterpret_cast<std::uintptr_t>(window),false,client.right,client.bottom),"Could not create game surface");
        bool running=true,playing=false,frameReady=false;int frames=0;while(running&&!engine.ShouldClose()){
            const auto start=std::chrono::steady_clock::now();MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){if(message.message==WM_QUIT){running=false;break;}TranslateMessage(&message);DispatchMessageW(&message);}
            if(!running)break;if(!IsIconic(window)){
                if(!playing){
                    engine.RenderEditorFrame();frameReady=engine.WaitForViewportFrame(1);
                    const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count();
                    if(!frameReady&&elapsed>60000)throw std::runtime_error("The game renderer did not present a startup frame");
                    if(frameReady&&(smoke||!introEnabled||Startup::Skip||elapsed>=duration)){
                        Require(engine.StartScripts(),engine.GetLastError().c_str());engine.Update();Require(engine.WaitForViewportFrame(1),"The first gameplay frame did not finish");
                        Startup::Finish();Bazzalt::Runtime::InputAccess::SetActive(!smoke&&GetForegroundWindow()==window);playing=true;
                    }
                }else{engine.Update();if(smoke&&++frames>=3)break;}
            }
            std::this_thread::sleep_until(start+std::chrono::milliseconds(IsIconic(window)?50:16));
        }
        engine.DestroyEditorViewport(1);engine.Shutdown();Engine=nullptr;if(IsWindow(window))DestroyWindow(window);return 0;
    }catch(const std::exception& error){Engine=nullptr;if(window&&IsWindow(window))DestroyWindow(window);std::cerr<<error.what()<<'\n';if(!verify&&!smoke){auto n=MultiByteToWideChar(CP_UTF8,0,error.what(),-1,nullptr,0);std::wstring text(std::max(1,n),L'\0');MultiByteToWideChar(CP_UTF8,0,error.what(),-1,text.data(),int(text.size()));MessageBoxW(nullptr,text.c_str(),L"Game failed to start",MB_OK|MB_ICONERROR);}return 1;}
}
