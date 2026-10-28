// Included inside the physics module's private namespace after World is defined.
void World::DetachJoints(JPH::BodyID body){
    for(auto& [key,j]:Joints)if(j.Native&&(j.A==body||j.B==body)){System.RemoveConstraint(j.Native);j.Native=nullptr;}
    Suppressed.clear();
}
void World::ClearExtras(){
    for(auto& [key,j]:Joints)if(j.Native)System.RemoveConstraint(j.Native);
    Joints.clear();Suppressed.clear();
    for(auto& [id,s]:SoftBodies){System.GetBodyInterface().RemoveBody(s.Id);System.GetBodyInterface().DestroyBody(s.Id);SoftColliders.erase(s.Id.GetIndexAndSequenceNumber());s.Mesh.Destroy();}
    SoftBodies.clear();
}
static JPH::SpringSettings Spring(float frequency,float damping){return {JPH::ESpringMode::FrequencyAndDamping,frequency,damping};}
static JPH::Ref<JPH::TwoBodyConstraint> CreateJoint(const Joint& c,JPH::Body& a,JPH::Body& b,Entity owner,Entity connected){
    if(!std::isfinite(c.Axis.LengthSquared()))throw std::invalid_argument("Joint axis magnitude is too large");
    const auto p=owner.GetWorldMatrix().TransformPoint(c.Anchor);
    const auto q=c.AutoConfigureConnectedAnchor?p:(connected?connected.GetWorldMatrix().TransformPoint(c.ConnectedAnchor):c.ConnectedAnchor);
    const auto axis=owner.GetWorldTransform().Rotation.Rotate(c.Axis.Normalized());
    const auto normal=Vec3::Cross(axis,std::abs(axis.Y)<.9f?Vec3{0,1,0}:Vec3{1,0,0}).Normalized();
    const auto motor=JPH::MotorSettings(c.MotorFrequency,c.MotorDamping,c.MotorForce,c.MotorForce);
    switch(c.Type){
    case JointType::Distance:{JPH::DistanceConstraintSettings s;s.mPoint1=V(p);s.mPoint2=V(q);s.mMinDistance=c.MinDistance;s.mMaxDistance=c.MaxDistance;s.mLimitsSpringSettings=Spring(c.SpringFrequency,c.SpringDamping);return s.Create(a,b);}
    case JointType::Hinge:{JPH::HingeConstraintSettings s;s.mPoint1=V(p);s.mPoint2=V(q);s.mHingeAxis1=s.mHingeAxis2=V(axis);s.mNormalAxis1=s.mNormalAxis2=V(normal);s.mLimitsMin=c.MinLimit;s.mLimitsMax=c.MaxLimit;s.mLimitsSpringSettings=Spring(c.SpringFrequency,c.SpringDamping);s.mMaxFrictionTorque=c.Friction;s.mMotorSettings=motor;auto* j=static_cast<JPH::HingeConstraint*>(s.Create(a,b));j->SetMotorState(JPH::EMotorState(c.Motor));j->SetTargetAngularVelocity(c.MotorVelocity);j->SetTargetAngle(c.MotorTarget);return j;}
    case JointType::Slider:{JPH::SliderConstraintSettings s;s.mPoint1=V(p);s.mPoint2=V(q);s.mSliderAxis1=s.mSliderAxis2=V(axis);s.mNormalAxis1=s.mNormalAxis2=V(normal);s.mLimitsMin=c.MinLimit;s.mLimitsMax=c.MaxLimit;s.mLimitsSpringSettings=Spring(c.SpringFrequency,c.SpringDamping);s.mMaxFrictionForce=c.Friction;s.mMotorSettings=motor;auto* j=static_cast<JPH::SliderConstraint*>(s.Create(a,b));j->SetMotorState(JPH::EMotorState(c.Motor));j->SetTargetVelocity(c.MotorVelocity);j->SetTargetPosition(c.MotorTarget);return j;}
    case JointType::BallAndSocket:{JPH::PointConstraintSettings s;s.mPoint1=V(p);s.mPoint2=V(q);return s.Create(a,b);}
    default:{JPH::FixedConstraintSettings s;s.mPoint1=V(p);s.mPoint2=V(q);s.mAxisX1=s.mAxisX2=V(axis);s.mAxisY1=s.mAxisY2=V(normal);return s.Create(a,b);}
    }
}
static void ConfigureJoint(JPH::TwoBodyConstraint& native,const Joint& c){
    const auto motor=JPH::MotorSettings(c.MotorFrequency,c.MotorDamping,c.MotorForce,c.MotorForce);
    switch(c.Type){
    case JointType::Distance:{auto& j=static_cast<JPH::DistanceConstraint&>(native);j.SetDistance(c.MinDistance,c.MaxDistance);j.SetLimitsSpringSettings(Spring(c.SpringFrequency,c.SpringDamping));break;}
    case JointType::Hinge:{auto& j=static_cast<JPH::HingeConstraint&>(native);j.SetLimits(c.MinLimit,c.MaxLimit);j.SetLimitsSpringSettings(Spring(c.SpringFrequency,c.SpringDamping));j.SetMaxFrictionTorque(c.Friction);j.GetMotorSettings()=motor;j.SetMotorState(JPH::EMotorState(c.Motor));j.SetTargetAngularVelocity(c.MotorVelocity);j.SetTargetAngle(c.MotorTarget);break;}
    case JointType::Slider:{auto& j=static_cast<JPH::SliderConstraint&>(native);j.SetLimits(c.MinLimit,c.MaxLimit);j.SetLimitsSpringSettings(Spring(c.SpringFrequency,c.SpringDamping));j.SetMaxFrictionForce(c.Friction);j.GetMotorSettings()=motor;j.SetMotorState(JPH::EMotorState(c.Motor));j.SetTargetVelocity(c.MotorVelocity);j.SetTargetPosition(c.MotorTarget);break;}
    default:break;
    }
}
template<class T> static void SyncJointPool(World& w,unsigned pool,std::set<JointKey>& retained){
    for(auto handle:w.ScenePtr->GetRegistry().view<T>()){
        auto entity=w.ScenePtr->GetEntity(static_cast<Entity::Id>(handle));const auto& c=entity.template GetComponent<T>();if(!c.Enabled)continue;
        if(!c.IsValid())throw std::invalid_argument("Invalid joint properties");
        auto body=w.Bodies.find(entity.GetUUID());if(body==w.Bodies.end())continue;
        auto connected=w.ScenePtr->GetEntity(c.ConnectedBody);auto other=w.Bodies.find(c.ConnectedBody);
        if(c.ConnectedBody&&(other==w.Bodies.end()||c.ConnectedBody==entity.GetUUID()))continue;
        auto* a=w.System.GetBodyLockInterfaceNoLock().TryGetBody(body->second.Id);
        auto* b=c.ConnectedBody?w.System.GetBodyLockInterfaceNoLock().TryGetBody(other->second.Id):&JPH::Body::sFixedToWorld;
        if(!a||!b||(!a->IsDynamic()&&!b->IsDynamic()))continue;
        const JointKey key{entity.GetUUID(),pool};retained.insert(key);std::string signature,configuration;
        AppendPhysicsKey(signature,c.Type);AppendPhysicsKey(signature,c.ConnectedBody);AppendPhysicsKey(signature,c.Anchor);AppendPhysicsKey(signature,c.ConnectedAnchor);AppendPhysicsKey(signature,c.Axis);AppendPhysicsKey(signature,c.AutoConfigureConnectedAnchor);
        VisitJoint(c,[&](const char*,const char*,const auto& field){AppendPhysicsKey(configuration,field);});
        auto& entry=w.Joints[key];
        if(entry.Signature!=signature||!entry.Native||entry.A!=a->GetID()||entry.B!=b->GetID()){
            const bool reset=entry.Signature.empty();
            if(entry.Native)w.System.RemoveConstraint(entry.Native);
            entry.Native=CreateJoint(c,*a,*b,entity,connected);entry.A=a->GetID();entry.B=b->GetID();entry.Signature=std::move(signature);entry.Settings=c;entry.Configuration.clear();if(reset)entry.Broken=false;
            entry.Native->SetEnabled(!entry.Broken);w.System.AddConstraint(entry.Native);w.System.GetBodyInterface().InvalidateContactCache(entry.A);if(c.ConnectedBody)w.System.GetBodyInterface().InvalidateContactCache(entry.B);
            if(a->IsDynamic())w.System.GetBodyInterface().ActivateBody(a->GetID());if(b->IsDynamic())w.System.GetBodyInterface().ActivateBody(b->GetID());
        }
        if(entry.Configuration!=configuration){ConfigureJoint(*entry.Native,c);entry.Settings=c;entry.Configuration=std::move(configuration);w.System.GetBodyInterface().InvalidateContactCache(entry.A);if(c.ConnectedBody)w.System.GetBodyInterface().InvalidateContactCache(entry.B);if(a->IsDynamic())w.System.GetBodyInterface().ActivateBody(entry.A);if(b->IsDynamic())w.System.GetBodyInterface().ActivateBody(entry.B);}
        if(!c.EnableCollision&&!entry.Broken&&c.ConnectedBody){std::pair<unsigned,unsigned> ids=std::minmax(entry.A.GetIndexAndSequenceNumber(),entry.B.GetIndexAndSequenceNumber());++w.Suppressed[{ids.first,ids.second}];}
    }
}
template<class T> static SoftEntry CreateSoft(World& w,Entity entity,const T& c,const std::string& signature){
    auto pose=entity.GetWorldTransform();RequireVector(pose.Position);RequireVector(pose.Scale);(void)Q(pose.Rotation);if(pose.Scale.X<=0||pose.Scale.Y<=0||pose.Scale.Z<=0)throw std::invalid_argument("Soft bodies require positive world scale");
    SoftEntry entry;entry.Signature=signature;entry.Pose=pose;entry.Wind=c.Wind;entry.Material=c.MaterialAsset;entry.CastShadows=c.CastShadows;
    JPH::Ref<JPH::SoftBodySharedSettings> shared=new JPH::SoftBodySharedSettings;
    const auto add=[&](Vec3 p,Vec2 uv,bool pin){p=p*pose.Scale;RequireVector(p);if(!std::isfinite(entry.InverseMass)||entry.InverseMass<=0)throw std::invalid_argument("Soft body mass is out of range");shared->mVertices.emplace_back(JPH::Float3(p.X,p.Y,p.Z),JPH::Float3(0,0,0),pin?0:entry.InverseMass);entry.Vertices.push_back({p,{0,0,1},uv,{1,1,1,1}});};
    const auto face=[&](unsigned a,unsigned b,unsigned d){shared->AddFace({a,b,d});entry.Indices.insert(entry.Indices.end(),{a,b,d});};
    if constexpr(std::is_same_v<T,Cloth>){
        entry.InverseMass=float((c.SegmentsX+1)*(c.SegmentsY+1))/c.Mass;
        for(unsigned y=0;y<=c.SegmentsY;++y)for(unsigned x=0;x<=c.SegmentsX;++x){float u=float(x)/c.SegmentsX,v=float(y)/c.SegmentsY;add({(u-.5f)*c.Width,(.5f-v)*c.Height,0},{u,v},((c.PinEdges&1)&&x==0)||((c.PinEdges&2)&&x==c.SegmentsX)||((c.PinEdges&4)&&y==0)||((c.PinEdges&8)&&y==c.SegmentsY));}
        for(unsigned y=0;y<c.SegmentsY;++y)for(unsigned x=0;x<c.SegmentsX;++x){unsigned a=y*(c.SegmentsX+1)+x,b=a+1,d=a+c.SegmentsX+1;face(a,d,b);face(b,d,d+1);}
        JPH::SoftBodySharedSettings::VertexAttributes attrs(c.Compliance,c.ShearCompliance,c.BendCompliance);shared->CreateConstraints(&attrs,1,JPH::SoftBodySharedSettings::EBendType::Dihedral);
    }else{
        entry.InverseMass=float((c.Segments+1)*c.Sides)/c.Mass;
        for(unsigned y=0;y<=c.Segments;++y)for(unsigned x=0;x<c.Sides;++x){float angle=6.2831853f*x/c.Sides;add({std::cos(angle)*c.Radius,-c.Length*y/c.Segments,std::sin(angle)*c.Radius},{float(x)/c.Sides,float(y)/c.Segments},(c.PinStart&&y==0)||(c.PinEnd&&y==c.Segments));}
        for(unsigned y=0;y<c.Segments;++y)for(unsigned x=0;x<c.Sides;++x){unsigned a=y*c.Sides+x,b=y*c.Sides+(x+1)%c.Sides,d=a+c.Sides;face(a,b,d);face(b,b+c.Sides,d);}
        JPH::SoftBodySharedSettings::VertexAttributes attrs(c.Compliance,c.Compliance,c.BendCompliance);shared->CreateConstraints(&attrs,1,JPH::SoftBodySharedSettings::EBendType::Dihedral);
    }
    shared->Optimize();JPH::SoftBodyCreationSettings settings(shared,V(pose.Position),Q(pose.Rotation),JPH::ObjectLayer(c.Layer));
    settings.mMakeRotationIdentity=false;settings.mUpdatePosition=false;settings.mAllowSleeping=c.AllowSleeping&&w.Settings.AllowSleeping;settings.mNumIterations=c.Iterations;settings.mLinearDamping=c.Damping;settings.mMaxLinearVelocity=c.MaxVelocity;settings.mGravityFactor=c.GravityFactor;settings.mFriction=c.Friction;settings.mRestitution=c.Restitution;
    auto* body=w.System.GetBodyInterface().CreateSoftBody(settings);if(!body)throw std::runtime_error("Physics body capacity exhausted while creating soft body");entry.Id=body->GetID();w.System.GetBodyInterface().AddBody(entry.Id,JPH::EActivation::Activate);ColliderData data;data.Body=data.Collider=entity.GetUUID();data.Layer=c.Layer;data.Friction=c.Friction;data.Restitution=c.Restitution;w.SoftColliders.emplace(entry.Id.GetIndexAndSequenceNumber(),data);return entry;
}
template<class T> static void SyncSoftPool(World& w,std::set<UUID>& retained){
    for(auto handle:w.ScenePtr->GetRegistry().view<T>()){
        auto e=w.ScenePtr->GetEntity(static_cast<Entity::Id>(handle));const auto& c=e.template GetComponent<T>();if(!c.Enabled)continue;
        if(!c.IsValid())throw std::invalid_argument("Invalid soft body properties");
        if(e.template HasComponent<Rigidbody>()||e.template HasComponent<ShapeCollider>()||e.template HasComponent<MeshCollider>()||(e.template HasComponent<Cloth>()&&e.template HasComponent<Rope>()))throw std::invalid_argument("A soft body cannot share its entity with another physics body or collider");
        const auto id=e.GetUUID();retained.insert(id);std::string signature,configuration;
        AppendPhysicsKey(signature,c.Mass);AppendPhysicsKey(signature,c.Compliance);AppendPhysicsKey(signature,c.BendCompliance);AppendPhysicsKey(signature,c.Layer);
        if constexpr(std::is_same_v<T,Cloth>){AppendPhysicsKey(signature,c.Width);AppendPhysicsKey(signature,c.Height);AppendPhysicsKey(signature,c.SegmentsX);AppendPhysicsKey(signature,c.SegmentsY);AppendPhysicsKey(signature,c.PinEdges);AppendPhysicsKey(signature,c.ShearCompliance);VisitCloth(c,[&](const char*,const char*,const auto& f){AppendPhysicsKey(configuration,f);});}
        else{AppendPhysicsKey(signature,c.Length);AppendPhysicsKey(signature,c.Radius);AppendPhysicsKey(signature,c.Segments);AppendPhysicsKey(signature,c.Sides);AppendPhysicsKey(signature,c.PinStart);AppendPhysicsKey(signature,c.PinEnd);VisitRope(c,[&](const char*,const char*,const auto& f){AppendPhysicsKey(configuration,f);});}
        AppendPhysicsKey(signature,e.GetWorldTransform().Scale);
        auto it=w.SoftBodies.find(id);
        if(it==w.SoftBodies.end()||it->second.Signature!=signature){if(it!=w.SoftBodies.end()){w.System.GetBodyInterface().RemoveBody(it->second.Id);w.System.GetBodyInterface().DestroyBody(it->second.Id);w.SoftColliders.erase(it->second.Id.GetIndexAndSequenceNumber());it->second.Mesh.Destroy();w.SoftBodies.erase(it);}w.SoftBodies.emplace(id,CreateSoft(w,e,c,signature));}
        else{auto pose=e.GetWorldTransform();if(pose.Position!=it->second.Pose.Position||PhysicsKey(pose.Rotation)!=PhysicsKey(it->second.Pose.Rotation)){w.System.GetBodyInterface().SetPositionAndRotation(it->second.Id,V(pose.Position),Q(pose.Rotation),JPH::EActivation::Activate);it->second.Pose=pose;}}
        auto& entry=w.SoftBodies.at(id);if(entry.Configuration!=configuration){auto* body=w.System.GetBodyLockInterfaceNoLock().TryGetBody(entry.Id);auto* motion=static_cast<JPH::SoftBodyMotionProperties*>(body->GetMotionProperties());motion->SetNumIterations(c.Iterations);motion->SetGravityFactor(c.GravityFactor);motion->SetLinearDamping(c.Damping);motion->SetMaxLinearVelocity(c.MaxVelocity);body->SetFriction(c.Friction);body->SetRestitution(c.Restitution);body->SetAllowSleeping(c.AllowSleeping&&w.Settings.AllowSleeping);entry.Wind=c.Wind;entry.Material=c.MaterialAsset;entry.CastShadows=c.CastShadows;entry.Configuration=std::move(configuration);w.System.GetBodyInterface().ActivateBody(entry.Id);}
    }
}
void World::SyncExtras(){
    std::set<JointKey> retained;Suppressed.clear();
    SyncJointPool<Joint>(*this,0,retained);SyncJointPool<DistanceJoint>(*this,1,retained);SyncJointPool<HingeJoint>(*this,2,retained);SyncJointPool<SliderJoint>(*this,3,retained);SyncJointPool<BallAndSocketJoint>(*this,4,retained);SyncJointPool<FixedJoint>(*this,5,retained);
    for(auto it=Joints.begin();it!=Joints.end();)if(!retained.contains(it->first)){if(it->second.Native)System.RemoveConstraint(it->second.Native);it=Joints.erase(it);}else ++it;
    std::set<UUID> soft;SyncSoftPool<Cloth>(*this,soft);SyncSoftPool<Rope>(*this,soft);
    for(auto it=SoftBodies.begin();it!=SoftBodies.end();)if(!soft.contains(it->first)){System.GetBodyInterface().RemoveBody(it->second.Id);System.GetBodyInterface().DestroyBody(it->second.Id);SoftColliders.erase(it->second.Id.GetIndexAndSequenceNumber());it->second.Mesh.Destroy();it=SoftBodies.erase(it);}else ++it;
}
void World::UpdateExtras(float dt){
    for(auto& [key,j]:Joints){if(!j.Native||j.Broken)continue;float force=0,torque=0;
        switch(j.Settings.Type){
        case JointType::Distance:force=std::abs(static_cast<JPH::DistanceConstraint*>(j.Native.GetPtr())->GetTotalLambdaPosition());break;
        case JointType::Hinge:{auto* p=static_cast<JPH::HingeConstraint*>(j.Native.GetPtr());force=p->GetTotalLambdaPosition().Length();auto r=p->GetTotalLambdaRotation();torque=std::sqrt(r[0]*r[0]+r[1]*r[1])+std::abs(p->GetTotalLambdaMotor())+std::abs(p->GetTotalLambdaRotationLimits());break;}
        case JointType::Slider:{auto* p=static_cast<JPH::SliderConstraint*>(j.Native.GetPtr());auto r=p->GetTotalLambdaPosition();force=std::sqrt(r[0]*r[0]+r[1]*r[1])+std::abs(p->GetTotalLambdaPositionLimits())+std::abs(p->GetTotalLambdaMotor());torque=p->GetTotalLambdaRotation().Length();break;}
        case JointType::BallAndSocket:force=static_cast<JPH::PointConstraint*>(j.Native.GetPtr())->GetTotalLambdaPosition().Length();break;
        default:{auto* p=static_cast<JPH::FixedConstraint*>(j.Native.GetPtr());force=p->GetTotalLambdaPosition().Length();torque=p->GetTotalLambdaRotation().Length();break;}}
        if((j.Settings.BreakForce>0&&force/dt>j.Settings.BreakForce)||(j.Settings.BreakTorque>0&&torque/dt>j.Settings.BreakTorque)){j.Broken=true;j.Native->SetEnabled(false);}
    }
}
static std::vector<Vec3> SoftVertices(World& w,Entity entity){
    if(!entity||entity.GetScene()!=w.ScenePtr)return {};if(w.TopologyDirty)w.Synchronize();auto it=w.SoftBodies.find(entity.GetUUID());if(it==w.SoftBodies.end())return {};
    auto* body=w.System.GetBodyLockInterfaceNoLock().TryGetBody(it->second.Id);auto* motion=static_cast<JPH::SoftBodyMotionProperties*>(body->GetMotionProperties());std::vector<Vec3> result;result.reserve(motion->GetVertices().size());for(auto& v:motion->GetVertices())result.push_back(V(body->GetCenterOfMassTransform()*v.mPosition));return result;
}
static bool SoftControl(World& w,Entity e,Vec3 value,int index,int operation){
    RequireVector(value);if(!e||e.GetScene()!=w.ScenePtr||index< -1)return false;if(w.TopologyDirty)w.Synchronize();auto it=w.SoftBodies.find(e.GetUUID());if(it==w.SoftBodies.end())return false;
    auto* body=w.System.GetBodyLockInterfaceNoLock().TryGetBody(it->second.Id);auto* motion=static_cast<JPH::SoftBodyMotionProperties*>(body->GetMotionProperties());auto& vertices=motion->GetVertices();if(index>=int(vertices.size()))return false;
    if(operation==0){auto impulse=body->GetRotation().Conjugated()*V(value);if(index>=0){auto& v=vertices[index];v.mVelocity+=impulse*v.mInvMass;}else{unsigned count=0;for(auto& v:vertices)if(v.mInvMass>0)++count;if(count)for(auto& v:vertices)if(v.mInvMass>0)v.mVelocity+=impulse*(v.mInvMass/count);}}
    else{if(index<0)return false;auto& v=vertices[index];v.mInvMass=operation==1?0:it->second.InverseMass;v.mVelocity=JPH::Vec3::sZero();}
    w.System.GetBodyInterface().ActivateBody(it->second.Id);return true;
}
void World::DrawSoftBodies(){
    for(auto& [id,entry]:SoftBodies){auto entity=ScenePtr->GetEntity(id);if(!entity)continue;auto* body=System.GetBodyLockInterfaceNoLock().TryGetBody(entry.Id);auto* motion=static_cast<JPH::SoftBodyMotionProperties*>(body->GetMotionProperties());bool changed=!entry.Mesh.IsValid();const auto& vertices=motion->GetVertices();for(std::size_t i=0;i<vertices.size();++i){auto position=V(body->GetCenterOfMassTransform()*vertices[i].mPosition);changed|=entry.Vertices[i].Position!=position;entry.Vertices[i].Position=position;entry.Vertices[i].Normal={};}
        for(std::size_t i=0;i<entry.Indices.size();i+=3){auto a=entry.Indices[i],b=entry.Indices[i+1],c=entry.Indices[i+2];auto normal=Vec3::Cross(entry.Vertices[b].Position-entry.Vertices[a].Position,entry.Vertices[c].Position-entry.Vertices[a].Position);entry.Vertices[a].Normal+=normal;entry.Vertices[b].Normal+=normal;entry.Vertices[c].Normal+=normal;}
        for(auto& vertex:entry.Vertices)vertex.Normal=vertex.Normal.LengthSquared()>1e-12f?vertex.Normal.Normalized():Vec3{0,0,1};if(!entry.Mesh.IsValid())entry.Mesh=Graphics::CreateMesh(entry.Vertices,entry.Indices);else if(changed)entry.Mesh.SetGeometry(entry.Vertices,entry.Indices);
        GraphicsDrawOptions options;options.CastShadows=entry.CastShadows;GraphicsAccess::DrawEntity(entry.Mesh,entry.Material,id,options);
    }
}
