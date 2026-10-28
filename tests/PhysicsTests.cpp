#include "Bazzalt/Scene.h"
#include "Bazzalt/Serialization.h"
#include "Runtime/Engine.h"
#include "Bazzalt/Components/PrimitiveObject.h"
#include "Physics/PhysicsModuleApi.h"
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
    {std::ofstream file(root/"Assets/parts.gltf");file<<R"gltf({"asset":{"version":"2.0"},"buffers":[{"uri":"geometry.bin","byteLength":168}],"bufferViews":[{"buffer":0,"byteLength":96},{"buffer":0,"byteOffset":96,"byteLength":72}],"accessors":[{"bufferView":0,"componentType":5126,"count":8,"type":"VEC3","min":[-0.5,-0.5,-0.5],"max":[0.5,0.5,0.5]},{"bufferView":1,"componentType":5123,"count":36,"type":"SCALAR"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],"nodes":[{"name":"Group","children":[1,2]},{"name":"Child A","mesh":0},{"name":"Child B","mesh":0,"translation":[6,0,0]},{"name":"Other branch","mesh":0,"translation":[20,0,0]}],"scenes":[{"nodes":[0,3]}],"scene":0})gltf";}
    {const std::array<float,24> positions{-.5f,-.5f,-.5f,.5f,-.5f,-.5f,.5f,.5f,-.5f,-.5f,.5f,-.5f,-.5f,-.5f,.5f,.5f,-.5f,.5f,.5f,.5f,.5f,-.5f,.5f,.5f};const std::array<std::uint16_t,36> indices{0,2,1,0,3,2,4,5,6,4,6,7,0,4,7,0,7,3,1,2,6,1,6,5,0,1,5,0,5,4,3,7,6,3,6,2};std::ofstream file(root/"Assets/geometry.bin",std::ios::binary);file.write(reinterpret_cast<const char*>(positions.data()),sizeof(positions));file.write(reinterpret_cast<const char*>(indices.data()),sizeof(indices));}
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
    CheckAttachedMeshSources();
}
