// Included once inside the private physics module's anonymous namespace.
struct ReplayFrame {
    UUID Session;
    std::string Key,Solver;
    std::uint64_t Tick=0,Bytes=0,Hash=14695981039346656037ull;
    float Alpha=1,Delta=0;
    std::map<UUID,Transform> Locals,Last,Previous,SoftPoses;
    std::map<UUID,VehicleDriverInput> Drivers,PendingDrivers;
    std::map<UUID,std::vector<float>> SoftInverseMass;
    std::map<JointKey,bool> Broken;
    std::map<std::pair<UUID,unsigned>,Vec3> WheelExtras;
    std::vector<PendingForce> Forces;
    std::map<Pair,CollisionEvent> Contacts;
    std::map<ContactPair,std::size_t> Counts,Queued;
    std::vector<CollisionEvent> Events;
};
void ReplayBoundary(World& w){if(!w.Running||w.Stepping||w.Updating||w.Dispatching)throw std::logic_error("Physics replay requires a running world at a step boundary");}
void KeyString(std::string& out,const std::string& value){AppendPhysicsKey(out,std::uint64_t(value.size()));out+=value;}
std::set<UUID> ReplayEntities(World& w){
    std::set<UUID> ids;
    const auto add=[&](UUID id){auto entity=w.ScenePtr->GetEntity(id);while(entity){if(!ids.insert(entity.GetUUID()).second)break;entity=entity.GetParent();}};
    for(auto& [id,body]:w.Bodies)add(id);for(auto& [id,body]:w.SoftBodies)add(id);for(auto& [id,data]:w.Colliders)add(data.Collider);for(auto& [id,data]:w.SoftColliders)add(data.Collider);for(auto& [id,vehicle]:w.Vehicles)for(auto wheel:vehicle.Wheels)add(wheel);
    return ids;
}
std::string ReplayKey(World& w){
    std::string key;AppendPhysicsKey(key,w.ScenePtr->GetUUID());
    VisitPhysicsSettings(w.Settings,[&](const char* name,const char*,const auto& value){if(std::string_view(name)=="AutoSimulation")return;if constexpr(requires{value.begin();}){for(auto item:value)AppendPhysicsKey(key,item);}else AppendPhysicsKey(key,value);});
    AppendPhysicsKey(key,w.Determinism.Enabled);AppendPhysicsKey(key,w.Determinism.SingleThreaded);AppendPhysicsKey(key,w.Determinism.FixedDeltaTime);
    std::map<UUID,std::pair<UUID,Entity::Id>> parents;for(auto id:ReplayEntities(w)){auto entity=w.ScenePtr->GetEntity(id);parents[id]={entity.GetParent().GetUUID(),entity.GetId()};}for(auto& [id,parent]:parents){AppendPhysicsKey(key,id);AppendPhysicsKey(key,parent.first);AppendPhysicsKey(key,parent.second);}
    AppendPhysicsKey(key,std::uint64_t(w.Bodies.size()));for(auto& [id,b]:w.Bodies){AppendPhysicsKey(key,id);AppendPhysicsKey(key,b.Id.GetIndexAndSequenceNumber());KeyString(key,b.Signature);}
    AppendPhysicsKey(key,std::uint64_t(w.SoftBodies.size()));for(auto& [id,b]:w.SoftBodies){AppendPhysicsKey(key,id);AppendPhysicsKey(key,b.Id.GetIndexAndSequenceNumber());KeyString(key,b.Signature);KeyString(key,b.Configuration);}
    for(auto& [id,j]:w.Joints){AppendPhysicsKey(key,id.first);AppendPhysicsKey(key,id.second);KeyString(key,j.Signature);KeyString(key,j.Configuration);}
    for(auto& [id,v]:w.Vehicles){AppendPhysicsKey(key,id);KeyString(key,v.Signature);}
    return key;
}
PhysicsState CaptureReplay(World& w){
    ReplayBoundary(w);w.Synchronize();auto frame=std::make_shared<ReplayFrame>();frame->Session=w.ReplaySession;frame->Tick=w.Tick;frame->Alpha=w.Alpha;frame->Delta=w.LastDelta;frame->Key=ReplayKey(w);
    JPH::StateRecorderImpl stream;w.System.SaveState(stream);if(stream.IsFailed())throw std::runtime_error("Could not capture physics state");frame->Solver=stream.GetData();
    for(auto id:ReplayEntities(w)){auto entity=w.ScenePtr->GetEntity(id);frame->Locals.emplace(id,entity.GetComponent<Transform>());}
    for(auto& [id,b]:w.Bodies){frame->Last[id]=b.LastScene;frame->Previous[id]=b.PreviousScene;}
    for(auto& [id,b]:w.SoftBodies){frame->SoftPoses[id]=b.Pose;const auto* body=w.System.GetBodyLockInterfaceNoLock().TryGetBody(b.Id);const auto* motion=static_cast<const JPH::SoftBodyMotionProperties*>(body->GetMotionProperties());auto& masses=frame->SoftInverseMass[id];for(auto& vertex:motion->GetVertices())masses.push_back(vertex.mInvMass);}
    for(auto& [id,v]:w.Vehicles)frame->Drivers[id]=static_cast<const VehicleDriverInput&>(v);
    for(auto& [id,j]:w.Joints)frame->Broken[id]=j.Broken;
    for(auto& [id,v]:w.Vehicles)for(unsigned i=0;i<v.Wheels.size();++i){const auto* wheel=static_cast<const JPH::WheelWV*>(v.Native->GetWheel(i));frame->WheelExtras[{id,i}]={wheel->GetSteerAngle(),wheel->mLongitudinalSlip,wheel->mLateralSlip};}
    frame->PendingDrivers=w.PendingVehicleInput;frame->Forces=w.Forces;frame->Contacts=w.Contacts;frame->Counts=w.ContactCounts;frame->Queued=w.Queued;frame->Events=w.Events;
    frame->Bytes=sizeof(ReplayFrame)+sizeof(PhysicsState)+frame->Solver.capacity()+frame->Key.capacity()+192ull*(frame->Locals.size()+frame->Last.size()+frame->Previous.size()+frame->SoftPoses.size()+frame->Drivers.size()+frame->PendingDrivers.size()+frame->Broken.size()+frame->WheelExtras.size()+frame->Counts.size()+frame->Queued.size())+frame->Forces.capacity()*sizeof(PendingForce);
    for(auto& [id,masses]:frame->SoftInverseMass)frame->Bytes+=128+masses.capacity()*sizeof(float);
    for(auto& [pair,event]:frame->Contacts)frame->Bytes+=256+event.Contacts.capacity()*sizeof(ContactPoint);
    for(auto& event:frame->Events)frame->Bytes+=sizeof(CollisionEvent)+event.Contacts.capacity()*sizeof(ContactPoint);
    if(frame->Bytes>512ull*1024*1024)throw std::length_error("Physics state exceeds 512 MiB");
    auto hash=[&](const std::string& text){for(unsigned char byte:text){frame->Hash^=byte;frame->Hash*=1099511628211ull;}};hash(frame->Solver);hash(frame->Key);
    std::string data;AppendPhysicsKey(data,frame->Tick);for(auto& [id,t]:frame->Locals){AppendPhysicsKey(data,id);AppendPhysicsKey(data,t.Position);AppendPhysicsKey(data,t.Rotation);AppendPhysicsKey(data,t.Scale);}for(auto& f:frame->Forces){AppendPhysicsKey(data,f.Owner);AppendPhysicsKey(data,f.Command);AppendPhysicsKey(data,f.Value);AppendPhysicsKey(data,f.Point);}for(auto& [id,d]:frame->Drivers){AppendPhysicsKey(data,id);AppendPhysicsKey(data,d.Throttle);AppendPhysicsKey(data,d.Steering);AppendPhysicsKey(data,d.Brake);AppendPhysicsKey(data,d.Handbrake);AppendPhysicsKey(data,d.Gear);AppendPhysicsKey(data,d.Clutch);}
    const auto pose=[&](const auto& poses){for(auto& [id,t]:poses){AppendPhysicsKey(data,id);AppendPhysicsKey(data,t.Position);AppendPhysicsKey(data,t.Rotation);AppendPhysicsKey(data,t.Scale);}};pose(frame->Last);pose(frame->Previous);pose(frame->SoftPoses);
    AppendPhysicsKey(data,frame->Alpha);AppendPhysicsKey(data,frame->Delta);
    for(auto& [id,masses]:frame->SoftInverseMass){AppendPhysicsKey(data,id);for(auto mass:masses)AppendPhysicsKey(data,mass);}
    for(auto& [id,d]:frame->PendingDrivers){AppendPhysicsKey(data,id);AppendPhysicsKey(data,d.Throttle);AppendPhysicsKey(data,d.Steering);AppendPhysicsKey(data,d.Brake);AppendPhysicsKey(data,d.Handbrake);AppendPhysicsKey(data,d.Gear);AppendPhysicsKey(data,d.Clutch);}
    for(auto& [key,value]:frame->WheelExtras){AppendPhysicsKey(data,key.first);AppendPhysicsKey(data,key.second);AppendPhysicsKey(data,value);}
    for(auto& [key,broken]:frame->Broken){AppendPhysicsKey(data,key.first);AppendPhysicsKey(data,key.second);AppendPhysicsKey(data,broken);}
    const auto eventKey=[&](const ContactPair& key){AppendPhysicsKey(data,std::get<0>(key));AppendPhysicsKey(data,std::get<1>(key));AppendPhysicsKey(data,std::get<2>(key));AppendPhysicsKey(data,std::get<3>(key));AppendPhysicsKey(data,std::get<4>(key));};
    for(auto& [key,count]:frame->Counts){eventKey(key);AppendPhysicsKey(data,std::uint64_t(count));}for(auto& [key,count]:frame->Queued){eventKey(key);AppendPhysicsKey(data,std::uint64_t(count));}
    const auto event=[&](const CollisionEvent& e){AppendPhysicsKey(data,e.Type);AppendPhysicsKey(data,e.BodyAUUID);AppendPhysicsKey(data,e.BodyBUUID);AppendPhysicsKey(data,e.ColliderAUUID);AppendPhysicsKey(data,e.ColliderBUUID);AppendPhysicsKey(data,e.Normal);AppendPhysicsKey(data,e.RelativeVelocity);AppendPhysicsKey(data,e.DeltaTime);for(auto& c:e.Contacts){AppendPhysicsKey(data,c.PointA);AppendPhysicsKey(data,c.PointB);AppendPhysicsKey(data,c.Penetration);}};for(auto& [key,e]:frame->Contacts)event(e);for(auto& e:frame->Events)event(e);hash(data);
    return PhysicsReplayAccess::Make(frame,frame->Tick,frame->Bytes,frame->Hash);
}
void RestoreReplay(World& w,const PhysicsState& state){
    ReplayBoundary(w);if(!state.IsValid())throw std::invalid_argument("Invalid physics checkpoint");w.Synchronize();auto frame=std::static_pointer_cast<const ReplayFrame>(PhysicsReplayAccess::Data(state));
    if(frame->Session!=w.ReplaySession||frame->Key!=ReplayKey(w))throw std::logic_error("Physics checkpoint belongs to a different world or its topology/settings changed");
    for(auto& [id,masses]:frame->SoftInverseMass){auto* body=w.System.GetBodyLockInterfaceNoLock().TryGetBody(w.SoftBodies.at(id).Id);auto* motion=static_cast<JPH::SoftBodyMotionProperties*>(body->GetMotionProperties());if(masses.size()!=motion->GetVertices().size())throw std::logic_error("Soft body checkpoint topology changed");}
    auto pending=frame->PendingDrivers;auto forces=frame->Forces;auto contacts=frame->Contacts;auto counts=frame->Counts;auto queued=frame->Queued;auto events=frame->Events;
    JPH::StateRecorderImpl backup;w.System.SaveState(backup);JPH::StateRecorderImpl stream;stream.WriteBytes(frame->Solver.data(),frame->Solver.size());stream.Rewind();
    if(!w.System.RestoreState(stream)||stream.IsFailed()){backup.Rewind();if(!w.System.RestoreState(backup))throw std::runtime_error("Physics checkpoint rollback failed");throw std::runtime_error("Physics checkpoint restore failed");}
    for(auto& [id,t]:frame->Locals)w.ScenePtr->GetEntity(id).GetComponent<Transform>()=t;
    for(auto& [id,b]:w.Bodies){b.LastScene=frame->Last.at(id);b.PreviousScene=frame->Previous.at(id);}
    for(auto& [id,b]:w.SoftBodies){b.Pose=frame->SoftPoses.at(id);auto* body=w.System.GetBodyLockInterfaceNoLock().TryGetBody(b.Id);auto* motion=static_cast<JPH::SoftBodyMotionProperties*>(body->GetMotionProperties());auto& vertices=motion->GetVertices();auto& masses=frame->SoftInverseMass.at(id);for(std::size_t i=0;i<vertices.size();++i)vertices[i].mInvMass=masses[i];}
    for(auto& [id,v]:w.Vehicles)static_cast<VehicleDriverInput&>(v)=frame->Drivers.at(id);
    for(auto& [key,value]:frame->WheelExtras){auto* wheel=static_cast<JPH::WheelWV*>(w.Vehicles.at(key.first).Native->GetWheel(key.second));wheel->SetSteerAngle(value.X);wheel->mLongitudinalSlip=value.Y;wheel->mLateralSlip=value.Z;}
    for(auto& [id,j]:w.Joints){j.Broken=frame->Broken.at(id);if(j.Native)j.Native->SetEnabled(!j.Broken&&j.Settings.Enabled);}
    w.PendingVehicleInput=std::move(pending);w.Forces=std::move(forces);w.Contacts=std::move(contacts);w.ContactCounts=std::move(counts);w.Queued=std::move(queued);w.Events=std::move(events);w.Tick=frame->Tick;w.Alpha=frame->Alpha;w.LastDelta=frame->Delta;w.Overflow=false;
}
void RecordReplayFrame(World& w){
    if(w.ReplayMode!=PhysicsReplayMode::Recording)return;
    try{auto frame=CaptureReplay(w);auto first=std::static_pointer_cast<const ReplayFrame>(PhysicsReplayAccess::Data(w.ReplayFrames.front()));auto next=std::static_pointer_cast<const ReplayFrame>(PhysicsReplayAccess::Data(frame));if(first->Key!=next->Key)throw std::logic_error("Physics topology/settings changed during replay recording");
        if(w.ReplayFrames.size()>=w.ReplayLimits.MaxFrames||frame.GetByteSize()>w.ReplayLimits.MaxMemoryBytes-w.ReplayBytes){w.ReplayLimitReached=true;w.ReplayMode=PhysicsReplayMode::Idle;return;}w.ReplayBytes+=frame.GetByteSize();w.ReplayFrames.push_back(std::move(frame));w.ReplayCursor=w.ReplayFrames.size()-1;
    }catch(...){w.ReplayMode=PhysicsReplayMode::Idle;throw;}
}
const PhysicsReplayModuleApi ReplayApi{
    [](Scene& scene){return Get(scene)->Determinism;},
    [](Scene& scene,const PhysicsDeterminismSettings& value){if(!value.IsValid())throw std::invalid_argument("Invalid deterministic physics settings");auto w=Get(scene);if(w->Stepping||w->Updating||w->Dispatching||w->ReplayMode!=PhysicsReplayMode::Idle)throw std::logic_error("Cannot change determinism during a step/recording/playback");w->Determinism=value;w->Configure();},
    [](){(void)State();
#ifdef JPH_CROSS_PLATFORM_DETERMINISTIC
        return true;
#else
        return false;
#endif
    },
    [](Scene& scene){return CaptureReplay(*Get(scene));},
    [](Scene& scene,const PhysicsState& state){auto w=Get(scene);if(w->ReplayMode!=PhysicsReplayMode::Idle)throw std::logic_error("Stop recording/playback before restoring a checkpoint");RestoreReplay(*w,state);},
    [](Scene& scene,int command,const PhysicsReplaySettings& settings,std::uint64_t index,bool loop){auto w=Get(scene);ReplayBoundary(*w);
        switch(command){
        case 0:{if(!settings.IsValid()||!w->Determinism.Enabled||w->ReplayMode!=PhysicsReplayMode::Idle)throw std::logic_error("Recording requires valid limits, idle replay and deterministic fixed stepping");auto frame=CaptureReplay(*w);if(frame.GetByteSize()>settings.MaxMemoryBytes)throw std::length_error("Initial checkpoint exceeds recording budget");w->ReplayFrames={frame};w->ReplayBytes=frame.GetByteSize();w->ReplayLimits=settings;w->ReplayCursor=0;w->ReplayLimitReached=false;w->ReplayMode=PhysicsReplayMode::Recording;break;}
        case 1:if(w->ReplayMode==PhysicsReplayMode::Recording)w->ReplayMode=PhysicsReplayMode::Idle;break;
        case 2:if(w->ReplayMode==PhysicsReplayMode::Recording||scene.GetPhysicsSettings().AutoSimulation||w->ReplayFrames.empty())throw std::logic_error("Playback requires recorded frames and disabled AutoSimulation");RestoreReplay(*w,w->ReplayFrames.front());w->ReplayLoop=loop;w->ReplayCursor=0;w->ReplayMode=PhysicsReplayMode::Playback;break;
        case 3:if(w->ReplayMode==PhysicsReplayMode::Playback)w->ReplayMode=PhysicsReplayMode::Idle;break;
        case 4:if(w->ReplayMode==PhysicsReplayMode::Recording||index>=w->ReplayFrames.size())throw std::out_of_range("Replay frame unavailable");RestoreReplay(*w,w->ReplayFrames[index]);w->ReplayCursor=index;break;
        case 5:if(w->ReplayMode!=PhysicsReplayMode::Playback)throw std::logic_error("Replay playback is not active");{auto next=w->ReplayCursor+1;if(next>=w->ReplayFrames.size()){if(w->ReplayLoop)next=0;else break;}RestoreReplay(*w,w->ReplayFrames[next]);w->ReplayCursor=next;}break;
        case 6:w->ReplayMode=PhysicsReplayMode::Idle;w->ReplayFrames.clear();w->ReplayBytes=w->ReplayCursor=0;w->ReplayLimitReached=false;break;
        default:throw std::invalid_argument("Invalid replay operation");
        }
    },
    [](Scene& scene){auto w=Get(scene);return PhysicsReplayStatus{w->ReplayMode,w->Tick,w->ReplayFrames.size(),w->ReplayCursor,w->ReplayBytes,w->ReplayLimitReached,!w->ReplayFrames.empty()&&w->ReplayCursor+1==w->ReplayFrames.size()};},
    [](Scene& scene,std::uint64_t index){auto w=Get(scene);if(index>=w->ReplayFrames.size())throw std::out_of_range("Replay frame unavailable");return w->ReplayFrames[std::size_t(index)];}
};
