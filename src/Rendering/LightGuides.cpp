#include "Rendering/LightGuides.h"
#include "Rendering/LightParameters.h"
#include <cmath>

namespace Bazzalt::Runtime {
std::vector<RenderBackend::EditorGuide> BuildLightGuides(const Light& original,const Mat4& world) {
    std::vector<RenderBackend::EditorGuide> guides;
    const Light light=SanitizeLight(original);
    if(!light.IsEnabled())return guides;
    const Vec3 p=world.TransformPoint({});
    if(!std::isfinite(p.X)||!std::isfinite(p.Y)||!std::isfinite(p.Z))return guides;
    Vec3 forward=world.TransformDirection({0,0,-1});
    if(!std::isfinite(forward.LengthSquared())||forward.LengthSquared()<1e-12f)forward={0,0,-1};
    else forward=forward.Normalized();
    const Vec3 helper=std::abs(forward.Y)<.99f?Vec3{0,1,0}:Vec3{1,0,0};
    const Vec3 right=Vec3::Cross(forward,helper).Normalized(),up=Vec3::Cross(right,forward).Normalized();
    // Guides remain visible even for a black light; their geometry is not illumination.
    const Vec4 outerColor{1,.85f,.35f,1},innerColor{1,.85f,.35f,.55f};
    const auto line=[&](Vec3 a,Vec3 b,Vec4 color){guides.push_back({a.X,a.Y,a.Z,b.X,b.Y,b.Z,color.X,color.Y,color.Z,color.W});};
    const auto ring=[&](Vec3 center,Vec3 a,Vec3 b,float radius,Vec4 color){
        if(radius<1e-6f)return;
        constexpr int steps=64;Vec3 previous=center+a*radius;
        for(int i=1;i<=steps;++i){const float angle=2*Pi*i/steps;
            const Vec3 next=i==steps?center+a*radius:center+(a*std::cos(angle)+b*std::sin(angle))*radius;
            line(previous,next,color);previous=next;}
    };
    if(light.Type==LightType::Point){
        // Point influence is a world-space sphere, independent of transform scale/rotation.
        ring(p,{1,0,0},{0,1,0},light.Range,outerColor);
        ring(p,{1,0,0},{0,0,1},light.Range,outerColor);
        ring(p,{0,1,0},{0,0,1},light.Range,outerColor);
    }else if(light.Type==LightType::Spot){
        const auto cone=[&](float angle,Vec4 color){
            // Filament Range is radial distance, not axial cone length.
            const Vec3 end=p+forward*(light.Range*std::cos(angle));
            const float radius=light.Range*std::sin(angle);
            ring(end,right,up,radius,color);
            for(const Vec3 axis:{right,-right,up,-up})line(p,end+axis*radius,color);
        };
        cone(light.OuterConeAngle,outerColor);
        if(light.OuterConeAngle-light.InnerConeAngle>1e-5f)cone(light.InnerConeAngle,innerColor);
        // Curved spherical falloff boundary in the four axial sections.
        for(const Vec3 axis:{right,-right,up,-up}){
            Vec3 previous=p+forward*light.Range;
            for(int i=1;i<=16;++i){const float angle=light.OuterConeAngle*i/16;
                const Vec3 next=p+(forward*std::cos(angle)+axis*std::sin(angle))*light.Range;
                line(previous,next,outerColor);previous=next;}
        }
    }else{
        // Directional lights have no range. Fixed-size direction marker.
        constexpr float length=3;const Vec3 end=p+forward*length;
        line(p,end,outerColor);
        for(const Vec3 axis:{right,-right,up,-up})line(end,end-forward*.45f+axis*.2f,outerColor);
        if(light.Type==LightType::Sun)ring(end,right,up,std::tan(light.SunAngularRadius)*length,outerColor);
    }
    return guides;
}
}
