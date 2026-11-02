#pragma once
#include "Bazzalt/Components/HLOD.h"
#include "Bazzalt/Components/LODTransition.h"
#include "Bazzalt/Components/Impostor.h"
namespace Bazzalt::Runtime {
template<class V,class F> void VisitHLOD(V& v,F f){f("Center",v.Center);f("Size",v.Size);f("Bias",v.Bias);f("Hysteresis",v.Hysteresis);f("ScreenHeight",v.ScreenHeight);f("CullHeight",v.CullHeight);f("ForcedLevel",v.ForcedLevel);f("Sources",v.Sources);f("Proxies",v.Proxies);}
template<class V,class F> void VisitLODTransition(V& v,F f){f("Duration",v.Duration);f("Easing",v.Easing);f("FadeCulling",v.FadeCulling);}
template<class V,class F> void VisitImpostor(V& v,F f){f("Atlas",v.Atlas);f("Size",v.Size);f("Pivot",v.Pivot);f("Center",v.Center);f("Color",v.Color);f("Columns",v.Columns);f("Rows",v.Rows);f("ViewCount",v.ViewCount);f("AzimuthOffset",v.AzimuthOffset);f("AlphaCutoff",v.AlphaCutoff);f("Facing",v.Facing);f("LayerMask",v.LayerMask);f("Visible",v.Visible);f("CastShadows",v.CastShadows);}
}
