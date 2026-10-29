#include <Bazzalt/Subsystem.h>
#include <Bazzalt/GlobalEvent.h>
#include <Bazzalt/Components/ScriptComponent.h>
#include <cstdlib>
#include <iostream>
#include <string>
SUBSYSTEM(SubsystemProbe) {
public:
    void OnRegister() override {
        const char* mode=std::getenv("BAZZALT_SUBSYSTEM_PROBE");
        if(mode&&std::string(mode)=="event"){
            Bazzalt::Application::OnPlay([]{
                Bazzalt::UUID id;Bazzalt::UUID::TryParse("98765432-1234-4234-8234-123456789abc",id);
                auto event=Bazzalt::GlobalEvent::Load(id);
                (void)event.Subscribe([](const Bazzalt::EventInvocation& value){std::cout<<"GLOBAL_EVENT:"<<value.Get<double>("Amount")<<'\n';});
                event.Fire({4.0});
            });return;
        }
        if(mode&&std::string(mode)=="lower"){Bazzalt::Application::OnBoot([]{});return;}
        if(mode&&std::string(mode)=="early"){(void)GetScene();return;}
        if(mode&&std::string(mode)=="attach"){
            Bazzalt::Application::OnLateUpdate([this](float){
                if(GetScene().FindEntityByName("InvalidAttachment"))return;
                auto entity=GetScene().CreateEntity("InvalidAttachment");auto& scripts=entity.AddComponent<Bazzalt::ScriptComponents>();
                Bazzalt::ScriptAttachment script;script.Source="Assets/__Scripts/SubsystemProbe.cpp";script.TypeName="SubsystemProbe";scripts.Values.push_back(script);
            });return;
        }
        for(int i=0;i<=int(Bazzalt::ApplicationEventType::OnShutdown);++i)
            Bazzalt::Application::Subscribe(Bazzalt::ApplicationEventType(i),[](const auto& event){std::cout<<"SUBSYSTEM:"<<Bazzalt::ApplicationEvents::GetName(event.Type)<<'\n';});
    }
};
namespace {
void* Create(){return static_cast<Bazzalt::Subsystem*>(new SubsystemProbe);}
void Destroy(void* value){delete static_cast<Bazzalt::Subsystem*>(value);}
void Register(void* value){static_cast<Bazzalt::Subsystem*>(value)->OnRegister();}
void Unregister(void* value){static_cast<Bazzalt::Subsystem*>(value)->OnUnregister();}
const Bazzalt::SubsystemModuleApi Api{Bazzalt::SubsystemAbiVersion,"SubsystemProbe",Create,Destroy,Register,Unregister};
}
#ifdef _WIN32
#define EXPORT_SUBSYSTEM __declspec(dllexport)
#else
#define EXPORT_SUBSYSTEM __attribute__((visibility("default")))
#endif
extern "C" EXPORT_SUBSYSTEM const Bazzalt::SubsystemModuleApi* BazzaltGetSubsystemModuleV1(){return &Api;}
