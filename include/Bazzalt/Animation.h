#pragma once
#include "Bazzalt/Entity.h"
#include "Bazzalt/Math.h"
#include <string>
#include <vector>
#include <memory>
namespace Bazzalt {
struct BoneTransform {Vec3 Position{};Quaternion Rotation{};Vec3 Scale{1};std::vector<float> Weights;Mat4 GetMatrix()const{return Mat4::Transform(Position,Rotation,Scale);} };
struct SkeletonBone {std::string Name;int Parent=-1;BoneTransform BindPose;unsigned SourceIndex=0;std::vector<std::string> MorphTargets;};
struct BAZZALT_API Skeleton {
 std::vector<SkeletonBone> Bones;
 bool IsValid()const;
 int FindBone(const std::string& name)const;
 std::vector<BoneTransform> GetBindPose()const;
 std::vector<Mat4> ForwardKinematics(const std::vector<BoneTransform>& pose)const;
};
enum class AnimationInterpolation {Step,Linear,CubicSpline};
enum class AnimationChannel {Position,Rotation,Scale,Weights};
struct AnimationKey {float Time=0;Vec4 Value{},InTangent{},OutTangent{};};
struct AnimationTrack {unsigned Bone=0;AnimationChannel Channel=AnimationChannel::Position;AnimationInterpolation Interpolation=AnimationInterpolation::Linear;std::vector<AnimationKey> Keys;unsigned Target=0;};
struct AnimationMarker {float Time=0;std::string Name;};
struct BAZZALT_API AnimationClip {
 std::string Name;float Duration=0;std::vector<AnimationTrack> Tracks;std::vector<AnimationMarker> Markers;
 bool IsValid(const Skeleton& skeleton)const;
 std::vector<BoneTransform> Sample(const Skeleton& skeleton,float time,bool loop=true)const;
 static AnimationClip FromSource(const std::string& source);
 std::string ToSource()const;
};
struct BAZZALT_API AnimationRig {
 Skeleton Rig;std::vector<AnimationClip> Clips;
 static std::shared_ptr<const AnimationRig> Load(UUID model);
 int FindClip(const std::string& name)const;
};
struct AnimationBlendSample {std::string Clip;Vec2 Position{};float Speed=1;};
enum class AnimationBlendType {Clip,Blend1D,Blend2D};
struct AnimationState {std::string Name,Clip,ParameterX,ParameterY;AnimationBlendType Blend=AnimationBlendType::Clip;bool Loop=true;float Speed=1;std::vector<AnimationBlendSample> Samples;};
enum class AnimationComparison {Greater,Less,Equal,NotEqual};
struct AnimationCondition {std::string Parameter;AnimationComparison Comparison=AnimationComparison::Greater;float Value=0;};
struct AnimationTransition {std::string From,To;float Duration=.2f,ExitTime=-1;std::vector<AnimationCondition> Conditions;};
struct RetargetBone {unsigned Source=0,Target=0;bool CopyTranslation=false;};
struct RetargetSettings {float TranslationScale=1;std::vector<RetargetBone> Bones;};
struct HumanoidJoint {std::string Name;unsigned Bone=0;};
struct BAZZALT_API HumanoidAvatar {
 std::vector<HumanoidJoint> Joints;
 bool IsValid(const Skeleton&)const;
 int FindBone(const std::string& semantic)const;
 static HumanoidAvatar Detect(const Skeleton&);
};
struct BAZZALT_API AnimationController {
 UUID SourceModel{};float TranslationScale=1;std::vector<RetargetBone> RetargetMap;
 std::string Entry;std::vector<AnimationState> States;std::vector<AnimationTransition> Transitions;
 std::vector<AnimationClip> Clips;
 bool IsValid()const;
 static AnimationController FromSource(const std::string& source);
 static AnimationController Load(UUID asset);
};
enum class IKSolver {TwoBone,CCD,FABRIK};
struct IKSettings {IKSolver Solver=IKSolver::TwoBone;unsigned Root=0,Tip=0,Iterations=16;Vec3 Target{},Pole{0,0,1};float Weight=1,Tolerance=.001f;};
enum class RootMotionMode {Disabled,ExtractOnly,Transform,PhysicsKinematic};
struct AnimationEvent {std::string Name,State;unsigned Clip=0;};
struct AnimationStatus {std::string State;float Time=0,NormalizedTime=0,TransitionWeight=1;bool Playing=false;Vec3 RootDelta{};Quaternion RootRotation{};};
// Engine-thread facade. Poses use local bone transforms; IK targets are skeleton/model space.
class BAZZALT_API Animation final {
public:
 static std::vector<BoneTransform> Blend(const std::vector<BoneTransform>& a,const std::vector<BoneTransform>& b,float weight,const std::vector<float>& mask={});
 static std::vector<BoneTransform> Additive(const std::vector<BoneTransform>& base,const std::vector<BoneTransform>& sample,const std::vector<BoneTransform>& reference,float weight,const std::vector<float>& mask={});
 static bool SolveIK(const Skeleton&,std::vector<BoneTransform>& pose,const IKSettings&);
 static std::vector<BoneTransform> Retarget(const Skeleton& source,const std::vector<BoneTransform>& pose,const Skeleton& target,const RetargetSettings&);
 static RetargetSettings MapByName(const Skeleton& source,const Skeleton& target,float translationScale=1);
 static RetargetSettings MapHumanoid(const Skeleton& source,const HumanoidAvatar& sourceAvatar,const Skeleton& target,const HumanoidAvatar& targetAvatar,float translationScale=1);
 static void Play(Entity,const std::string& state="",float fadeSeconds=0);
 static void Rebind(Entity);static void Stop(Entity);static void Pause(Entity);static void Resume(Entity);
 static void SetRig(Entity,const AnimationRig&);static std::shared_ptr<const AnimationRig> GetRig(Entity);
 static void SetController(Entity,AnimationController);
 static void SetParameter(Entity,const std::string&,float value);static float GetParameter(Entity,const std::string&);
 static void SetTrigger(Entity,const std::string&);
 static void SetTime(Entity,float seconds);
 static void Evaluate(Entity,float deltaTime);
 static void SetIK(Entity,const std::string& name,const IKSettings&);static void RemoveIK(Entity,const std::string& name);
 static void SetLayer(Entity,const std::string& name,const std::string& clip,float weight,bool additive=false,const std::vector<float>& mask={});static void RemoveLayer(Entity,const std::string& name);
 static std::vector<BoneTransform> GetPose(Entity);static void SetPose(Entity,const std::vector<BoneTransform>&);
 static AnimationStatus GetStatus(Entity);static std::vector<AnimationEvent> GetEvents(Entity);
};
}
