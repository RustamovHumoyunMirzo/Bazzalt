#pragma once
#include "Bazzalt/ApplicationControl.h"
#include "Bazzalt/Project.h"
namespace Bazzalt::Runtime {
struct ApplicationAccess {
    static void Reset(bool editor,const ProjectMetadata& project);
    static void Observe(const ApplicationEvent& event);
    static void Advance(double deltaTime);
    static int GetEffectiveFrameRate();
    static void SetServicesReady(bool ready);
    static void SetSceneReady(bool ready);
    static void SetAudioReady(bool ready);
};
}
