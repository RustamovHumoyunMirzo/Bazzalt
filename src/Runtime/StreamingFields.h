#pragma once
#include "Bazzalt/Components/WorldCell.h"
#include "Bazzalt/Components/StreamingSource.h"
namespace Bazzalt::Runtime {
template<class T,class F>void VisitWorldCell(T& v,F f){f("SceneAsset",v.SceneAsset);f("HalfExtents",v.HalfExtents);f("LoadDistance",v.LoadDistance);f("UnloadDistance",v.UnloadDistance);f("Priority",v.Priority);f("AlwaysLoaded",v.AlwaysLoaded);}
template<class T,class F>void VisitStreamingSource(T& v,F f){f("DistanceScale",v.DistanceScale);}
}
