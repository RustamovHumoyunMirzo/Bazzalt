#include "Bazzalt/Scene.h"
#include "Bazzalt/Serialization.h"
#include "Runtime/Engine.h"
#include "Bazzalt/Components/PrimitiveObject.h"
#include "Physics/PhysicsModuleApi.h"
#include "Runtime/GraphicsAccess.h"
#include <cassert>
#include <cmath>
#include <filesystem>
#include <limits>
#include <fstream>
#include <array>
#include "Bazzalt/Components/ModelNode.h"
#include "Bazzalt/AssetManager.h"
using namespace Bazzalt;
static_assert(((BodyConstraints::PositionX|BodyConstraints::PositionY)&BodyConstraints::PositionX)==BodyConstraints::PositionX);
struct CleanupPhysics final:System {
    bool& Cleaned;PhysicsScene::Subscription Id=0;
    explicit CleanupPhysics(bool& cleaned):Cleaned(cleaned){}
    void OnUpdate(Scene&,float) override {}
    void OnCreate(Scene& scene) override {Id=scene.GetPhysics().Subscribe(PhysicsEventType::CollisionEnter,[](const CollisionEvent&){});}
    void OnDestroy(Scene& scene) override {Cleaned=scene.GetPhysics().Unsubscribe(Id);}
};
void CheckAttachedMeshSources(){
    const auto root=std::filesystem::temp_directory_path()/("bazzalt-collider-source-"+UUID::Generate().ToString());std::filesystem::create_directories(root/"Assets");
    struct Cleanup{std::filesystem::path Path;~Cleanup(){std::error_code error;std::filesystem::remove_all(Path,error);}} cleanup{root};
    {std::ofstream file(root/"Test.bproject");file<<"FormatVersion: 1\nProjectUUID: \"12345678-1234-4234-8234-123456789abc\"\nName: ColliderTest\nAssetDirectory: Assets\nStartupScene: \"\"\nProperties: {}\n";}
    {std::ofstream file(root/"Assets/parts.gltf");file<<R"gltf({"asset":{"version":"2.0"},"buffers":[{"uri":"geometry.bin","byteLength":232}],"bufferViews":[{"buffer":0,"byteLength":96},{"buffer":0,"byteOffset":96,"byteLength":72},{"buffer":0,"byteOffset":168,"byteLength":64}],"accessors":[{"bufferView":0,"componentType":5126,"count":8,"type":"VEC3","min":[-0.5,-0.5,-0.5],"max":[0.5,0.5,0.5]},{"bufferView":1,"componentType":5123,"count":36,"type":"SCALAR"},{"bufferView":2,"componentType":5126,"count":8,"type":"VEC2"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"TEXCOORD_0":2},"indices":1}]}],"nodes":[{"name":"Group","children":[1,2]},{"name":"Child A","mesh":0},{"name":"Child B","mesh":0,"translation":[6,0,0]},{"name":"Other branch","mesh":0,"translation":[20,0,0]}],"scenes":[{"nodes":[0,3]}],"scene":0})gltf";}
    {const std::array<float,24> positions{-.5f,-.5f,-.5f,.5f,-.5f,-.5f,.5f,.5f,-.5f,-.5f,.5f,-.5f,-.5f,-.5f,.5f,.5f,-.5f,.5f,.5f,.5f,.5f,-.5f,.5f,.5f};const std::array<std::uint16_t,36> indices{0,2,1,0,3,2,4,5,6,4,6,7,0,4,7,0,7,3,1,2,6,1,6,5,0,1,5,0,5,4,3,7,6,3,6,2};std::ofstream file(root/"Assets/geometry.bin",std::ios::binary);file.write(reinterpret_cast<const char*>(positions.data()),sizeof(positions));file.write(reinterpret_cast<const char*>(indices.data()),sizeof(indices));const std::array<float,16> uv{0,0,1,0,1,1,0,1,0,0,1,0,1,1,0,1};file.write(reinterpret_cast<const char*>(uv.data()),sizeof(uv));}
    Runtime::Engine engine;assert(engine.LoadProject(root/"Test.bproject",false));auto asset=AssetManager::GetAsset(root/"Assets/parts.gltf");assert(asset);auto& scene=engine.GetScene();scene.InstantiateModel(asset->Id);
    auto group=scene.FindEntityByName("Group"),first=scene.FindEntityByName("Child A");assert(group&&first);auto world=scene.GetPhysics();
    first.AddComponent<MeshCollider>();assert(world.Raycast({0,5,0},{0,-1,0},10).Object==first);assert(!world.Raycast({6,5,0},{0,-1,0},10));assert(!world.Raycast({20,5,0},{0,-1,0},10));
    auto firstLines=Runtime::PhysicsModule().Guides(scene,first.GetUUID());assert(!firstLines.empty());for(auto& line:firstLines){assert(line[0]>=-.51f&&line[0]<=.51f);assert(line[3]>=-.51f&&line[3]<=.51f);}
    first.SetComponentEnabled<MeshCollider>(false);auto& collider=group.AddComponent<MeshCollider>();collider.Convex=false;
    assert(world.Raycast({0,5,0},{0,-1,0},10).Object==group);assert(world.Raycast({6,5,0},{0,-1,0},10).Object==group);assert(!world.Raycast({3,5,0},{0,-1,0},10));assert(!world.Raycast({20,5,0},{0,-1,0},10));
    collider.Convex=true;assert(world.Raycast({3,5,0},{0,-1,0},10).Object==group);
    for(int i=0;i<10;++i){collider.Friction=.1f+i*.01f;world.Synchronize();assert(world.Raycast({3,5,0},{0,-1,0},10).Object==group);}
    collider.Convex=false;collider.MeshAsset=asset->Id;assert(world.Raycast({6,5,0},{0,-1,0},10).Object==group);assert(!world.Raycast({20,5,0},{0,-1,0},10));collider.MeshAsset={};assert(!world.Raycast({20,5,0},{0,-1,0},10));
    group.GetComponent<Transform>().Position={100,0,0};assert(world.Raycast({106,5,0},{0,-1,0},10).Object==group);assert(!world.Raycast({6,5,0},{0,-1,0},10));
    // Relative subtree edits invalidate cooked geometry without pulling in siblings.
    scene.FindEntityByName("Child B").GetComponent<Transform>().Position.X=8;assert(world.Raycast({108,5,0},{0,-1,0},10).Object==group);assert(!world.Raycast({106,5,0},{0,-1,0},10));
    first.AddComponent<Rigidbody>();assert(!world.Raycast({100,5,0},{0,-1,0},10));assert(world.Raycast({108,5,0},{0,-1,0},10).Object==group);first.RemoveComponent<Rigidbody>();assert(world.Raycast({100,5,0},{0,-1,0},10).Object==group);
    auto empty=scene.CreateEntity("Awaiting geometry");empty.AddComponent<MeshCollider>();world.Synchronize();assert(!world.Raycast({200,5,0},{0,-1,0},10));
    first.RemoveComponent<MeshCollider>();group.RemoveComponent<MeshCollider>();first.AddComponent<Cloth>();world.Synchronize();assert(world.GetSoftBodyVertices(first).size()==8);for(auto p:world.GetSoftBodyVertices(first))assert(p.X>=99.49f&&p.X<=100.51f);Runtime::GraphicsAccess::BindScene(&scene);Runtime::GraphicsAccess::BeginFrame();assert(Runtime::GraphicsAccess::Commands().size()==1);const auto& draw=Runtime::GraphicsAccess::Commands().front();assert(draw.Source==first.GetUUID());assert(std::any_of(draw.Geometry->Vertices.begin(),draw.Geometry->Vertices.end(),[](auto& v){return v.UV.X>.9f&&v.UV.Y>.9f;}));Runtime::GraphicsAccess::EndFrame();Runtime::GraphicsAccess::BindScene(nullptr);
    first.RemoveComponent<Cloth>();group.AddComponent<Cloth>();world.Synchronize();assert(world.GetSoftBodyVertices(group).size()==16);for(auto p:world.GetSoftBodyVertices(group))assert(p.X<109);auto second=scene.FindEntityByName("Child B");second.AddComponent<ShapeCollider>();world.Synchronize();assert(world.Raycast({108,5,0},{0,-1,0},10).Object==group);group.SetComponentEnabled<Cloth>(false);world.Synchronize();assert(world.Raycast({108,5,0},{0,-1,0},10).Object==second);

}
void CheckJointsAndSoftBodies(){
    Scene scene;auto settings=scene.GetPhysicsSettings();settings.AutoSimulation=false;settings.WorkerThreads=0;scene.SetPhysicsSettings(settings);auto world=scene.GetPhysics();
    auto make=[&](const char* name,Vec3 p){auto e=scene.CreateEntity(name);e.GetComponent<Transform>().Position=p;e.AddComponent<ShapeCollider>().Size={.2f,.2f,.2f};e.AddComponent<Rigidbody>();return e;};
    auto fixed=make("Fixed",{0,4,0});fixed.AddComponent<FixedJoint>();
    auto distance=make("Distance",{3,4,0});auto& d=distance.AddComponent<DistanceJoint>();d.AutoConfigureConnectedAnchor=false;d.ConnectedAnchor={3,5,0};d.MinDistance=d.MaxDistance=1;
    auto hinge=make("Hinge",{6,4,0});auto& h=hinge.AddComponent<HingeJoint>();h.Motor=JointMotorMode::Velocity;h.MotorVelocity=1;
    auto slider=make("Slider",{9,4,0});slider.AddComponent<SliderJoint>();
    auto ball=make("Ball",{12,4,0});ball.AddComponent<BallAndSocketJoint>();
    auto cloth=scene.CreateEntity("Cloth");cloth.GetComponent<Transform>().Position={20,5,0};auto& plane=cloth.AddComponent<PrimitiveObject>();plane.Shape=PrimitiveShape::Plane;plane.Width=plane.Depth=2;plane.Segments=plane.Rings=4;cloth.GetComponent<Transform>().Rotation=Quaternion::FromAxisAngle({1,0,0},-Pi*.5f);auto& c=cloth.AddComponent<Cloth>();c.SegmentsX=c.SegmentsY=4;c.Wind={0,0,2};
    auto rope=scene.CreateEntity("Rope");rope.GetComponent<Transform>().Position={25,5,0};auto& r=rope.AddComponent<Rope>();r.Segments=8;r.Sides=4;r.Wind={1,0,0};
    Runtime::PhysicsModule().Start(scene);assert(world.GetBodyCount()==7);auto before=world.GetSoftBodyVertices(cloth);auto ropeBefore=world.GetSoftBodyVertices(rope);assert(before.size()==25&&ropeBefore.size()==36);PhysicsQueryOptions clothRay;clothRay.IncludeBackFaces=true;assert(world.Raycast({20,5,3},{0,0,-1},6,clothRay).Object==cloth);
    for(int i=0;i<60;++i)world.Simulate(1.f/60);
    assert((fixed.GetWorldTransform().Position-Vec3{0,4,0}).Length()<.01f);
    assert(std::abs((distance.GetWorldTransform().Position-Vec3{3,5,0}).Length()-1)<.03f);
    assert(std::abs(slider.GetWorldTransform().Position.Y-4)<.01f);
    assert(std::abs(ball.GetWorldTransform().Position.Y-4)<.01f);
    assert(world.GetAngularVelocity(hinge).Length()>.1f);auto beforeMotor=world.GetAngularVelocity(hinge).Y;h.MotorVelocity=-1;world.Simulate(1.f/60);assert(world.GetAngularVelocity(hinge).Y*beforeMotor<0);
    auto breakable=make("Breakable",{40,5,0});breakable.AddComponent<FixedJoint>().BreakForce=.01f;world.Simulate(1.f/60);assert(world.IsJointBroken(breakable,JointType::Fixed));
    auto after=world.GetSoftBodyVertices(cloth);c.Damping=.2f;c.Wind={0,0,1};world.Synchronize();assert((world.GetSoftBodyVertices(cloth).back()-after.back()).Length()<.0001f);assert((after.back()-before.back()).Length()<.001f);assert((after[0]-before[0]).Length()>.05f);
    auto ropeAfter=world.GetSoftBodyVertices(rope);assert((ropeAfter[0]-ropeBefore[0]).Length()<.001f);assert(ropeAfter.back().X>ropeBefore.back().X+.05f);
    assert(world.ApplySoftBodyImpulse(cloth,{0,0,3}));assert(world.SetSoftBodyVertexPinned(cloth,0,true));auto pin=world.GetSoftBodyVertices(cloth)[0];world.Simulate(1.f/60);assert((world.GetSoftBodyVertices(cloth)[0]-pin).Length()<.001f);assert(world.SetSoftBodyVertexPinned(cloth,0,false));assert(!world.SetSoftBodyVertexPinned(cloth,99999,true));
    assert(world.BreakJoint(fixed,JointType::Fixed));assert(world.IsJointBroken(fixed,JointType::Fixed));for(int i=0;i<10;++i)world.Simulate(1.f/60);assert(fixed.GetWorldTransform().Position.Y<3.95f);assert(world.RepairJoint(fixed,JointType::Fixed));assert(!world.IsJointBroken(fixed,JointType::Fixed));
    // Body recreation must detach and rebind every referencing native constraint.
    fixed.GetComponent<Rigidbody>().Mass=2;world.Simulate(1.f/60);
    auto connected=make("Connected",{15,4,0});auto& j=connected.AddComponent<FixedJoint>();j.ConnectedBody=ball.GetUUID();world.Simulate(1.f/60);scene.DestroyEntity(ball);world.Simulate(1.f/60);
    cloth.SetComponentEnabled<Cloth>(false);world.Synchronize();assert(world.GetSoftBodyVertices(cloth).empty());cloth.SetComponentEnabled<Cloth>(true);world.Synchronize();assert(world.GetSoftBodyVertices(cloth).size()==25);
    auto path=std::filesystem::temp_directory_path()/("bazzalt-extras-"+UUID::Generate().ToString()+".bscene");Runtime::Engine host;assert(host.SaveSceneAsset(scene,path));auto loaded=host.LoadSceneAsset(path);std::filesystem::remove(path);assert(loaded);assert(loaded->FindEntityByName("Cloth").GetComponent<Cloth>().SegmentsX==4);assert(loaded->FindEntityByName("Connected").GetComponent<FixedJoint>().ConnectedBody==j.ConnectedBody);assert(loaded->FindEntityByName("Rope").GetComponent<Rope>().Sides==4);
    auto floor=scene.CreateEntity("Soft floor");floor.GetComponent<Transform>().Position={30,0,0};floor.AddComponent<ShapeCollider>().Size={4,.2f,4};
    auto falling=scene.CreateEntity("Falling cloth");falling.GetComponent<Transform>().Position={30,2,0};auto& freePlane=falling.AddComponent<PrimitiveObject>();freePlane.Shape=PrimitiveShape::Plane;freePlane.Segments=3;freePlane.Rings=2;auto& free=falling.AddComponent<Cloth>();free.SegmentsX=free.SegmentsY=2;free.PinEdges=0;
    for(int i=0;i<120;++i)world.Simulate(1.f/60);for(auto point:world.GetSoftBodyVertices(falling))assert(point.Y>-.1f);
    Runtime::PhysicsModule().Stop(scene);
}
void CheckClothSourceShape(){
    Scene scene;auto settings=scene.GetPhysicsSettings();settings.AutoSimulation=false;settings.WorkerThreads=0;settings.Gravity={};scene.SetPhysicsSettings(settings);
    auto cube=scene.CreateEntity("Soft cube");cube.AddComponent<PrimitiveObject>();auto& cloth=cube.AddComponent<Cloth>();cloth.PinEdges=0;cloth.Compliance=.01f;cloth.BendCompliance=.01f;
    Runtime::PhysicsModule().Start(scene);auto physics=scene.GetPhysics();auto points=physics.GetSoftBodyVertices(cube);assert(points.size()==8);float minZ=1,maxZ=-1;for(auto p:points){minZ=std::min(minZ,p.Z);maxZ=std::max(maxZ,p.Z);}assert(minZ<-.49f&&maxZ>.49f);
    assert(physics.ApplySoftBodyImpulse(cube,{0,0,1},0));physics.Simulate(1.f/60);auto deformed=physics.GetSoftBodyVertices(cube);assert((deformed[0]-points[0]).Length()>.001f);
    Runtime::PhysicsModule().Stop(scene);
}
void CheckClothGravityAndPins(){
    Scene scene;auto settings=scene.GetPhysicsSettings();settings.AutoSimulation=false;settings.WorkerThreads=0;assert(scene.SetPhysicsSettings(settings));
    auto make=[&](const char* name,float x){auto e=scene.CreateEntity(name);e.GetComponent<Transform>().Position={x,5,0};e.AddComponent<PrimitiveObject>();e.AddComponent<Cloth>();return e;};
    auto pinned=make("Pinned",0),floating=make("Zero gravity",10);floating.GetComponent<Cloth>().PinEdges=0;floating.GetComponent<Cloth>().GravityFactor=0;
    Runtime::PhysicsModule().Start(scene);auto physics=scene.GetPhysics();auto anchor=physics.GetSoftBodyVertices(pinned),zero=physics.GetSoftBodyVertices(floating);
    for(int i=0;i<30;++i)physics.Simulate(1.f/60);
    auto held=physics.GetSoftBodyVertices(pinned);for(std::size_t i=0;i<anchor.size();++i)if(anchor[i].Y>5.49f)assert((held[i]-anchor[i]).Length()<.001f);
    for(auto point:physics.GetSoftBodyVertices(floating))assert(std::abs(point.Y-4.5f)<.001f||std::abs(point.Y-5.5f)<.001f);
    pinned.GetComponent<Cloth>().PinEdges=0;physics.Synchronize();auto released=physics.GetSoftBodyVertices(pinned);
    for(int i=0;i<30;++i)physics.Simulate(1.f/60);
    auto falling=physics.GetSoftBodyVertices(pinned);float before=0,after=0;for(auto point:released)before+=point.Y;for(auto point:falling)after+=point.Y;assert((before-after)/falling.size()>.8f);
    floating.GetComponent<Cloth>().GravityFactor=1;for(int i=0;i<30;++i)physics.Simulate(1.f/60);float moved=0;auto affected=physics.GetSoftBodyVertices(floating);for(std::size_t i=0;i<zero.size();++i)moved+=zero[i].Y-affected[i].Y;assert(moved/zero.size()>.8f);
    Runtime::PhysicsModule().Stop(scene);
}
int main(){
    Rigidbody damping;damping.LinearDamping=10;damping.AngularDamping=10;assert(damping.IsValid());
    Scene scene;auto settings=scene.GetPhysicsSettings();settings.AutoSimulation=false;settings.WorkerThreads=0;assert(scene.SetPhysicsSettings(settings));
    auto ground=scene.CreateEntity("Ground");ground.GetComponent<Transform>().Position={0,-.5f,0};ground.AddComponent<ShapeCollider>().Size={20,1,20};
    auto cube=scene.CreateEntity("Cube");cube.GetComponent<Transform>().Position={0,3,0};cube.AddComponent<ShapeCollider>();cube.AddComponent<Rigidbody>();
    auto world=scene.GetPhysics();assert(world.GetBodyCount()==2);auto ray=scene.Raycast(Vec3{0,10,0},Vec3{0,-1,0},20);assert(ray&&ray.Object==cube&&std::abs(ray.Distance-6.5f)<.01f);
    PhysicsQueryOptions onlyGround;onlyGround.ExcludedEntities={cube.GetUUID()};auto floor=world.Raycast({0,10,0},{0,-1,0},20,onlyGround);assert(floor&&floor.Object==ground);
    Runtime::PhysicsModule().Start(scene);int enter=0,stay=0,points=0;
    auto id=world.Subscribe(PhysicsEventType::CollisionEnter,[&](const CollisionEvent& event){++enter;points+=int(event.Contacts.size());assert(event.Normal.LengthSquared()>.9f);},cube);
    auto stayId=world.Subscribe(PhysicsEventType::CollisionStay,[&](const CollisionEvent&){++stay;},cube);
    for(int i=0;i<180;++i)world.Simulate(1.f/60);
    assert(cube.GetWorldTransform().Position.Y>.45f&&cube.GetWorldTransform().Position.Y<.55f);assert(enter>0&&points>0&&stay>0);
    assert(world.SetGravity(cube,settings.Gravity));for(int i=0;i<180;++i)world.Simulate(1.f/60);assert(!world.IsAwake(cube));assert(world.SetGravity(cube,std::nullopt));
    assert(world.ApplyImpulse(cube,{0,3,0}));assert(world.GetLinearVelocity(cube).Y>2.9f);world.Simulate(1.f/60);assert(cube.GetWorldTransform().Position.Y>.5f);
    assert(world.SetGravity(cube,Vec3{}));assert(world.GetGravity(cube).has_value());assert(world.SetLinearVelocity(cube,{}));world.Simulate(1.f/60);assert(std::abs(world.GetLinearVelocity(cube).Y)<.01f);assert(world.SetGravity(cube,std::nullopt));
    assert(world.Teleport(cube,{0,3,0},{}));assert(world.SetLinearVelocity(cube,{}));world.SetLayerCollision(0,1,false);ground.GetComponent<ShapeCollider>().Layer=1;
    for(int i=0;i<90;++i)world.Simulate(1.f/60);assert(cube.GetWorldTransform().Position.Y<0);
    PhysicsShape sphere;sphere.Radius=.1f;Transform cast;cast.Position={0,5,0};auto hit=world.ShapeCast(sphere,cast,{0,-1,0},10,onlyGround);assert(hit&&hit.Object==ground);
    auto overlaps=world.OverlapBox({0,-.5f,0},{1,1,1});assert(!overlaps.empty());
    auto trigger=scene.CreateEntity("Trigger");auto& tc=trigger.AddComponent<ShapeCollider>();tc.IsTrigger=true;tc.Size={2,2,2};trigger.GetComponent<Transform>().Position={5,0,0};
    auto mover=scene.CreateEntity("Mover");mover.GetComponent<Transform>().Position={5,0,0};mover.AddComponent<ShapeCollider>();auto& body=mover.AddComponent<Rigidbody>();body.Gravity=Vec3{};
    int triggers=0;world.Subscribe(PhysicsEventType::TriggerEnter,[&](const CollisionEvent& e){assert(e.ColliderA==trigger||e.ColliderB==trigger);++triggers;},mover);
    world.Simulate(1.f/60);assert(triggers>0);
    PhysicsQueryOptions query;auto excludedTrigger=world.Raycast({5,5,0},{0,-1,0},10,query);assert(excludedTrigger&&excludedTrigger.Object==mover);query.IncludeTriggers=true;assert(world.RaycastAll({5,5,0},{0,-1,0},10,query).size()>=2);
    auto mesh=scene.CreateEntity("Mesh");mesh.GetComponent<Transform>().Position={10,0,0};mesh.AddComponent<PrimitiveObject>();mesh.AddComponent<MeshCollider>().Convex=false;
    assert(world.Raycast({10,5,0},{0,-1,0},10));
    mover.SetComponentEnabled<Rigidbody>(false);world.Synchronize();query.IncludeTriggers=false;query.LayerMask=1;assert(!world.Raycast({5,5,0},{0,-1,0},10,query));
    bool invalid=false;try{world.Simulate(std::numeric_limits<float>::quiet_NaN());}catch(const std::invalid_argument&){invalid=true;}assert(invalid);
    assert(world.Unsubscribe(id));assert(world.Unsubscribe(stayId));
    auto path=std::filesystem::temp_directory_path()/"bazzalt-physics-test.bscene";Runtime::Engine host;assert(host.SaveSceneAsset(scene,path));auto loaded=host.LoadSceneAsset(path);assert(loaded);assert(!loaded->GetPhysicsSettings().AutoSimulation);assert(loaded->FindEntityByName("Cube").GetComponent<Rigidbody>().Mass==1);assert(loaded->FindEntityByName("Trigger").GetComponent<ShapeCollider>().IsTrigger);std::filesystem::remove(path);
    scene.DestroyEntity(mesh);world.Synchronize();assert(!world.Raycast({10,5,0},{0,-1,0},10,query));
    auto empty=scene.CreateEntity("No collider");empty.GetComponent<Transform>().Position={50,10,0};empty.AddComponent<Rigidbody>();world.Simulate(1.f/60);assert(empty.GetWorldTransform().Position.Y<10);
    {auto temporary=std::make_unique<Scene>();auto stale=temporary->GetPhysics();temporary.reset();bool rejected=false;try{stale.GetBodyCount();}catch(const std::logic_error&){rejected=true;}assert(rejected);}
    bool cleaned=false;{Scene temporary;temporary.AddSystem<CleanupPhysics>(cleaned);}assert(cleaned);
    Runtime::PhysicsModule().Stop(scene);assert(!world.IsRunning());scene.Clear();assert(scene.GetPhysicsSettings().AutoSimulation);
    CheckAttachedMeshSources();CheckJointsAndSoftBodies();CheckClothSourceShape();CheckClothGravityAndPins();
}
