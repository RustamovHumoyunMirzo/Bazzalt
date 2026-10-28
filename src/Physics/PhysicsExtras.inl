// Included inside the physics module's private namespace after World is defined.
void World::DetachJoints(JPH::BodyID body){
    for(auto& [key,j]:Joints)if(j.Native&&(j.A==body||j.B==body)){System.RemoveConstraint(j.Native);j.Native=nullptr;}
    Suppressed.clear();
}
void World::ClearExtras(){
    for(auto& [key,j]:Joints)if(j.Native)System.RemoveConstraint(j.Native);
    Joints.clear();Suppressed.clear();
    for(auto& [id,s]:SoftBodies){System.GetBodyInterface().RemoveBody(s.Id);System.GetBodyInterface().DestroyBody(s.Id);SoftColliders.erase(s.Id.GetIndexAndSequenceNumber());s.DestroyMeshes();}
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
static bool ClosedSoftSurface(const JPH::SoftBodySharedSettings& shared){
    const auto count=shared.mVertices.size();std::vector<unsigned> parent(count);std::iota(parent.begin(),parent.end(),0);
    const auto root=[&](unsigned index){while(parent[index]!=index){parent[index]=parent[parent[index]];index=parent[index];}return index;};
    std::map<std::pair<unsigned,unsigned>,std::pair<unsigned,int>> edges;
    for(auto& face:shared.mFaces)for(int i=0;i<3;++i){unsigned a=face.mVertex[i],b=face.mVertex[(i+1)%3];auto& edge=edges[{std::min(a,b),std::max(a,b)}];++edge.first;edge.second+=a<b?1:-1;parent[root(a)]=root(b);}
    if(edges.empty()||std::any_of(edges.begin(),edges.end(),[](const auto& e){return e.second.first!=2||e.second.second!=0;}))return false;
    std::map<unsigned,double> volumes;
    for(auto& face:shared.mFaces){auto id=root(face.mVertex[0]);auto origin=shared.mVertices[id].mPosition;const auto point=[&](unsigned index){const auto& p=shared.mVertices[index].mPosition;return std::array<double,3>{double(p.x)-origin.x,double(p.y)-origin.y,double(p.z)-origin.z};};auto a=point(face.mVertex[0]),b=point(face.mVertex[1]),c=point(face.mVertex[2]);volumes[id]+=a[0]*(b[1]*c[2]-b[2]*c[1])+a[1]*(b[2]*c[0]-b[0]*c[2])+a[2]*(b[0]*c[1]-b[1]*c[0]);}
    return std::all_of(volumes.begin(),volumes.end(),[](auto& v){return std::isfinite(v.second)&&v.second>1e-9;});
}
template<class T> static SoftEntry CreateSoft(World& w,Entity entity,const T& c,const std::string& signature){
    auto pose=entity.GetWorldTransform();RequireVector(pose.Position);RequireVector(pose.Scale);(void)Q(pose.Rotation);if(pose.Scale.X<=0||pose.Scale.Y<=0||pose.Scale.Z<=0)throw std::invalid_argument("Soft bodies require positive world scale");
    SoftEntry entry;entry.Signature=signature;entry.Pose=pose;entry.RestScale=pose.Scale;entry.Wind=c.Wind;entry.Material=c.MaterialAsset;entry.CastShadows=c.CastShadows;
    JPH::Ref<JPH::SoftBodySharedSettings> shared=new JPH::SoftBodySharedSettings;
    SoftSurface surface;
    const auto add=[&](Vec3 p,Vec2 uv,bool pin){p=p*pose.Scale;RequireVector(p);if(!std::isfinite(entry.InverseMass)||entry.InverseMass<=0)throw std::invalid_argument("Soft body mass is out of range");shared->mVertices.emplace_back(JPH::Float3(p.X,p.Y,p.Z),JPH::Float3(0,0,0),pin?0:entry.InverseMass);surface.Particles.push_back(unsigned(surface.Vertices.size()));surface.Vertices.push_back({p,{0,0,1},uv,{1,1,1,1}});};
    const auto face=[&](unsigned a,unsigned b,unsigned d){shared->AddFace({a,b,d});entry.Indices.insert(entry.Indices.end(),{a,b,d});surface.Indices.insert(surface.Indices.end(),{a,b,d});};
    if constexpr(std::is_same_v<T,Cloth>){
        std::vector<SoftMeshPart> sources;if(!ReadSoftMesh(entity,sources))throw std::invalid_argument("Cloth requires an enabled Primitive Object or triangle Mesh on this entity or its model children");
        RefineSoftMesh(sources,c.SubdivisionLevel);
        Vec3 low{std::numeric_limits<float>::max()},high{-std::numeric_limits<float>::max()};for(auto& source:sources)for(auto& v:source.Vertices){auto p=v.Position*pose.Scale;RequireVector(p);low={std::min(low.X,p.X),std::min(low.Y,p.Y),std::min(low.Z,p.Z)};high={std::max(high.X,p.X),std::max(high.Y,p.Y),std::max(high.Z,p.Z)};}
        const float weld=std::max(1e-7f,(high-low).Length()*1e-6f);if(!std::isfinite(weld))throw std::invalid_argument("Cloth source extents are out of range");std::map<std::array<int,3>,std::vector<unsigned>> cells;
        const auto particle=[&](Vec3 point){auto d=(point-low)/weld;std::array<int,3> cell{int(std::floor(d.X)),int(std::floor(d.Y)),int(std::floor(d.Z))};for(int x=-1;x<=1;++x)for(int y=-1;y<=1;++y)for(int z=-1;z<=1;++z){auto found=cells.find({cell[0]+x,cell[1]+y,cell[2]+z});if(found!=cells.end())for(auto index:found->second){const auto& p=shared->mVertices[index].mPosition;if((Vec3{p.x,p.y,p.z}-point).LengthSquared()<=weld*weld)return index;}}if(shared->mVertices.size()>=16384)throw std::length_error("Cloth source exceeds 16384 welded particles");auto index=unsigned(shared->mVertices.size());shared->mVertices.emplace_back(JPH::Float3(point.X,point.Y,point.Z));cells[cell].push_back(index);return index;};
        for(auto& source:sources){SoftSurface render;render.Source=source.Owner;render.Node=source.Node;render.Slot=source.Slot;render.Vertices=std::move(source.Vertices);render.Indices=std::move(source.Indices);
            for(auto& v:render.Vertices){auto point=v.Position*pose.Scale;RequireVector(point);render.Particles.push_back(particle(point));}
            std::map<std::array<int,4>,unsigned> smoothing;for(std::size_t i=0;i<render.Vertices.size();++i){const auto n=render.Vertices[i].Normal;auto [group,_]=smoothing.emplace(std::array<int,4>{int(render.Particles[i]),int(std::round(n.X*10000)),int(std::round(n.Y*10000)),int(std::round(n.Z*10000))},unsigned(smoothing.size()));render.SmoothingGroups.push_back(group->second);}
            for(std::size_t i=0;i+2<render.Indices.size();i+=3){unsigned a=render.Particles[render.Indices[i]],b=render.Particles[render.Indices[i+1]],d=render.Particles[render.Indices[i+2]];if(a==b||b==d||a==d)continue;shared->AddFace({a,b,d});entry.Indices.insert(entry.Indices.end(),{a,b,d});}
            entry.Surfaces.push_back(std::move(render));
        }
        if(shared->mFaces.empty())throw std::invalid_argument("Cloth source has no nondegenerate triangles");
        entry.InverseMass=float(shared->mVertices.size())/c.Mass;if(!std::isfinite(entry.InverseMass))throw std::invalid_argument("Cloth mass is out of range");
        const auto tolerance=std::max(1e-6f,(high-low).Length()*1e-5f);
        for(auto& v:shared->mVertices){const auto& p=v.mPosition;bool pinned=((c.PinEdges&1)&&high.X-low.X>tolerance&&p.x<=low.X+tolerance)||((c.PinEdges&2)&&high.X-low.X>tolerance&&p.x>=high.X-tolerance)||((c.PinEdges&4)&&(high.Y-low.Y>tolerance?p.y>=high.Y-tolerance:high.Z-low.Z>tolerance&&p.z>=high.Z-tolerance))||((c.PinEdges&8)&&(high.Y-low.Y>tolerance?p.y<=low.Y+tolerance:high.Z-low.Z>tolerance&&p.z<=low.Z+tolerance));v.mInvMass=pinned?0:entry.InverseMass;}
        entry.Closed=ClosedSoftSurface(*shared);if(c.Pressure>0&&!entry.Closed)throw std::invalid_argument("Cloth pressure requires a closed mesh with consistent outward triangle winding");shared->mVertexRadius=c.CollisionRadius;
        JPH::SoftBodySharedSettings::VertexAttributes attrs(c.Compliance,c.ShearCompliance,c.BendCompliance);shared->CreateConstraints(&attrs,1,JPH::SoftBodySharedSettings::EBendType(c.BendMode));
    }else{
        entry.InverseMass=float((c.Segments+1)*c.Sides)/c.Mass;
        for(unsigned y=0;y<=c.Segments;++y)for(unsigned x=0;x<c.Sides;++x){float angle=6.2831853f*x/c.Sides;add({std::cos(angle)*c.Radius,-c.Length*y/c.Segments,std::sin(angle)*c.Radius},{float(x)/c.Sides,float(y)/c.Segments},(c.PinStart&&y==0)||(c.PinEnd&&y==c.Segments));}
        for(unsigned y=0;y<c.Segments;++y)for(unsigned x=0;x<c.Sides;++x){unsigned a=y*c.Sides+x,b=y*c.Sides+(x+1)%c.Sides,d=a+c.Sides;face(a,b,d);face(b,b+c.Sides,d);}
        JPH::SoftBodySharedSettings::VertexAttributes attrs(c.Compliance,c.Compliance,c.BendCompliance);shared->CreateConstraints(&attrs,1,JPH::SoftBodySharedSettings::EBendType::Dihedral);
    }
    if constexpr(std::is_same_v<T,Rope>)entry.Surfaces.push_back(std::move(surface));
    // Center the simulation frame without changing the source entity's pivot.
    Vec3 minimum{std::numeric_limits<float>::max()},maximum{-std::numeric_limits<float>::max()};
    for(auto& vertex:shared->mVertices){const auto& p=vertex.mPosition;minimum={std::min(minimum.X,p.x),std::min(minimum.Y,p.y),std::min(minimum.Z,p.z)};maximum={std::max(maximum.X,p.x),std::max(maximum.Y,p.y),std::max(maximum.Z,p.z)};}
    entry.ReferenceCenter=(minimum+maximum)*.5f;
    for(auto& vertex:shared->mVertices){vertex.mPosition.x-=entry.ReferenceCenter.X;vertex.mPosition.y-=entry.ReferenceCenter.Y;vertex.mPosition.z-=entry.ReferenceCenter.Z;}
    shared->Optimize();JPH::SoftBodyCreationSettings settings(shared,V(pose.Position+pose.Rotation.Rotate(entry.ReferenceCenter)),Q(pose.Rotation),JPH::ObjectLayer(c.Layer));
    if constexpr(std::is_same_v<T,Cloth>)settings.mPressure=c.Pressure;
    settings.mMakeRotationIdentity=false;settings.mUpdatePosition=true;settings.mAllowSleeping=c.AllowSleeping&&w.Settings.AllowSleeping;settings.mNumIterations=c.Iterations;settings.mLinearDamping=c.Damping;settings.mMaxLinearVelocity=c.MaxVelocity;settings.mGravityFactor=c.GravityFactor;settings.mFriction=c.Friction;settings.mRestitution=c.Restitution;
    auto* body=w.System.GetBodyInterface().CreateSoftBody(settings);if(!body)throw std::runtime_error("Physics body capacity exhausted while creating soft body");entry.Id=body->GetID();w.System.GetBodyInterface().AddBody(entry.Id,JPH::EActivation::Activate);ColliderData data;data.Body=data.Collider=entity.GetUUID();data.Layer=c.Layer;data.Friction=c.Friction;data.Restitution=c.Restitution;w.SoftColliders.emplace(entry.Id.GetIndexAndSequenceNumber(),data);return entry;
}
template<class T> static void SyncSoftPool(World& w,std::set<UUID>& retained){
    for(auto handle:w.ScenePtr->GetRegistry().view<T>()){
        auto e=w.ScenePtr->GetEntity(static_cast<Entity::Id>(handle));const auto& c=e.template GetComponent<T>();if(!c.Enabled){w.SoftDiagnostics.erase(e.GetUUID());continue;}
        try{
        if(!c.IsValid())throw std::invalid_argument("Invalid soft body properties");
        if(e.template HasComponent<Rigidbody>()||e.template HasComponent<ShapeCollider>()||e.template HasComponent<MeshCollider>()||(e.template HasComponent<Cloth>()&&e.template HasComponent<Rope>()))throw std::invalid_argument("A soft body cannot share its entity with another physics body or collider");
        const auto id=e.GetUUID();retained.insert(id);std::string signature,configuration;
        AppendPhysicsKey(signature,c.Mass);AppendPhysicsKey(signature,c.Compliance);AppendPhysicsKey(signature,c.BendCompliance);AppendPhysicsKey(signature,c.Layer);
        if constexpr(std::is_same_v<T,Cloth>){PhysicsMeshRevisions revisions;signature+=PhysicsMeshSignature(e,{},revisions);AppendPhysicsKey(signature,c.BendMode);AppendPhysicsKey(signature,c.SubdivisionLevel);AppendPhysicsKey(signature,c.CollisionRadius);AppendPhysicsKey(signature,c.PinEdges);AppendPhysicsKey(signature,c.ShearCompliance);VisitCloth(c,[&](const char*,const char*,const auto& f){AppendPhysicsKey(configuration,f);});}
        else{AppendPhysicsKey(signature,c.Length);AppendPhysicsKey(signature,c.Radius);AppendPhysicsKey(signature,c.Segments);AppendPhysicsKey(signature,c.Sides);AppendPhysicsKey(signature,c.PinStart);AppendPhysicsKey(signature,c.PinEnd);VisitRope(c,[&](const char*,const char*,const auto& f){AppendPhysicsKey(configuration,f);});}
        auto it=w.SoftBodies.find(id);auto scale=e.GetWorldTransform().Scale;
        if(it!=w.SoftBodies.end()){const auto rest=it->second.RestScale;const auto close=[](float a,float b){return std::abs(a-b)<=1e-6f*std::max({1.f,std::abs(a),std::abs(b)});};if(close(scale.X,rest.X)&&close(scale.Y,rest.Y)&&close(scale.Z,rest.Z))scale=rest;}
        AppendPhysicsKey(signature,scale);
        if(it==w.SoftBodies.end()||it->second.Signature!=signature){if(it!=w.SoftBodies.end()){w.System.GetBodyInterface().RemoveBody(it->second.Id);w.System.GetBodyInterface().DestroyBody(it->second.Id);w.SoftColliders.erase(it->second.Id.GetIndexAndSequenceNumber());it->second.DestroyMeshes();w.SoftBodies.erase(it);}w.SoftBodies.emplace(id,CreateSoft(w,e,c,signature));}
        else{auto pose=e.GetWorldTransform();if(pose.Position!=it->second.Pose.Position||PhysicsKey(pose.Rotation)!=PhysicsKey(it->second.Pose.Rotation)){w.System.GetBodyInterface().SetPositionAndRotation(it->second.Id,V(pose.Position+pose.Rotation.Rotate(it->second.ReferenceCenter)),Q(pose.Rotation),JPH::EActivation::Activate);it->second.Pose=pose;}}
        auto& entry=w.SoftBodies.at(id);if constexpr(std::is_same_v<T,Cloth>)if(c.Pressure>0&&!entry.Closed)throw std::invalid_argument("Cloth pressure requires a closed mesh with consistent outward triangle winding");if(entry.Configuration!=configuration){auto* body=w.System.GetBodyLockInterfaceNoLock().TryGetBody(entry.Id);auto* motion=static_cast<JPH::SoftBodyMotionProperties*>(body->GetMotionProperties());if constexpr(std::is_same_v<T,Cloth>)motion->SetPressure(c.Pressure);motion->SetNumIterations(c.Iterations);motion->SetGravityFactor(c.GravityFactor);motion->SetLinearDamping(c.Damping);motion->SetMaxLinearVelocity(c.MaxVelocity);body->SetFriction(c.Friction);body->SetRestitution(c.Restitution);body->SetAllowSleeping(c.AllowSleeping&&w.Settings.AllowSleeping);entry.Wind=c.Wind;entry.Material=c.MaterialAsset;entry.CastShadows=c.CastShadows;entry.Configuration=std::move(configuration);w.System.GetBodyInterface().ActivateBody(entry.Id);}
        w.SoftDiagnostics.erase(id);
        }catch(const std::exception& error){
            if(!w.Preview)throw;
            retained.erase(e.GetUUID());auto& diagnostic=w.SoftDiagnostics[e.GetUUID()];if(diagnostic!=error.what()){diagnostic=error.what();Console::Error("Soft body "+e.GetUUID().ToString()+": "+diagnostic,"Physics");}
        }
    }
}
void World::SyncExtras(){
    std::erase_if(SoftDiagnostics,[&](const auto& item){return !ScenePtr->GetEntity(item.first);});
    std::set<JointKey> retained;Suppressed.clear();
    SyncJointPool<Joint>(*this,0,retained);SyncJointPool<DistanceJoint>(*this,1,retained);SyncJointPool<HingeJoint>(*this,2,retained);SyncJointPool<SliderJoint>(*this,3,retained);SyncJointPool<BallAndSocketJoint>(*this,4,retained);SyncJointPool<FixedJoint>(*this,5,retained);
    for(auto it=Joints.begin();it!=Joints.end();)if(!retained.contains(it->first)){if(it->second.Native)System.RemoveConstraint(it->second.Native);it=Joints.erase(it);}else ++it;
    std::set<UUID> soft;SyncSoftPool<Cloth>(*this,soft);SyncSoftPool<Rope>(*this,soft);
    for(auto it=SoftBodies.begin();it!=SoftBodies.end();)if(!soft.contains(it->first)){System.GetBodyInterface().RemoveBody(it->second.Id);System.GetBodyInterface().DestroyBody(it->second.Id);SoftColliders.erase(it->second.Id.GetIndexAndSequenceNumber());it->second.DestroyMeshes();it=SoftBodies.erase(it);}else ++it;
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
    for(auto& [id,entry]:SoftBodies){auto entity=ScenePtr->GetEntity(id);if(!entity)continue;auto* body=System.GetBodyLockInterfaceNoLock().TryGetBody(entry.Id);auto* motion=static_cast<JPH::SoftBodyMotionProperties*>(body->GetMotionProperties());const auto& vertices=motion->GetVertices();
        for(auto& surface:entry.Surfaces){
            GraphicsDrawOptions options;options.CastShadows=entry.CastShadows;
            if(surface.Source){auto source=ScenePtr->GetEntity(surface.Source);if(!source)continue;if(auto* p=source.TryGetComponent<PrimitiveObject>()){if(!p->Enabled||!p->Visible)continue;options.LayerMask=p->LayerMask&0x7f;options.CastShadows&=p->CastShadows;options.ReceiveShadows=p->ReceiveShadows;}else if(auto* m=source.TryGetComponent<Mesh>()){if(!m->Enabled||!m->Visible)continue;options.LayerMask=m->LayerMask&0x7f;options.CastShadows&=m->CastShadows;options.ReceiveShadows=m->ReceiveShadows;}}
            bool changed=!surface.Mesh.IsValid();for(std::size_t i=0;i<surface.Vertices.size();++i){auto position=V(body->GetCenterOfMassTransform()*vertices[surface.Particles[i]].mPosition);changed|=surface.Vertices[i].Position!=position;surface.Vertices[i].Position=position;surface.Vertices[i].Normal={};}
            for(std::size_t i=0;i<surface.Indices.size();i+=3){auto a=surface.Indices[i],b=surface.Indices[i+1],c=surface.Indices[i+2];auto normal=Vec3::Cross(surface.Vertices[b].Position-surface.Vertices[a].Position,surface.Vertices[c].Position-surface.Vertices[a].Position);surface.Vertices[a].Normal+=normal;surface.Vertices[b].Normal+=normal;surface.Vertices[c].Normal+=normal;}
            if(!surface.SmoothingGroups.empty()){std::vector<Vec3> smooth(surface.Vertices.size());for(std::size_t i=0;i<surface.Vertices.size();++i)smooth[surface.SmoothingGroups[i]]+=surface.Vertices[i].Normal;for(std::size_t i=0;i<surface.Vertices.size();++i)surface.Vertices[i].Normal=smooth[surface.SmoothingGroups[i]];}
            for(auto& vertex:surface.Vertices)vertex.Normal=vertex.Normal.LengthSquared()>1e-12f?vertex.Normal.Normalized():Vec3{0,0,1};if(!surface.Mesh.IsValid())surface.Mesh=Graphics::CreateMesh(surface.Vertices,surface.Indices);else if(changed)surface.Mesh.SetGeometry(surface.Vertices,surface.Indices);
            GraphicsAccess::DrawEntity(surface.Mesh,entry.Material,id,options,surface.Source,surface.Node,surface.Slot);
        }
    }
}
