#include "PosixIntegrity.h"
#include "Runtime/Engine.h"
#include "Runtime/InputAccess.h"
#include "Runtime/ApplicationEventsAccess.h"
#include "Runtime/ApplicationAccess.h"
#include "Platform/AndroidPlatform.h"
#include "GUI/GuiSystem.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <android/log.h>
#include <chrono>
#include <thread>

using namespace Bazzalt::Game;

extern "C" __attribute__((visibility("default"))) int SDL_main(int, char**) {
    SDL_Window* window = nullptr;
    try {
        const auto executable = Bazzalt::Runtime::AndroidPlatform::NativeLibrary("libmain.so");
        auto verified = VerifyInventory(executable);
        auto tree = ryml::parse_in_arena(ryml::csubstr(verified.Configuration.data(), verified.Configuration.size()));
        auto config = tree.rootref();
        auto folder = GameDataFolder(executable);
        struct ExtractedContent {
            std::filesystem::path Path;
            FileLeases* Leases=nullptr;
            ~ExtractedContent(){if(Leases)Leases->clear();if(!Path.empty()){std::error_code error;std::filesystem::remove_all(Path,error);}}
        } extracted;
        auto data = folder / "Data";
        if (Text(config["format"]) == "pak") {
            // App-private, build-specific content is never shared with another application.
            auto* cache=SDL_GetAndroidCachePath();Require(cache,"Android cache directory is unavailable");
            auto pattern=(std::filesystem::path(cache)/"bazzalt-session-XXXXXX").string();
            auto* temporary=mkdtemp(pattern.data());Require(temporary,"Could not create game session");
            data=temporary;extracted.Path=data;extracted.Leases=&verified.ExtractedLeases;
            std::vector<std::string> packages;
            for (auto name : config["packages"].children()) packages.push_back(Text(name));
            Require(!packages.empty() && packages.size() <= 65536, "Invalid package list");
            Extract(folder, data, packages, verified.PackageKey);
            VerifyExtracted(verified, data);
        } else Require(Text(config["format"]) == "loose", "Unsupported package format");

        Bazzalt::Runtime::AndroidPlatform::SetAssetDirectory(data);
        const auto intro=config["intro"];
        const bool brand=Text(intro["enabled"])=="true";
        const int duration=std::stoi(Text(intro["duration_ms"]));
        const auto introStarted=std::chrono::steady_clock::now();
        Bazzalt::Runtime::AndroidPlatform::ShowIntro(brand,std::stoi(Text(intro["logo_size"])),Text(intro["allow_skip"])=="true");
        Bazzalt::Runtime::Engine engine;
        const auto check = [&](bool success) { if (!success) throw std::runtime_error(engine.GetLastError()); };
        check(engine.LoadProject(data / "Game.bproject", false));
        check(engine.PrepareApplication());
        using Events = Bazzalt::Runtime::ApplicationEventsAccess;
        using Input = Bazzalt::Runtime::InputAccess;
        Events::Emit({Bazzalt::ApplicationEventType::OnBeforeWindowCreate});
        Events::ThrowIfFailed();
        Require(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS), SDL_GetError());
        auto settings = config["window"];
        window = SDL_CreateWindow(Text(settings["title"]).c_str(), 1280, 720, SDL_WINDOW_FULLSCREEN | SDL_WINDOW_HIGH_PIXEL_DENSITY);
        Require(window, SDL_GetError());
        auto backend = Text(settings["backend"]);
        if (backend == "opengles") backend = "opengl";
        Require(engine.ConfigureRenderingBackend(backend), "Unsupported Android rendering backend");
        check(engine.Init(false, false));
        auto startup = engine.GetProject().StartupScene;
        Require(!startup.empty(), "A startup scene must be selected");
        check(engine.LoadScene(data / SafePath(startup.generic_string())));
        int width = 0, height = 0;
        SDL_GetWindowSizeInPixels(window, &width, &height);
        auto* native = SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, nullptr);
        Require(native && engine.CreateEditorViewport(1, reinterpret_cast<std::uintptr_t>(native), false, width, height), "Could not create Android game surface");
        Events::Emit({Bazzalt::ApplicationEventType::OnPostWindowCreate});
        bool playing=false;
        bool scriptsStarted=false;
        Input::SetActive(false);
        bool textInput=false;
        bool active = true;
        bool surfaceReady = true;
        while (!engine.ShouldClose()) {
            const auto started = std::chrono::steady_clock::now();
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                switch (event.type) {
                case SDL_EVENT_QUIT:
                case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                    Events::Emit({Bazzalt::ApplicationEventType::OnWindowCloseRequested});
                    engine.RequestClose();
                    break;
                case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
                    SDL_GetWindowSizeInPixels(window, &width, &height);
                    if (width > 0 && height > 0) engine.ResizeEditorViewport(1, width, height);
                    Bazzalt::ApplicationEvent resized{Bazzalt::ApplicationEventType::OnWindowResize};
                    resized.Width = width; resized.Height = height; Events::Emit(resized);
                    break;
                }
                case SDL_EVENT_WILL_ENTER_BACKGROUND:
                case SDL_EVENT_WINDOW_FOCUS_LOST:
                case SDL_EVENT_DID_ENTER_FOREGROUND:
                case SDL_EVENT_WINDOW_FOCUS_GAINED: {
                    active = event.type == SDL_EVENT_DID_ENTER_FOREGROUND || event.type == SDL_EVENT_WINDOW_FOCUS_GAINED;
                    if(event.type==SDL_EVENT_WILL_ENTER_BACKGROUND && surfaceReady){engine.DestroyEditorViewport(1);surfaceReady=false;}
                    if(active && !surfaceReady){
                        SDL_GetWindowSizeInPixels(window,&width,&height);
                        auto* handle=SDL_GetPointerProperty(SDL_GetWindowProperties(window),SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER,nullptr);
                        Require(handle&&engine.CreateEditorViewport(1,reinterpret_cast<std::uintptr_t>(handle),false,width,height),"Could not restore Android game surface");
                        surfaceReady=true;
                    }
                    Input::SetActive(active&&playing);
                    engine.ResetFrameClock();
                    Bazzalt::ApplicationEvent focus{Bazzalt::ApplicationEventType::OnWindowFocusChanged};
                    focus.IsFocused = active; Events::Emit(focus);
                    break;
                }
                case SDL_EVENT_KEY_DOWN:
                case SDL_EVENT_KEY_UP: Input::KeyEvent(int(event.key.scancode), event.type == SDL_EVENT_KEY_DOWN, event.key.repeat); break;
                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                case SDL_EVENT_MOUSE_BUTTON_UP: Input::ButtonEvent(event.button.button, event.type == SDL_EVENT_MOUSE_BUTTON_DOWN); break;
                case SDL_EVENT_MOUSE_MOTION: Input::MotionEvent(event.motion.x, event.motion.y, event.motion.xrel, event.motion.yrel); break;
                case SDL_EVENT_MOUSE_WHEEL: Input::ScrollEvent(event.wheel.x, event.wheel.y); break;
                case SDL_EVENT_TEXT_INPUT: Input::TextEvent(event.text.text); break;
                default: break;
                }
            }
            Events::ThrowIfFailed();
            if(active&&surfaceReady&&!playing){
                if(scriptsStarted)engine.Update();
                else engine.RenderEditorFrame();
                const bool ready=engine.WaitForViewportFrame(1);
                const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(started-introStarted).count();
                Require(ready||elapsed<60000,"Android renderer startup timed out");
                if(ready&&(!brand||elapsed>=duration||Bazzalt::Runtime::AndroidPlatform::IntroSkipped())){
                    if(!scriptsStarted){
                        check(engine.StartScripts());scriptsStarted=true;engine.ResetFrameClock();
                    }else{
                        Bazzalt::Runtime::AndroidPlatform::FinishIntro();playing=true;Input::SetActive(true);
                    }
                }
            }
            else if (active && surfaceReady) engine.Update();
            else if(playing&&Bazzalt::Application::GetRunInBackground())engine.Update(false);
            else engine.ResetFrameClock();
            bool wantsText=false;
            auto& scene=engine.GetScene();
            if(playing&&scene.HasSystem<Bazzalt::Runtime::GuiSystem>()){
                auto focused=scene.GetEntity(scene.GetSystem<Bazzalt::Runtime::GuiSystem>().Focused);
                if(focused){
                    const auto* input=focused.TryGetComponent<Bazzalt::GuiTextInput>();
                    wantsText=input&&input->Enabled&&input->Interactable&&!input->ReadOnly;
                }
            }
            if(wantsText!=textInput){
                if(wantsText)SDL_StartTextInput(window);else SDL_StopTextInput(window);
                textInput=wantsText;
            }
            const int rate = Bazzalt::Runtime::ApplicationAccess::GetEffectiveFrameRate();
            if(active&&rate>0)std::this_thread::sleep_until(started+std::chrono::duration<double>(1.0/rate));
            else if(!active)std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        engine.BeginShutdown(); engine.Shutdown(true);
        SDL_DestroyWindow(window); window = nullptr;
        engine.EndApplication(); Events::ThrowIfFailed(); SDL_Quit();
        return Bazzalt::Application::GetExitCode();
    } catch (const std::exception& error) {
        __android_log_print(ANDROID_LOG_ERROR, "Bazzalt", "%s", error.what());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Bazzalt game error", error.what(), window);
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
}
