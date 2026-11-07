#pragma once
#include "Bazzalt/Components/NavMeshModifier.h"
#include "Bazzalt/Components/NavMeshSurface.h"
#include "Bazzalt/Components/NavigationAgent.h"
#include "Bazzalt/Components/NavigationObstacle.h"
namespace Bazzalt::Runtime {
template <class T, class F> void VisitNavMeshSurface(T &v, F f) {
    f("Data", v.Data);
    f("Center", v.Center);
    f("Size", v.Size);
    f("CellSize", v.CellSize);
    f("CellHeight", v.CellHeight);
    f("AgentRadius", v.AgentRadius);
    f("AgentHeight", v.AgentHeight);
    f("MaxSlope", v.MaxSlope);
    f("MaxClimb", v.MaxClimb);
    f("MaxSimplificationError", v.MaxSimplificationError);
    f("TileSize", v.TileSize);
    f("MaxAgents", v.MaxAgents);
    f("MaxObstacles", v.MaxObstacles);
    f("LayerMask", v.LayerMask);
    f("UsePhysicsGeometry", v.UsePhysicsGeometry);
    f("AutoBake", v.AutoBake);
}
template <class T, class F> void VisitNavigationAgent(T &v, F f) {
    f("Surface", v.Surface);
    f("Radius", v.Radius);
    f("Height", v.Height);
    f("Speed", v.Speed);
    f("Acceleration", v.Acceleration);
    f("AngularSpeed", v.AngularSpeed);
    f("StoppingDistance", v.StoppingDistance);
    f("BaseOffset", v.BaseOffset);
    f("SeparationWeight", v.SeparationWeight);
    f("AreaMask", v.AreaMask);
    f("UpdatePosition", v.UpdatePosition);
    f("UpdateRotation", v.UpdateRotation);
    f("Avoidance", v.Avoidance);
    f("Stopped", v.Stopped);
}
template <class T, class F> void VisitNavigationObstacle(T &v, F f) {
    f("Shape", v.Shape);
    f("Center", v.Center);
    f("Size", v.Size);
    f("Radius", v.Radius);
    f("Height", v.Height);
    f("Carve", v.Carve);
}
template <class T, class F> void VisitNavMeshModifier(T &v, F f) {
    f("Area", v.Area);
    f("Ignore", v.Ignore);
}
} // namespace Bazzalt::Runtime
