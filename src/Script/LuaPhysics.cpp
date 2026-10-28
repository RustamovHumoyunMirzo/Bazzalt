#include "Script/LuaBindings.h"
#include "Physics/PhysicsFields.h"
#include "Runtime/ApplicationEventsAccess.h"
namespace Bazzalt::Runtime {
namespace {
void Budget(lua_State* state,lua_Debug*){luaL_error(state,"Physics callback exceeded its Lua instruction budget");}
template<class T> void Operations(sol::table api,const char* name){
    sol::table operations=api["_ComponentOperations"];auto type=operations.create_named(name);
    type["Has"]=[](Entity e){return e.HasComponent<T>();};type["Get"]=[](Entity e){return T(e.GetComponent<T>());};
    type["Add"]=[](Entity e){return T(e.AddComponent<T>());};
    type["Set"]=[](Entity e,T value){if(!value.IsValid())throw std::invalid_argument("Invalid physics component properties");e.GetComponent<T>()=value;};
    type["Remove"]=[](Entity e){e.RemoveComponent<T>();};type["IsEnabled"]=[](Entity e){return e.IsComponentEnabled<T>();};type["SetEnabled"]=[](Entity e,bool value){e.SetComponentEnabled<T>(value);};
    type["Inherited"]=[](Entity e)->std::optional<T>{auto* value=e.GetScene()->TryGetInheritedComponent<T>(e);return value?std::optional<T>(*value):std::nullopt;};
}
}
void BindLuaPhysics(sol::table api){
    api.new_enum("BodyMotionType","Static",BodyMotionType::Static,"Kinematic",BodyMotionType::Kinematic,"Dynamic",BodyMotionType::Dynamic);
    api.new_enum("CollisionDetectionMode","Discrete",CollisionDetectionMode::Discrete,"Continuous",CollisionDetectionMode::Continuous);
    api.new_enum("BodyInterpolation","None",BodyInterpolation::None,"Interpolate",BodyInterpolation::Interpolate);
    api.new_enum("BodyConstraints","None",BodyConstraints::None,"PositionX",BodyConstraints::PositionX,"PositionY",BodyConstraints::PositionY,"PositionZ",BodyConstraints::PositionZ,"RotationX",BodyConstraints::RotationX,"RotationY",BodyConstraints::RotationY,"RotationZ",BodyConstraints::RotationZ,"All",BodyConstraints::All);
    api.new_enum("ColliderShape","Box",ColliderShape::Box,"Sphere",ColliderShape::Sphere,"Capsule",ColliderShape::Capsule,"Cylinder",ColliderShape::Cylinder,"Plane",ColliderShape::Plane);
    api.new_enum("PhysicsEventType","CollisionEnter",PhysicsEventType::CollisionEnter,"CollisionStay",PhysicsEventType::CollisionStay,"CollisionExit",PhysicsEventType::CollisionExit,"TriggerEnter",PhysicsEventType::TriggerEnter,"TriggerStay",PhysicsEventType::TriggerStay,"TriggerExit",PhysicsEventType::TriggerExit);
    auto rb=api.new_usertype<Rigidbody>("Rigidbody",sol::constructors<Rigidbody()>(),sol::base_classes,sol::bases<Component>());
#define FIELD(Name,Label) rb[#Name]=&Rigidbody::Name;
    BAZZALT_BODY_FIELDS(FIELD)
#undef FIELD
    rb["IsValid"]=&Rigidbody::IsValid;
    auto collider=api.new_usertype<ShapeCollider>("ShapeCollider",sol::constructors<ShapeCollider()>(),sol::base_classes,sol::bases<Component>());
#define FIELD(Name,Label) collider[#Name]=&ShapeCollider::Name;
    BAZZALT_SHAPE_FIELDS(FIELD)
#undef FIELD
    collider["IsValid"]=&ShapeCollider::IsValid;
    auto mesh=api.new_usertype<MeshCollider>("MeshCollider",sol::constructors<MeshCollider()>(),sol::base_classes,sol::bases<Component>());
#define FIELD(Name,Label) mesh[#Name]=&MeshCollider::Name;
    BAZZALT_MESH_COLLIDER_FIELDS(FIELD)
#undef FIELD
    mesh["IsValid"]=&MeshCollider::IsValid;
    Operations<Rigidbody>(api,"Rigidbody");Operations<ShapeCollider>(api,"ShapeCollider");Operations<MeshCollider>(api,"MeshCollider");
    api.new_enum("JointType","Distance",JointType::Distance,"Hinge",JointType::Hinge,"Slider",JointType::Slider,"BallAndSocket",JointType::BallAndSocket,"Fixed",JointType::Fixed);
    api.new_enum("JointMotorMode","Off",JointMotorMode::Off,"Velocity",JointMotorMode::Velocity,"Position",JointMotorMode::Position);
    auto typeJoint=api.new_usertype<Joint>("Joint",sol::constructors<Joint()>(),sol::base_classes,sol::bases<Component>());
#define FIELD(Name,Label) typeJoint[#Name]=&Joint::Name;
    BAZZALT_JOINT_FIELDS(FIELD)
#undef FIELD
    typeJoint["IsValid"]=&Joint::IsValid;Operations<Joint>(api,"Joint");
    auto typeDistanceJoint=api.new_usertype<DistanceJoint>("DistanceJoint",sol::constructors<DistanceJoint()>(),sol::base_classes,sol::bases<Joint,Component>());
    Operations<DistanceJoint>(api,"DistanceJoint"); // Properties and IsValid are inherited from Joint.
    auto typeHingeJoint=api.new_usertype<HingeJoint>("HingeJoint",sol::constructors<HingeJoint()>(),sol::base_classes,sol::bases<Joint,Component>());
    Operations<HingeJoint>(api,"HingeJoint"); // Properties and IsValid are inherited from Joint.
    auto typeSliderJoint=api.new_usertype<SliderJoint>("SliderJoint",sol::constructors<SliderJoint()>(),sol::base_classes,sol::bases<Joint,Component>());
    Operations<SliderJoint>(api,"SliderJoint"); // Properties and IsValid are inherited from Joint.
    auto typeBallAndSocketJoint=api.new_usertype<BallAndSocketJoint>("BallAndSocketJoint",sol::constructors<BallAndSocketJoint()>(),sol::base_classes,sol::bases<Joint,Component>());
    Operations<BallAndSocketJoint>(api,"BallAndSocketJoint"); // Properties and IsValid are inherited from Joint.
    auto typeFixedJoint=api.new_usertype<FixedJoint>("FixedJoint",sol::constructors<FixedJoint()>(),sol::base_classes,sol::bases<Joint,Component>());
    Operations<FixedJoint>(api,"FixedJoint"); // Properties and IsValid are inherited from Joint.
    api.new_enum("ClothBendMode","None",ClothBendMode::None,"Distance",ClothBendMode::Distance,"Dihedral",ClothBendMode::Dihedral);
    auto typeCloth=api.new_usertype<Cloth>("Cloth",sol::constructors<Cloth()>(),sol::base_classes,sol::bases<Component>());
#define FIELD(Name,Label) typeCloth[#Name]=&Cloth::Name;
    BAZZALT_CLOTH_FIELDS(FIELD)
#undef FIELD
    typeCloth["IsValid"]=&Cloth::IsValid;Operations<Cloth>(api,"Cloth");
    auto typeRope=api.new_usertype<Rope>("Rope",sol::constructors<Rope()>(),sol::base_classes,sol::bases<Component>());
#define FIELD(Name,Label) typeRope[#Name]=&Rope::Name;
    BAZZALT_ROPE_FIELDS(FIELD)
#undef FIELD
    typeRope["IsValid"]=&Rope::IsValid;Operations<Rope>(api,"Rope");
    auto settings=api.new_usertype<PhysicsSettings>("PhysicsSettings",sol::constructors<PhysicsSettings()>());
#define FIELD(Name,Label) settings[#Name]=&PhysicsSettings::Name;
    BAZZALT_PHYSICS_SETTINGS_FIELDS(FIELD)
#undef FIELD
    settings["CollisionMasks"]=sol::property([](const PhysicsSettings& s){return sol::as_table(s.CollisionMasks);},[](PhysicsSettings& s,sol::as_table_t<std::vector<std::uint32_t>> masks){if(masks.value().size()!=32)throw std::invalid_argument("CollisionMasks requires 32 masks");std::copy(masks.value().begin(),masks.value().end(),s.CollisionMasks.begin());});
    settings["IsValid"]=&PhysicsSettings::IsValid;
    auto options=api.new_usertype<PhysicsQueryOptions>("PhysicsQueryOptions",sol::constructors<PhysicsQueryOptions()>());
    options["LayerMask"]=&PhysicsQueryOptions::LayerMask;options["MaxResults"]=&PhysicsQueryOptions::MaxResults;options["IncludeTriggers"]=&PhysicsQueryOptions::IncludeTriggers;options["IncludeBackFaces"]=&PhysicsQueryOptions::IncludeBackFaces;
    options["ExcludedEntities"]=sol::property([](const PhysicsQueryOptions& o){return sol::as_table(o.ExcludedEntities);},[](PhysicsQueryOptions& o,sol::as_table_t<std::vector<UUID>> ids){o.ExcludedEntities=ids.value();});
    auto shape=api.new_usertype<PhysicsShape>("PhysicsShape",sol::constructors<PhysicsShape()>());
    shape["Shape"]=&PhysicsShape::Shape;shape["Size"]=&PhysicsShape::Size;shape["Radius"]=&PhysicsShape::Radius;shape["Height"]=&PhysicsShape::Height;
    auto hit=api.new_usertype<PhysicsHit>("PhysicsHit",sol::constructors<PhysicsHit()>());
#define FIELD(Name) hit[#Name]=&PhysicsHit::Name;
    FIELD(Object) FIELD(Collider) FIELD(Point) FIELD(Normal) FIELD(Distance) FIELD(Fraction) FIELD(SubShape)
#undef FIELD
    hit["IsValid"]=&PhysicsHit::IsValid;
    auto contact=api.new_usertype<ContactPoint>("ContactPoint",sol::constructors<ContactPoint()>());
    contact["PointA"]=&ContactPoint::PointA;contact["PointB"]=&ContactPoint::PointB;contact["Penetration"]=&ContactPoint::Penetration;
    auto event=api.new_usertype<CollisionEvent>("CollisionEvent",sol::constructors<CollisionEvent()>());
#define FIELD(Name) event[#Name]=&CollisionEvent::Name;
    FIELD(Type) FIELD(DeltaTime) FIELD(BodyA) FIELD(BodyB) FIELD(ColliderA) FIELD(ColliderB) FIELD(BodyAUUID) FIELD(BodyBUUID) FIELD(ColliderAUUID) FIELD(ColliderBUUID) FIELD(Normal) FIELD(RelativeVelocity)
#undef FIELD
    event["Contacts"]=sol::property([](const CollisionEvent& e){return sol::as_table(e.Contacts);});
    auto world=api.new_usertype<PhysicsScene>("PhysicsScene",sol::no_constructor);
#define METHOD(Name) world[#Name]=&PhysicsScene::Name;
    METHOD(BreakJoint) METHOD(RepairJoint) METHOD(IsJointBroken) METHOD(SetSoftBodyVertexPinned) METHOD(GetSettings) METHOD(SetSettings) METHOD(SetLayerCollision) METHOD(GetLayerCollision) METHOD(IsRunning) METHOD(Simulate) METHOD(Synchronize) METHOD(GetBodyCount)
    METHOD(ApplyForce) METHOD(ApplyForceAtPosition) METHOD(ApplyImpulse) METHOD(ApplyImpulseAtPosition) METHOD(ApplyTorque) METHOD(ApplyAngularImpulse)
    METHOD(SetLinearVelocity) METHOD(SetAngularVelocity) METHOD(GetLinearVelocity) METHOD(GetAngularVelocity) METHOD(MoveKinematic) METHOD(WakeUp) METHOD(Sleep) METHOD(IsAwake) METHOD(Unsubscribe)
#undef METHOD
    world["GetSoftBodyVertices"]=[](PhysicsScene w,Entity e){return sol::as_table(w.GetSoftBodyVertices(e));};
    world["ApplySoftBodyImpulse"]=[](PhysicsScene w,Entity e,Vec3 impulse,sol::optional<int> index){return w.ApplySoftBodyImpulse(e,impulse,index.value_or(-1));};
    world["SetGravity"]=sol::overload([](PhysicsScene w,Vec3 value){w.SetGravity(value);},[](PhysicsScene w,Entity e,sol::optional<Vec3> value){return w.SetGravity(e,value?std::optional<Vec3>(*value):std::nullopt);});
    world["GetGravity"]=sol::overload([](PhysicsScene w){return w.GetGravity();},[](PhysicsScene w,Entity e){return w.GetGravity(e);});
    world["Teleport"]=[](PhysicsScene w,Entity e,Vec3 p,Quaternion q,sol::optional<bool> awake){return w.Teleport(e,p,q,awake.value_or(true));};
    world["Raycast"]=[](PhysicsScene w,Vec3 origin,Vec3 direction,sol::optional<float> distance,sol::optional<PhysicsQueryOptions> o){return w.Raycast(origin,direction,distance.value_or(10000),o.value_or(PhysicsQueryOptions{}));};
    world["RaycastAll"]=[](PhysicsScene w,Vec3 origin,Vec3 direction,sol::optional<float> distance,sol::optional<PhysicsQueryOptions> o){return sol::as_table(w.RaycastAll(origin,direction,distance.value_or(10000),o.value_or(PhysicsQueryOptions{})));};
    world["SphereCast"]=[](PhysicsScene w,Vec3 p,float radius,Vec3 direction,float distance,sol::optional<PhysicsQueryOptions> o){return w.SphereCast(p,radius,direction,distance,o.value_or(PhysicsQueryOptions{}));};
    world["SphereCastAll"]=[](PhysicsScene w,Vec3 p,float radius,Vec3 direction,float distance,sol::optional<PhysicsQueryOptions> o){return sol::as_table(w.SphereCastAll(p,radius,direction,distance,o.value_or(PhysicsQueryOptions{})));};
    world["CheckSphere"]=[](PhysicsScene w,Vec3 p,float radius,sol::optional<PhysicsQueryOptions> o){return w.CheckSphere(p,radius,o.value_or(PhysicsQueryOptions{}));};
    world["CheckBox"]=[](PhysicsScene w,Vec3 p,Vec3 halfExtents,sol::optional<Quaternion> q,sol::optional<PhysicsQueryOptions> o){return w.CheckBox(p,halfExtents,q.value_or(Quaternion{}),o.value_or(PhysicsQueryOptions{}));};
    world["ShapeCast"]=[](PhysicsScene w,PhysicsShape s,Transform t,Vec3 direction,float distance,sol::optional<PhysicsQueryOptions> o){return w.ShapeCast(s,t,direction,distance,o.value_or(PhysicsQueryOptions{}));};
    world["ShapeCastAll"]=[](PhysicsScene w,PhysicsShape s,Transform t,Vec3 direction,float distance,sol::optional<PhysicsQueryOptions> o){return sol::as_table(w.ShapeCastAll(s,t,direction,distance,o.value_or(PhysicsQueryOptions{})));};
    world["OverlapShape"]=[](PhysicsScene w,PhysicsShape s,Transform t,sol::optional<PhysicsQueryOptions> o){return sol::as_table(w.OverlapShape(s,t,o.value_or(PhysicsQueryOptions{})));};
    world["OverlapSphere"]=[](PhysicsScene w,Vec3 p,float radius,sol::optional<PhysicsQueryOptions> o){return sol::as_table(w.OverlapSphere(p,radius,o.value_or(PhysicsQueryOptions{})));};
    world["OverlapBox"]=[](PhysicsScene w,Vec3 p,Vec3 extents,sol::optional<Quaternion> rotation,sol::optional<PhysicsQueryOptions> o){return sol::as_table(w.OverlapBox(p,extents,rotation.value_or(Quaternion{}),o.value_or(PhysicsQueryOptions{})));};
    world["Subscribe"]=[](PhysicsScene w,PhysicsEventType type,sol::protected_function fn,sol::optional<Entity> entity){auto* state=fn.lua_state();auto previous=ApplicationEventsAccess::SetOwner(state);PhysicsScene::Subscription id=0;try{id=w.Subscribe(type,[fn=std::move(fn),state](const CollisionEvent& e)mutable{auto hook=lua_gethook(state);int mask=lua_gethookmask(state),count=lua_gethookcount(state);lua_sethook(state,Budget,LUA_MASKCOUNT,1000000);auto result=fn(e);lua_sethook(state,hook,mask,count);if(!result.valid()){sol::error error=result;throw std::runtime_error(error.what());}},entity.value_or(Entity{}));}catch(...){ApplicationEventsAccess::SetOwner(previous);throw;}ApplicationEventsAccess::SetOwner(previous);return id;};
    sol::usertype<Scene> scene=api["Scene"];scene["GetPhysics"]=&Scene::GetPhysics;scene["GetPhysicsSettings"]=[](Scene& s){return PhysicsSettings(s.GetPhysicsSettings());};scene["SetPhysicsSettings"]=&Scene::SetPhysicsSettings;
    scene["Raycast"]=sol::overload([](Scene& s,SceneRay ray,sol::optional<SceneQueryOptions> o){return s.Raycast(ray,o.value_or(SceneQueryOptions{}));},[](Scene& s,Vec3 p,Vec3 d,sol::optional<float> distance,sol::optional<PhysicsQueryOptions> o){return s.Raycast(p,d,distance.value_or(10000),o.value_or(PhysicsQueryOptions{}));});
    scene["RaycastAll"]=sol::overload([](Scene& s,SceneRay ray,sol::optional<SceneQueryOptions> o){return sol::as_table(s.RaycastAll(ray,o.value_or(SceneQueryOptions{})));},[](Scene& s,Vec3 p,Vec3 d,sol::optional<float> distance,sol::optional<PhysicsQueryOptions> o){return sol::as_table(s.RaycastAll(p,d,distance.value_or(10000),o.value_or(PhysicsQueryOptions{})));});
    scene["ShapeCast"]=[](Scene& s,PhysicsShape shape,Transform t,Vec3 d,float distance,sol::optional<PhysicsQueryOptions> o){return s.ShapeCast(shape,t,d,distance,o.value_or(PhysicsQueryOptions{}));};
    scene["ShapeCastAll"]=[](Scene& s,PhysicsShape shape,Transform t,Vec3 d,float distance,sol::optional<PhysicsQueryOptions> o){return sol::as_table(s.ShapeCastAll(shape,t,d,distance,o.value_or(PhysicsQueryOptions{})));};
    scene["OverlapShape"]=[](Scene& s,PhysicsShape shape,Transform t,sol::optional<PhysicsQueryOptions> o){return sol::as_table(s.OverlapShape(shape,t,o.value_or(PhysicsQueryOptions{})));};
}
}
