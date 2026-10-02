#include <Bazzalt/Script.h>
#include <Bazzalt/Time.h>

COMPONENT(TimeProbe) {
public:
    void OnCreate() override { Bazzalt::Time::SetSlowMotion(0.25f); }
    void OnUpdate(float deltaTime) override {
        if (deltaTime != Bazzalt::Time::GetDeltaTime())
            throw std::runtime_error("Time service is not shared");
    }
    void OnFixedUpdate(float deltaTime) override {
        if (!Bazzalt::Time::IsInFixedTimeStep() || deltaTime != Bazzalt::Time::GetDeltaTime())
            throw std::runtime_error("Fixed time service is not shared");
    }
};
