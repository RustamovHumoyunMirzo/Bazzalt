#pragma once
#include "Bazzalt/Component.h"
#include "Bazzalt/Math.h"
#include "Bazzalt/UUID.h"
namespace Bazzalt {
enum class ImpostorFacing { Spherical, Cylindrical };
// Authored RGBA atlas; frames run left-to-right then top-to-bottom around +Y.
struct Impostor : Component {
    UUID Atlas{};
    Vec2 Size{1,1},Pivot{.5f,.5f};
    Vec3 Center{};
    Vec4 Color{1,1,1,1};
    unsigned Columns=1,Rows=1,ViewCount=1;
    float AzimuthOffset=0,AlphaCutoff=.5f; // Offset in degrees, relative to local +Z.
    ImpostorFacing Facing=ImpostorFacing::Cylindrical;
    std::uint8_t LayerMask=0x7f;
    bool Visible=true,CastShadows=false;
    [[nodiscard]] bool IsValid() const {
        return Size.X>0&&Size.Y>0&&std::isfinite(Size.X)&&std::isfinite(Size.Y)&&Columns>0&&Columns<=64&&Rows>0&&Rows<=64&&ViewCount>0&&ViewCount<=256&&ViewCount<=Columns*Rows&&std::isfinite(AzimuthOffset)&&std::isfinite(AlphaCutoff)&&AlphaCutoff>=0&&AlphaCutoff<=1&&unsigned(Facing)<=1&&!(LayerMask&0x80)&&std::isfinite(Center.X)&&std::isfinite(Center.Y)&&std::isfinite(Center.Z)&&std::isfinite(Pivot.X)&&std::isfinite(Pivot.Y)&&std::isfinite(Color.X)&&std::isfinite(Color.Y)&&std::isfinite(Color.Z)&&std::isfinite(Color.W);
    }
    [[nodiscard]] unsigned SelectFrame(const Mat4& world,Vec3 eye) const {
        if(!IsValid())return 0;Mat4 inverse;if(!world.TryInverse(inverse))return 0;auto v=inverse.TransformPoint(eye)-Center;
        constexpr float pi=3.14159265358979323846f;float turns=std::atan2(v.X,v.Z)/(2*pi)-AzimuthOffset/360.f;turns-=std::floor(turns);
        return unsigned(std::floor(turns*ViewCount+.5f))%ViewCount;
    }
    [[nodiscard]] Vec4 GetAtlasRect(unsigned frame) const {
        if(!IsValid()||frame>=ViewCount)return {};float x=1.f/Columns,y=1.f/Rows;return {float(frame%Columns)*x,float(frame/Columns)*y,x,y};
    }
    [[nodiscard]] Mat4 GetMatrix(const Mat4& world,const Mat4& cameraWorld) const {
        const auto center=world.TransformPoint(Center);auto right=cameraWorld.TransformDirection({1,0,0}).Normalized(),up=cameraWorld.TransformDirection({0,1,0}).Normalized();
        if(Facing==ImpostorFacing::Cylindrical){up={0,1,0};auto normal=cameraWorld.TransformPoint({})-center;normal.Y=0;if(normal.LengthSquared()>1e-10f)right=Vec3::Cross(up,normal.Normalized());else{right.Y=0;right=right.LengthSquared()>1e-10f?right.Normalized():Vec3{1,0,0};}}
        auto normal=Vec3::Cross(right,up).Normalized();right*=Size.X*world.TransformDirection({1,0,0}).Length();up*=Size.Y*world.TransformDirection({0,1,0}).Length();Mat4 m;
        for(int i=0;i<3;++i){m(i,0)=i==0?right.X:i==1?right.Y:right.Z;m(i,1)=i==0?up.X:i==1?up.Y:up.Z;m(i,2)=i==0?normal.X:i==1?normal.Y:normal.Z;auto origin=center-right*Pivot.X-up*Pivot.Y;m(i,3)=i==0?origin.X:i==1?origin.Y:origin.Z;}return m;
    }
};
}
