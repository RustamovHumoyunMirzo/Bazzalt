// Included inside the private physics namespace after World and the query helpers.
template<class T> const T* VehiclePart(Entity entity) {
    auto* value=entity.TryGetComponent<T>();
    if(!value||!value->Enabled)return nullptr;
    if(!value->IsValid())throw std::invalid_argument("Invalid vehicle component on "+entity.GetUUID().ToString());
    return value;
}
static Entity VehicleOwner(Entity entity) {
    for(auto current=entity;current;current=current.GetParent()) {
        if(current.HasComponent<Vehicle>())return current;
        if(current.HasComponent<Rigidbody>())return {};
    }
    return {};
}
template<class T> static void VehicleSignature(Entity entity,std::string& key) {
    const auto* value=VehiclePart<T>(entity);AppendPhysicsKey(key,value!=nullptr);
    if(value) {
        if constexpr(std::is_same_v<T,Vehicle>)VisitVehicle(*value,[&](const char*,const char*,const auto& v){AppendPhysicsKey(key,v);});
        else if constexpr(std::is_same_v<T,VehicleWheel>)VisitVehicleWheel(*value,[&](const char*,const char*,const auto& v){AppendPhysicsKey(key,v);});
        else if constexpr(std::is_same_v<T,WheelSuspension>)VisitWheelSuspension(*value,[&](const char*,const char*,const auto& v){AppendPhysicsKey(key,v);});
        else if constexpr(std::is_same_v<T,WheelTire>)VisitWheelTire(*value,[&](const char*,const char*,const auto& v){AppendPhysicsKey(key,v);});
        else if constexpr(std::is_same_v<T,WheelSteering>)VisitWheelSteering(*value,[&](const char*,const char*,const auto& v){AppendPhysicsKey(key,v);});
        else if constexpr(std::is_same_v<T,WheelBrake>)VisitWheelBrake(*value,[&](const char*,const char*,const auto& v){AppendPhysicsKey(key,v);});
        else if constexpr(std::is_same_v<T,VehicleEngine>)VisitVehicleEngine(*value,[&](const char*,const char*,const auto& v){AppendPhysicsKey(key,v);});
        else if constexpr(std::is_same_v<T,VehicleTransmission>)VisitVehicleTransmission(*value,[&](const char*,const char*,const auto& v){AppendPhysicsKey(key,v);});
        else if constexpr(std::is_same_v<T,VehicleDifferential>)VisitVehicleDifferential(*value,[&](const char*,const char*,const auto& v){AppendPhysicsKey(key,v);});
        else if constexpr(std::is_same_v<T,VehicleAntiRollBar>)VisitVehicleAntiRollBar(*value,[&](const char*,const char*,const auto& v){AppendPhysicsKey(key,v);});
    }
}
void World::DetachVehicle(JPH::BodyID body) {
    for(auto it=Vehicles.begin();it!=Vehicles.end();) {
        if(it->second.Body!=body){++it;continue;}
        PendingVehicleInput[it->first]=it->second;
        System.RemoveStepListener(it->second.Native);System.RemoveConstraint(it->second.Native);
        it=Vehicles.erase(it);
    }
}
void World::SyncVehicles() {
    if(!Running)return;
    // Reject misplaced active parts instead of silently accepting a nonfunctional setup.
    const auto checkParts=[&]<class T>(bool chassisOnly,bool wheelOnly) {
        for(auto h:ScenePtr->GetRegistry().view<T>()) {
            auto e=ScenePtr->GetEntity(static_cast<Entity::Id>(h));if(!e.GetComponent<T>().Enabled)continue;
            if((chassisOnly&&!e.HasComponent<Vehicle>())||(wheelOnly&&!e.HasComponent<VehicleWheel>())||!VehicleOwner(e))
                throw std::invalid_argument("Vehicle part has no compatible chassis/wheel owner: "+e.GetUUID().ToString());
        }
    };
    checkParts.template operator()<VehicleEngine>(true,false);checkParts.template operator()<VehicleTransmission>(true,false);checkParts.template operator()<VehicleAerodynamics>(true,false);
    checkParts.template operator()<VehicleWheel>(false,true);checkParts.template operator()<WheelSuspension>(false,true);checkParts.template operator()<WheelTire>(false,true);checkParts.template operator()<WheelBrake>(false,true);checkParts.template operator()<WheelSteering>(false,true);
    checkParts.template operator()<VehicleDifferential>(false,false);checkParts.template operator()<VehicleAntiRollBar>(false,false);
    // Group once per world synchronization, not once per chassis (traffic scales linearly).
    const auto gather=[&]<class T>() {
        std::map<UUID,std::vector<Entity>> groups;
        for(auto h:ScenePtr->GetRegistry().view<T>()) {
            auto e=ScenePtr->GetEntity(static_cast<Entity::Id>(h));if(!VehiclePart<T>(e))continue;
            if constexpr(std::is_same_v<T,VehicleWheel>)if(!VehiclePart<WheelSuspension>(e))continue;
            auto owner=VehicleOwner(e);if(owner)groups[owner.GetUUID()].push_back(e);
        }
        for(auto& [id,items]:groups)std::sort(items.begin(),items.end(),[](Entity a,Entity b){return a.GetUUID()<b.GetUUID();});
        return groups;
    };
    auto wheelGroups=gather.template operator()<VehicleWheel>();auto differentialGroups=gather.template operator()<VehicleDifferential>();auto barGroups=gather.template operator()<VehicleAntiRollBar>();
    std::set<UUID> retained;
    for(auto handle:ScenePtr->GetRegistry().view<Vehicle>()) {
        auto root=ScenePtr->GetEntity(static_cast<Entity::Id>(handle));
        const auto* vehicle=VehiclePart<Vehicle>(root);if(!vehicle)continue;
        if(auto* body=root.TryGetComponent<Rigidbody>();body&&!body->Enabled)continue;
        if(!root.GetComponent<Transform>().Enabled)continue;
        if(!VehiclePart<Rigidbody>(root)||root.GetComponent<Rigidbody>().MotionType!=BodyMotionType::Dynamic)
            throw std::invalid_argument("Vehicle requires an enabled dynamic Rigidbody and chassis collider");
        const auto found=Bodies.find(root.GetUUID());if(found==Bodies.end()||found->second.Shapes.empty())throw std::invalid_argument("Vehicle has no chassis collider");
        const auto scale=root.GetWorldTransform().Scale;
        if(scale.X<=0||std::abs(scale.X-scale.Y)>.0001f||std::abs(scale.X-scale.Z)>.0001f)throw std::invalid_argument("Vehicle requires positive uniform world scale");
        std::string signature;VehicleSignature<Vehicle>(root,signature);AppendPhysicsKey(signature,scale.X);AppendPhysicsKey(signature,Settings.Gravity);
        VehicleSignature<VehicleEngine>(root,signature);VehicleSignature<VehicleTransmission>(root,signature);
        auto& wheels=wheelGroups[root.GetUUID()];auto& differentials=differentialGroups[root.GetUUID()];auto& bars=barGroups[root.GetUUID()];
        if(wheels.size()<2||wheels.size()>32)throw std::invalid_argument("Vehicle requires 2 to 32 enabled child wheels with WheelSuspension");
        for(auto wheel:wheels) {
            if(wheel==root||wheel.HasComponent<Rigidbody>()||wheel.HasComponent<ShapeCollider>()||wheel.HasComponent<MeshCollider>())throw std::invalid_argument("VehicleWheel must be a child without its own body or collider");
            AppendPhysicsKey(signature,wheel.GetUUID());VehicleSignature<VehicleWheel>(wheel,signature);VehicleSignature<WheelSuspension>(wheel,signature);
            VehicleSignature<WheelTire>(wheel,signature);VehicleSignature<WheelBrake>(wheel,signature);VehicleSignature<WheelSteering>(wheel,signature);
        }
        for(auto e:differentials){AppendPhysicsKey(signature,e.GetUUID());VehicleSignature<VehicleDifferential>(e,signature);}
        for(auto e:bars){AppendPhysicsKey(signature,e.GetUUID());VehicleSignature<VehicleAntiRollBar>(e,signature);}
        auto old=Vehicles.find(root.GetUUID());retained.insert(root.GetUUID());
        if(old!=Vehicles.end()&&old->second.Signature==signature&&old->second.Body==found->second.Id)continue;
        JPH::VehicleConstraintSettings settings;settings.mUp=V(vehicle->Up.Normalized());settings.mForward=V((vehicle->Forward-vehicle->Up*Vec3::Dot(vehicle->Up,vehicle->Forward)).Normalized());settings.mMaxPitchRollAngle=ToRadians(vehicle->MaxPitchRollAngle);
        JPH::Ref<JPH::WheeledVehicleControllerSettings> controller=new JPH::WheeledVehicleControllerSettings;
        controller->mEngine.mMaxTorque=0; // No engine component makes a passive wheeled trailer.
        if(const auto* engine=VehiclePart<VehicleEngine>(root)) {
            auto& e=controller->mEngine;e.mMaxTorque=engine->MaxTorque;e.mMinRPM=engine->MinRPM;e.mMaxRPM=engine->MaxRPM;e.mInertia=engine->Inertia;e.mAngularDamping=engine->AngularDamping;
            e.mNormalizedTorque.Clear();e.mNormalizedTorque.AddPoint(0,engine->LowTorque);e.mNormalizedTorque.AddPoint(engine->PeakRPMFraction,1);e.mNormalizedTorque.AddPoint(1,engine->HighTorque);
        }
        if(const auto* transmission=VehiclePart<VehicleTransmission>(root)) {
            auto& t=controller->mTransmission;t.mMode=transmission->Automatic?JPH::ETransmissionMode::Auto:JPH::ETransmissionMode::Manual;
            const float gears[]={transmission->Gear1,transmission->Gear2,transmission->Gear3,transmission->Gear4,transmission->Gear5,transmission->Gear6,transmission->Gear7,transmission->Gear8,transmission->Gear9,transmission->Gear10};t.mGearRatios.assign(gears,gears+transmission->GearCount);t.mReverseGearRatios={transmission->ReverseRatio};
            t.mSwitchTime=transmission->SwitchTime;t.mClutchReleaseTime=transmission->ClutchReleaseTime;t.mSwitchLatency=transmission->SwitchLatency;t.mShiftUpRPM=transmission->ShiftUpRPM;t.mShiftDownRPM=transmission->ShiftDownRPM;t.mClutchStrength=transmission->ClutchStrength;
        }
        if(!VehiclePart<VehicleEngine>(root)||!VehiclePart<VehicleTransmission>(root)){controller->mEngine.mMaxTorque=0;controller->mTransmission.mMode=JPH::ETransmissionMode::Manual;}
        if(controller->mTransmission.mMode==JPH::ETransmissionMode::Auto&&(controller->mTransmission.mShiftUpRPM>controller->mEngine.mMaxRPM||controller->mTransmission.mShiftDownRPM<controller->mEngine.mMinRPM))
            throw std::invalid_argument("Transmission shift RPM must lie within the engine RPM range");
        std::map<UUID,int> indices;
        for(auto entity:wheels) {
            const auto& wheel=*VehiclePart<VehicleWheel>(entity);const auto& suspension=*VehiclePart<WheelSuspension>(entity);
            if(vehicle->CollisionTest==VehicleCollisionMode::SphereCast&&vehicle->CollisionRadius>wheel.Radius)throw std::invalid_argument("Vehicle sphere contact radius must not exceed any wheel radius");
            JPH::Ref<JPH::WheelSettingsWV> w=new JPH::WheelSettingsWV;
            w->mPosition=V(wheel.Position*scale.X);w->mWheelUp=V(wheel.Up.Normalized());w->mWheelForward=V(wheel.Forward.Normalized());w->mRadius=wheel.Radius*scale.X;w->mWidth=wheel.Width*scale.X;w->mInertia=wheel.Inertia;w->mAngularDamping=wheel.AngularDamping;RequireVector({w->mRadius,w->mWidth,(suspension.MaxLength+wheel.Radius)*scale.X});
            w->mSuspensionDirection=V(suspension.Direction.Normalized());w->mSuspensionMinLength=suspension.MinLength*scale.X;w->mSuspensionMaxLength=suspension.MaxLength*scale.X;w->mSuspensionPreloadLength=suspension.PreloadLength*scale.X;
            w->mSuspensionSpring.mFrequency=suspension.Frequency;w->mSuspensionSpring.mDamping=suspension.Damping;w->mEnableSuspensionForcePoint=suspension.UseForcePoint;w->mSuspensionForcePoint=V(suspension.ForcePoint*scale.X);
            w->mMaxSteerAngle=0;w->mMaxBrakeTorque=w->mMaxHandBrakeTorque=0;
            if(const auto* steering=VehiclePart<WheelSteering>(entity)){w->mSteeringAxis=V(steering->Axis.Normalized());w->mMaxSteerAngle=ToRadians(steering->MaxAngle);}
            if(const auto* brake=VehiclePart<WheelBrake>(entity)){w->mMaxBrakeTorque=brake->MaxTorque;w->mMaxHandBrakeTorque=brake->HandbrakeTorque;}
            if(const auto* tire=VehiclePart<WheelTire>(entity)) {
                w->mLongitudinalFriction.Clear();w->mLongitudinalFriction.AddPoint(0,0);w->mLongitudinalFriction.AddPoint(tire->LongitudinalPeakSlip,tire->LongitudinalPeakGrip);w->mLongitudinalFriction.AddPoint(tire->LongitudinalTailSlip,tire->LongitudinalTailGrip);
                w->mLateralFriction.Clear();w->mLateralFriction.AddPoint(0,0);w->mLateralFriction.AddPoint(tire->LateralPeakAngle,tire->LateralPeakGrip);w->mLateralFriction.AddPoint(tire->LateralTailAngle,tire->LateralTailGrip);
            }
            indices.emplace(entity.GetUUID(),int(settings.mWheels.size()));settings.mWheels.push_back(w.GetPtr());
        }
        const auto index=[&](UUID id){
            auto it=indices.find(id);if(it!=indices.end())return it->second;
            auto e=ScenePtr->GetEntity(id);if(!e||VehicleOwner(e)!=root||!e.HasComponent<VehicleWheel>()||!e.HasComponent<WheelSuspension>())throw std::invalid_argument("Differential/anti-roll reference must name a wheel of this vehicle");
            if(!e.GetComponent<VehicleWheel>().Enabled||!e.GetComponent<WheelSuspension>().Enabled)return -1;
            throw std::invalid_argument("Unregistered vehicle wheel reference");
        };
        float shares=0;std::set<int> driven;
        for(auto entity:differentials) {
            const auto& diff=*VehiclePart<VehicleDifferential>(entity);JPH::VehicleDifferentialSettings d;
            d.mLeftWheel=index(diff.LeftWheel);d.mRightWheel=index(diff.RightWheel);
            if(diff.LeftWheel==diff.RightWheel||(d.mLeftWheel>=0&&!driven.insert(d.mLeftWheel).second)||(d.mRightWheel>=0&&!driven.insert(d.mRightWheel).second))throw std::invalid_argument("A wheel can belong to only one differential");
            if(d.mLeftWheel<0&&d.mRightWheel<0)continue;
            d.mDifferentialRatio=diff.Ratio;d.mLeftRightSplit=diff.Split;d.mLimitedSlipRatio=diff.LimitedSlipRatio;d.mEngineTorqueRatio=diff.TorqueShare;shares+=diff.TorqueShare;controller->mDifferentials.push_back(d);
        }
        if(!controller->mDifferentials.empty()&&shares<=0)throw std::invalid_argument("Differential torque shares must have a positive total");
        for(auto& d:controller->mDifferentials)d.mEngineTorqueRatio/=shares;

        for(auto entity:bars) {
            const auto& bar=*VehiclePart<VehicleAntiRollBar>(entity);JPH::VehicleAntiRollBar b;b.mLeftWheel=index(bar.LeftWheel);b.mRightWheel=index(bar.RightWheel);b.mStiffness=bar.Stiffness;
            if(bar.LeftWheel==bar.RightWheel)throw std::invalid_argument("Anti-roll bar requires two distinct wheels");if(b.mLeftWheel>=0&&b.mRightWheel>=0)settings.mAntiRollBars.push_back(b);
        }
        settings.mController=controller;VehicleEntry entry;
        if(auto saved=PendingVehicleInput.find(root.GetUUID());saved!=PendingVehicleInput.end()){static_cast<VehicleDriverInput&>(entry)=saved->second;PendingVehicleInput.erase(saved);}
        entry.Body=found->second.Id;entry.Signature=std::move(signature);for(auto e:wheels)entry.Wheels.push_back(e.GetUUID());
        auto* body=System.GetBodyLockInterfaceNoLock().TryGetBody(entry.Body);entry.Native=new JPH::VehicleConstraint(*body,settings);
        const auto up=Settings.Gravity.LengthSquared()>1e-8f?V((-Settings.Gravity).Normalized()):JPH::Vec3::sAxisY();
        if(vehicle->CollisionTest==VehicleCollisionMode::Ray)entry.Native->SetVehicleCollisionTester(new JPH::VehicleCollisionTesterRay(JPH::ObjectLayer(vehicle->Layer),up));
        else if(vehicle->CollisionTest==VehicleCollisionMode::SphereCast)entry.Native->SetVehicleCollisionTester(new JPH::VehicleCollisionTesterCastSphere(JPH::ObjectLayer(vehicle->Layer),vehicle->CollisionRadius*scale.X,up));
        else entry.Native->SetVehicleCollisionTester(new JPH::VehicleCollisionTesterCastCylinder(JPH::ObjectLayer(vehicle->Layer)));
        if(old!=Vehicles.end()){entry.Throttle=old->second.Throttle;entry.Steering=old->second.Steering;entry.Brake=old->second.Brake;entry.Handbrake=old->second.Handbrake;entry.Gear=old->second.Gear;entry.Clutch=old->second.Clutch;System.RemoveStepListener(old->second.Native);System.RemoveConstraint(old->second.Native);Vehicles.erase(old);}
        entry.Gear=std::clamp(entry.Gear,-1,int(controller->mTransmission.mGearRatios.size()));
        System.AddConstraint(entry.Native);System.AddStepListener(entry.Native);Vehicles.emplace(root.GetUUID(),std::move(entry));
    }
    std::erase_if(PendingVehicleInput,[&](const auto& item){return !retained.contains(item.first);});
    for(auto it=Vehicles.begin();it!=Vehicles.end();) {
        if(retained.contains(it->first)){++it;continue;}
        System.RemoveStepListener(it->second.Native);System.RemoveConstraint(it->second.Native);it=Vehicles.erase(it);
    }
}
void World::UpdateVehicles(float) {
    for(auto& [id,entry]:Vehicles) {
        auto root=ScenePtr->GetEntity(id);auto& c=static_cast<JPH::WheeledVehicleController&>(*entry.Native->GetController());
        const bool powered=VehiclePart<VehicleEngine>(root)&&VehiclePart<VehicleTransmission>(root);
        c.SetDriverInput(powered?entry.Throttle:0,entry.Steering,entry.Brake,entry.Handbrake);
        if(!powered)c.GetTransmission().Set(0,0);
        else if(c.GetTransmission().mMode==JPH::ETransmissionMode::Manual)c.GetTransmission().Set(entry.Gear,entry.Clutch);
        if(std::abs(entry.Throttle)>0||std::abs(entry.Steering)>0||entry.Brake>0||entry.Handbrake>0)System.GetBodyInterface().ActivateBody(entry.Body);
        if(const auto* aero=VehiclePart<VehicleAerodynamics>(root)) {
            auto& body=System.GetBodyInterface();const auto velocity=V(body.GetLinearVelocity(entry.Body));const float speed=velocity.Length();
            const float pressure=.5f*aero->AirDensity*speed*speed*aero->FrontalArea;const auto rotation=Q(body.GetRotation(entry.Body));
            auto force=(speed>1e-6f?velocity*(-pressure*aero->DragCoefficient/speed):Vec3{})-rotation.Rotate(root.GetComponent<Vehicle>().Up)*(pressure*aero->DownforceCoefficient);
            RequireVector(force);
            if(force.LengthSquared()>0)body.AddForce(entry.Body,V(force),body.GetPosition(entry.Body)+V(rotation.Rotate(aero->CenterOfPressure*root.GetWorldTransform().Scale)),JPH::EActivation::DontActivate);
        }
    }
}
void World::UpdateWheelTransforms() {
    for(auto& [id,entry]:Vehicles)for(unsigned i=0;i<entry.Wheels.size();++i) {
        auto e=ScenePtr->GetEntity(entry.Wheels[i]);if(!e)continue;const auto* wheel=VehiclePart<VehicleWheel>(e);if(!wheel||!wheel->Animate)continue;
        const auto matrix=entry.Native->GetWheelWorldTransform(i,JPH::Vec3::sAxisX(),JPH::Vec3::sAxisY());auto pose=e.GetWorldTransform();pose.Position=V(matrix.GetTranslation());pose.Rotation=Q(matrix.GetQuaternion());
        if(!ScenePtr->SetWorldTransform(e,pose))throw std::runtime_error("Cannot update vehicle wheel under singular parent transform");
    }
}
static VehicleEntry* FindVehicle(World& world,Entity entity) {
    if(!entity||entity.GetScene()!=world.ScenePtr||!world.Running)return nullptr;
    const auto* c=entity.TryGetComponent<Vehicle>();const auto* body=entity.TryGetComponent<Rigidbody>();if(!c||!c->Enabled||!body||!body->Enabled)return nullptr;
    if(world.TopologyDirty)world.Synchronize();auto it=world.Vehicles.find(entity.GetUUID());return it==world.Vehicles.end()?nullptr:&it->second;
}
static bool SetVehicleInput(Scene& scene,Entity entity,float throttle,float steering,float brake,float handbrake) {
    if(!Detail::VehicleValidation::Range(throttle,-1,1)||!Detail::VehicleValidation::Range(steering,-1,1)||!Detail::VehicleValidation::Range(brake,0,1)||!Detail::VehicleValidation::Range(handbrake,0,1))throw std::invalid_argument("Vehicle input must be finite and normalized");
    auto w=Get(scene);auto* entry=FindVehicle(*w,entity);if(!entry)return false;
    if(const auto* transmission=VehiclePart<VehicleTransmission>(entity);transmission&&!transmission->Automatic&&throttle<0)return false;
    entry->Throttle=throttle;entry->Steering=steering;entry->Brake=brake;entry->Handbrake=handbrake;return true;
}
static bool SetVehicleGear(Scene& scene,Entity entity,int gear,float clutch) {
    if(!Detail::VehicleValidation::Range(clutch,0,1))throw std::invalid_argument("Clutch must be between zero and one");
    auto w=Get(scene);auto* entry=FindVehicle(*w,entity);if(!entry)return false;
    if(!VehiclePart<VehicleEngine>(entity)||!VehiclePart<VehicleTransmission>(entity))return false;
    const auto& transmission=static_cast<JPH::WheeledVehicleController&>(*entry->Native->GetController()).GetTransmission();
    if(transmission.mMode!=JPH::ETransmissionMode::Manual||gear<-1||gear>int(transmission.mGearRatios.size()))return false;
    entry->Gear=gear;entry->Clutch=clutch;return true;
}
static std::optional<VehicleState> ReadVehicleState(Scene& scene,Entity entity) {
    auto w=Get(scene);auto* entry=FindVehicle(*w,entity);if(!entry)return {};
    const auto& controller=static_cast<JPH::WheeledVehicleController&>(*entry->Native->GetController());VehicleState state;
    state.Speed=w->System.GetBodyInterface().GetLinearVelocity(entry->Body).Dot(w->System.GetBodyInterface().GetRotation(entry->Body)*entry->Native->GetLocalForward());state.EngineRPM=VehiclePart<VehicleEngine>(entity)?controller.GetEngine().GetCurrentRPM():0;state.Gear=controller.GetTransmission().GetCurrentGear();state.Clutch=controller.GetTransmission().GetClutchFriction();state.WheelCount=unsigned(entry->Wheels.size());if(!VehiclePart<VehicleEngine>(entity)||!VehiclePart<VehicleTransmission>(entity)){state.Gear=0;state.Clutch=0;}
    for(auto* wheel:entry->Native->GetWheels())if(wheel->HasContact())++state.GroundedWheels;return state;
}
static std::optional<VehicleWheelState> ReadVehicleWheelState(Scene& scene,Entity entity) {
    auto w=Get(scene);if(!entity||entity.GetScene()!=&scene||!w->Running)return {};const auto* c=entity.TryGetComponent<VehicleWheel>();const auto* spring=entity.TryGetComponent<WheelSuspension>();if(!c||!c->Enabled||!spring||!spring->Enabled)return {};
    auto root=VehicleOwner(entity);auto* entry=FindVehicle(*w,root);if(!entry)return {};
    auto it=std::find(entry->Wheels.begin(),entry->Wheels.end(),entity.GetUUID());if(it==entry->Wheels.end())return {};const unsigned index=unsigned(it-entry->Wheels.begin());
    const auto* wheel=static_cast<const JPH::WheelWV*>(entry->Native->GetWheel(index));VehicleWheelState state;state.Grounded=wheel->HasContact();state.Position=V(entry->Native->GetWheelWorldTransform(index,JPH::Vec3::sAxisX(),JPH::Vec3::sAxisY()).GetTranslation());state.AngularVelocity=wheel->GetAngularVelocity();state.SteerAngle=ToDegrees(wheel->GetSteerAngle());state.SuspensionLength=wheel->GetSuspensionLength();state.SuspensionForce=w->LastDelta>0?wheel->GetSuspensionLambda()*float(w->Settings.CollisionSteps)/w->LastDelta:0;state.LongitudinalSlip=wheel->mLongitudinalSlip;state.LateralSlip=ToDegrees(wheel->mLateralSlip);
    if(state.Grounded){state.ContactPoint=V(wheel->GetContactPosition());state.ContactNormal=V(wheel->GetContactNormal());}return state;
}
void World::DrawVehicleGuides(UUID selected,std::vector<std::array<float,10>>& lines) {
    auto root=ScenePtr->GetEntity(selected);if(!root)return;auto selectedRoot=VehicleOwner(root);if(!selectedRoot)return;
    if(!selectedRoot.GetComponent<Vehicle>().Enabled)return;
    const auto line=[&](Vec3 a,Vec3 b){if(lines.size()<12288)lines.push_back({a.X,a.Y,a.Z,b.X,b.Y,b.Z,.3f,.8f,1,1});};
    const auto matrix=selectedRoot.GetWorldMatrix();const float scale=selectedRoot.GetWorldTransform().Scale.X;
    const auto centerOf=[&](Entity e) {
        if(auto state=ReadVehicleWheelState(*ScenePtr,e))return state->Position;
        const auto* wheel=e.TryGetComponent<VehicleWheel>();const auto* spring=e.TryGetComponent<WheelSuspension>();
        return wheel&&spring?matrix.TransformPoint(wheel->Position+spring->Direction*spring->MaxLength):e.GetWorldTransform().Position;
    };
    for(auto h:ScenePtr->GetRegistry().view<VehicleWheel,WheelSuspension>()) {
        auto entity=ScenePtr->GetEntity(static_cast<Entity::Id>(h));if(VehicleOwner(entity)!=selectedRoot||(root!=selectedRoot&&root!=entity))continue;
        const auto& wheel=entity.GetComponent<VehicleWheel>();const auto& suspension=entity.GetComponent<WheelSuspension>();if(!wheel.Enabled||!suspension.Enabled||!wheel.IsValid()||!suspension.IsValid())continue;
        const auto a=matrix.TransformPoint(wheel.Position),b=matrix.TransformPoint(wheel.Position+suspension.Direction*suspension.MaxLength);line(a,b);
        Vec3 center=centerOf(entity),up=matrix.TransformDirection(wheel.Up)*wheel.Radius,forward=matrix.TransformDirection(wheel.Forward)*wheel.Radius,right=matrix.TransformDirection(Vec3::Cross(wheel.Up,wheel.Forward).Normalized())*(wheel.Width*.5f);
        if(auto entry=Vehicles.find(selectedRoot.GetUUID());entry!=Vehicles.end()) {
            auto it=std::find(entry->second.Wheels.begin(),entry->second.Wheels.end(),entity.GetUUID());
            if(it!=entry->second.Wheels.end()) {
                const auto pose=entry->second.Native->GetWheelWorldTransform(unsigned(it-entry->second.Wheels.begin()),JPH::Vec3::sAxisX(),JPH::Vec3::sAxisY());
                up=V(pose.Multiply3x3(JPH::Vec3::sAxisY()))*(wheel.Radius*scale);forward=V(pose.Multiply3x3(JPH::Vec3::sAxisZ()))*(wheel.Radius*scale);right=V(pose.Multiply3x3(JPH::Vec3::sAxisX()))*(wheel.Width*scale*.5f);
            }
        }
        for(unsigned i=0;i<32;++i) {
            const float x=6.2831853f*i/32,y=6.2831853f*(i+1)/32;
            for(float side:{-1.f,1.f})line(center+right*side+up*std::cos(x)+forward*std::sin(x),center+right*side+up*std::cos(y)+forward*std::sin(y));
        }
    }
    for(auto h:ScenePtr->GetRegistry().view<VehicleAntiRollBar>()) {
        auto e=ScenePtr->GetEntity(static_cast<Entity::Id>(h));const auto& bar=e.GetComponent<VehicleAntiRollBar>();if(!bar.Enabled||VehicleOwner(e)!=selectedRoot||(root!=selectedRoot&&root!=e))continue;
        auto left=ScenePtr->GetEntity(bar.LeftWheel),right=ScenePtr->GetEntity(bar.RightWheel);if(left&&right&&VehicleOwner(left)==selectedRoot&&VehicleOwner(right)==selectedRoot)line(centerOf(left),centerOf(right));
    }
}
