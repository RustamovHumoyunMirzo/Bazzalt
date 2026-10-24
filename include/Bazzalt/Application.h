#pragma once
#include "Bazzalt/Script.h"
namespace Bazzalt {
// Optional project-level script (Project.Properties["application.script"] = asset UUID).
// It has no owning entity; scene state is not available until OnSceneLoaded.
class Application : public Behavior {
public:
    virtual void OnBoot() {}
    virtual void OnBeforeWindowCreate() {}
    virtual void OnInit() {}
    virtual void OnLoad() {}
    virtual void OnPostWindowCreate() {}
    virtual void OnPreUpdate(float) {}
    virtual void OnPreRender() {}
    virtual void OnRender() {}
    virtual void OnPostRender() {}
    virtual void OnWindowResize(std::uint32_t,std::uint32_t) {}
    virtual void OnWindowFocusChanged(bool) {}
    virtual void OnWindowCloseRequested() {}
    virtual void OnSceneLoad(const std::string&) {}
    virtual void OnSceneLoaded(const std::string&) {}
    virtual void OnSceneUnload(const std::string&) {}
    virtual void OnSceneUnloaded(const std::string&) {}
    virtual void OnPlay() {}
    virtual void OnStop() {}
    virtual void OnPreShutdown() {}
    virtual void OnShutdown() {}
    void OnApplicationEvent(const ApplicationEvent& event) override {
        switch(event.Type){
        case ApplicationEventType::OnBoot:OnBoot();break;
        case ApplicationEventType::OnBeforeWindowCreate:OnBeforeWindowCreate();break;
        case ApplicationEventType::OnInit:OnInit();break;
        case ApplicationEventType::OnLoad:OnLoad();break;
        case ApplicationEventType::OnPostWindowCreate:OnPostWindowCreate();break;
        case ApplicationEventType::OnPreUpdate:OnPreUpdate(event.DeltaTime);break;
        case ApplicationEventType::OnUpdate:OnUpdate(event.DeltaTime);break;
        case ApplicationEventType::OnFixedUpdate:OnFixedUpdate(event.DeltaTime);break;
        case ApplicationEventType::OnLateUpdate:OnLateUpdate(event.DeltaTime);break;
        case ApplicationEventType::OnPreRender:OnPreRender();break;
        case ApplicationEventType::OnRender:OnRender();break;
        case ApplicationEventType::OnPostRender:OnPostRender();break;
        case ApplicationEventType::OnWindowResize:OnWindowResize(event.Width,event.Height);break;
        case ApplicationEventType::OnWindowFocusChanged:OnWindowFocusChanged(event.IsFocused);break;
        case ApplicationEventType::OnWindowCloseRequested:OnWindowCloseRequested();break;
        case ApplicationEventType::OnSceneLoad:OnSceneLoad(event.ScenePath);break;
        case ApplicationEventType::OnSceneLoaded:OnSceneLoaded(event.SceneUUID);break;
        case ApplicationEventType::OnSceneUnload:OnSceneUnload(event.SceneUUID);break;
        case ApplicationEventType::OnSceneUnloaded:OnSceneUnloaded(event.SceneUUID);break;
        case ApplicationEventType::OnPlay:OnPlay();break;
        case ApplicationEventType::OnStop:OnStop();break;
        case ApplicationEventType::OnPreShutdown:OnPreShutdown();break;
        case ApplicationEventType::OnShutdown:OnShutdown();break;
        }
    }
};
}
#define APPLICATION(Name) class Name final : public ::Bazzalt::Application
