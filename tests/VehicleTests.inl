// Native vehicle regressions use actual Jolt contacts, drivetrain torque and brakes.
static void CheckVehicles() {
    Scene scene;auto settings=scene.GetPhysicsSettings();settings.AutoSimulation=false;settings.WorkerThreads=0;scene.SetPhysicsSettings(settings);
    auto ground=scene.CreateEntity("Vehicle ground");ground.GetComponent<Transform>().Position={0,-.5f,0};ground.AddComponent<ShapeCollider>().Size={200,1,200};
    auto car=scene.CreateEntity("Composed car");car.GetComponent<Transform>().Position={0,1.2f,0};car.AddComponent<ShapeCollider>().Size={1.6f,.4f,3};
    auto& rb=car.AddComponent<Rigidbody>();rb.Mass=1200;rb.CenterOfMassOffset={0,-.15f,0};car.AddComponent<Vehicle>();car.AddComponent<VehicleEngine>();car.AddComponent<VehicleTransmission>();car.AddComponent<VehicleAerodynamics>();
    std::array<Entity,4> wheels;
    for(unsigned i=0;i<4;++i) {
        auto e=scene.CreateEntity("Wheel "+std::to_string(i));assert(e.SetParent(car));wheels[i]=e;
        e.AddComponent<VehicleWheel>().Position={(i&1)?.9f:-.9f,0,i<2?1.2f:-1.2f};e.AddComponent<WheelSuspension>();e.AddComponent<WheelTire>();e.AddComponent<WheelBrake>();
        if(i<2)e.AddComponent<WheelSteering>();else e.GetComponent<WheelBrake>().HandbrakeTorque=2500;
    }
    auto visualCollider=scene.CreateEntity("Wheel visual collider");visualCollider.SetParent(wheels[0],false);visualCollider.GetComponent<Transform>().Position={30,0,0};visualCollider.AddComponent<ShapeCollider>();
    auto& differential=car.AddComponent<VehicleDifferential>();differential.LeftWheel=wheels[2].GetUUID();differential.RightWheel=wheels[3].GetUUID();
    auto axle=scene.CreateEntity("Front anti-roll");axle.SetParent(car);auto& bar=axle.AddComponent<VehicleAntiRollBar>();bar.LeftWheel=wheels[0].GetUUID();bar.RightWheel=wheels[1].GetUUID();
    Runtime::PhysicsModule().Start(scene);auto physics=scene.GetPhysics();assert(!physics.GetVehicleState(ground));assert(!physics.GetVehicleWheelState(car));
    PhysicsQueryOptions noGround;noGround.ExcludedEntities={ground.GetUUID()};assert(!physics.Raycast(visualCollider.GetWorldTransform().Position+Vec3{0,2,0},{0,-1,0},4,noGround));
    for(int i=0;i<120;++i)physics.Simulate(1.f/60);
    auto state=physics.GetVehicleState(car);assert(state&&state->WheelCount==4&&state->GroundedWheels==4);assert(state->EngineRPM>=790);
    auto wheelState=physics.GetVehicleWheelState(wheels[0]);assert(wheelState&&wheelState->Grounded&&wheelState->ContactNormal.Y>.9f&&wheelState->SuspensionForce>0);
    assert((wheels[0].GetWorldTransform().Position-wheelState->Position).Length()<.001f);
    for(auto mode:{VehicleCollisionMode::Ray,VehicleCollisionMode::CylinderCast,VehicleCollisionMode::SphereCast}) {
        car.GetComponent<Vehicle>().CollisionTest=mode;physics.Synchronize();for(int i=0;i<60;++i)physics.Simulate(1.f/60);assert(physics.GetVehicleState(car)->GroundedWheels==4);
    }
    assert(physics.SetVehicleInput(car,1,0));for(int i=0;i<240;++i)physics.Simulate(1.f/60);
    const auto speed=physics.GetVehicleState(car)->Speed;std::fprintf(stderr,"vehicle speed %f\n",speed);assert(speed>2&&car.GetWorldTransform().Position.Z>2);
    assert(physics.SetVehicleInput(car,0,0,1));for(int i=0;i<150;++i)physics.Simulate(1.f/60);assert(std::abs(physics.GetVehicleState(car)->Speed)<speed*.6f);
    bool rejected=false;try{physics.SetVehicleInput(car,std::numeric_limits<float>::quiet_NaN(),0);}catch(const std::invalid_argument&){rejected=true;}assert(rejected);
    assert(!physics.SetVehicleGear(car,2));car.GetComponent<VehicleTransmission>().Automatic=false;physics.Synchronize();assert(physics.SetVehicleGear(car,2,.7f));assert(!physics.SetVehicleGear(car,7));physics.Simulate(1.f/60);assert(physics.GetVehicleState(car)->Gear==2);
    car.GetComponent<VehicleTransmission>().GearCount=10;physics.Synchronize();assert(physics.SetVehicleGear(car,10));physics.Simulate(1.f/60);assert(physics.GetVehicleState(car)->Gear==10);
    car.GetComponent<VehicleTransmission>().GearCount=6;physics.Synchronize();physics.Simulate(1.f/60);assert(physics.GetVehicleState(car)->Gear==6);assert(physics.SetVehicleGear(car,2));
    car.GetComponent<VehicleEngine>().Enabled=false;physics.Synchronize();assert(physics.SetVehicleInput(car,1,0));physics.Simulate(1.f/60);assert(physics.GetVehicleState(car)->EngineRPM==0&&physics.GetVehicleState(car)->Gear==0);assert(!physics.SetVehicleGear(car,2));car.GetComponent<VehicleEngine>().Enabled=true;
    car.GetComponent<VehicleTransmission>().Enabled=false;physics.Synchronize();physics.Simulate(1.f/60);assert(physics.GetVehicleState(car)->Gear==0&&physics.GetVehicleState(car)->Clutch==0);assert(!physics.SetVehicleGear(car,2));car.GetComponent<VehicleTransmission>().Enabled=true;physics.Synchronize();
    car.GetComponent<VehicleDifferential>().Enabled=false;physics.Synchronize();assert(physics.GetVehicleState(car));car.GetComponent<VehicleDifferential>().Enabled=true;physics.Synchronize();
    car.GetComponent<Rigidbody>().Enabled=false;physics.Synchronize();assert(!physics.GetVehicleState(car));car.GetComponent<Rigidbody>().Enabled=true;physics.Synchronize();
    car.GetComponent<Vehicle>().Enabled=false;physics.Synchronize();assert(!physics.GetVehicleState(car));car.GetComponent<Vehicle>().Enabled=true;physics.Synchronize();assert(physics.GetVehicleState(car));
    assert(physics.SetVehicleGear(car,2,.7f));physics.Simulate(1.f/60);
    // Recooking a chassis must detach its vehicle constraint and step listener first.
    car.GetComponent<ShapeCollider>().Size.X=1.7f;physics.Synchronize();physics.Simulate(1.f/60);assert(physics.GetVehicleState(car)->WheelCount==4&&physics.GetVehicleState(car)->Gear==2);
    auto oldForward=car.GetWorldTransform().Rotation.Rotate({0,0,1});assert(physics.SetVehicleInput(car,1,.7f));for(int i=0;i<180;++i)physics.Simulate(1.f/60);assert((car.GetWorldTransform().Rotation.Rotate({0,0,1})-oldForward).Length()>.05f);
    assert(!Runtime::PhysicsModule().Guides(scene,car.GetUUID()).empty());
    wheels[0].GetComponent<VehicleWheel>().Enabled=false;bar.Enabled=false;physics.Synchronize();assert(physics.GetVehicleState(car)->WheelCount==3);assert(!physics.GetVehicleWheelState(wheels[0]));
    const auto originalLeft=differential.LeftWheel;differential.LeftWheel=UUID::Generate();bool invalidReference=false;try{physics.Synchronize();}catch(const std::invalid_argument&){invalidReference=true;}assert(invalidReference);differential.LeftWheel=originalLeft;physics.Synchronize();
    Runtime::PhysicsModule().Stop(scene);assert(!physics.GetVehicleState(car));
    Runtime::Engine host;auto path=std::filesystem::temp_directory_path()/("vehicle-"+UUID::Generate().ToString()+".bscene");assert(host.SaveSceneAsset(scene,path));auto loaded=host.LoadSceneAsset(path);std::filesystem::remove(path);assert(loaded);auto saved=loaded->FindEntityByName("Composed car");assert(saved.GetComponent<VehicleDifferential>().RightWheel==wheels[3].GetUUID());assert(saved.GetComponent<VehicleTransmission>().GearCount==6);
}
