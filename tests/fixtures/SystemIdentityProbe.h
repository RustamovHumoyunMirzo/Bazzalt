#pragma once
#include <Bazzalt/Scene.h>
#if defined(__GNUC__)
class __attribute__((visibility("hidden"))) SharedSystemProbe final : public Bazzalt::System {
#else
class SharedSystemProbe final : public Bazzalt::System {
#endif
public:
    int Value=42;
protected:
    void OnUpdate(Bazzalt::Scene&,float) override {}
};
#if defined(_WIN32)
#if defined(BUILDING_SYSTEM_IDENTITY_PROBE)
#define SYSTEM_PROBE_API __declspec(dllexport)
#else
#define SYSTEM_PROBE_API __declspec(dllimport)
#endif
#else
#define SYSTEM_PROBE_API __attribute__((visibility("default")))
#endif
extern "C" SYSTEM_PROBE_API int ReadSystemProbe(Bazzalt::Scene* scene);
extern "C" SYSTEM_PROBE_API bool RemoveSystemProbe(Bazzalt::Scene* scene);
extern "C" SYSTEM_PROBE_API void AddSystemProbe(Bazzalt::Scene* scene);
