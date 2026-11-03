#include "VFX/VFXInternal.h"
#include "Bazzalt/AssetManager.h"
#include <ryml.hpp>
#include <fstream>
#include <sstream>
#include <locale>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>
#include <cctype>
namespace Bazzalt {
namespace {
using Node=ryml::ConstNodeRef;
std::string Text(Node n){if(!n.has_val())throw std::invalid_argument("Expected VFX scalar");auto s=n.val();return {s.str,s.len};}
Node Child(Node n,const char* key){if(!n.is_map()||!n.has_child(key))throw std::invalid_argument(std::string("Missing VFX field: ")+key);return n[key];}
float Number(Node n){auto text=Text(n);std::istringstream stream(text);stream.imbue(std::locale::classic());float value;if(!(stream>>value)||stream.peek()!=std::char_traits<char>::eof()||!std::isfinite(value)||std::abs(value)>1000000)throw std::invalid_argument("Invalid VFX number");return value;}
float Value(Node n,const char* key,float fallback){return n.has_child(key)?Number(n[key]):fallback;}
unsigned Count(Node n,const char* key,unsigned fallback,unsigned max){float v=Value(n,key,float(fallback));if(v<0||v>max||std::floor(v)!=v)throw std::invalid_argument("Invalid VFX count");return unsigned(v);}
bool Bool(Node n,const char* key,bool fallback){if(!n.has_child(key))return fallback;auto s=Text(n[key]);if(s!="true"&&s!="false")throw std::invalid_argument("Invalid VFX boolean");return s=="true";}
std::vector<float> Vector(Node n,const char* key,unsigned size,std::vector<float> fallback){if(!n.has_child(key))return fallback;auto v=n[key];if(!v.is_seq()||v.num_children()!=size)throw std::invalid_argument("Invalid VFX vector");std::vector<float> out;for(auto p:v.children())out.push_back(Number(p));return out;}
Vec3 V3(Node n,const char* key,Vec3 fallback={}){auto v=Vector(n,key,3,{fallback.X,fallback.Y,fallback.Z});return {v[0],v[1],v[2]};}
Vec4 V4(Node n,const char* key,Vec4 fallback){auto v=Vector(n,key,4,{fallback.X,fallback.Y,fallback.Z,fallback.W});return {v[0],v[1],v[2],v[3]};}
UUID Asset(Node n,const char* key){if(!n.has_child(key))return {};UUID id;if(!UUID::TryParse(Text(n[key]),id))throw std::invalid_argument("Invalid VFX asset UUID");return id;}
std::string Source(UUID id){auto asset=AssetManager::GetAsset(id);if(!asset||asset->State!=AssetState::Ready||[&]{auto extension=asset->SourcePath.extension().string();std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return char(std::tolower(c));});return extension!=".vfx";}())throw std::invalid_argument("VFX import is missing or not a .vfx asset");auto path=std::filesystem::exists(asset->CachePath)?asset->CachePath:asset->SourcePath;std::ifstream file(path,std::ios::binary);std::string text(1024*1024+1,'\0');file.read(text.data(),text.size());text.resize(std::size_t(file.gcount()));if(!file||text.empty()||text.size()>1024*1024){if(!file.eof()||text.empty()||text.size()>1024*1024)throw std::invalid_argument("VFX source cannot be read or exceeds 1 MiB");}return text;}
struct Compiler {
    Runtime::VFXProgram Program;std::unordered_set<UUID> Stack;unsigned Nodes=0,Spawners=0;bool Resolve=true;
    void Compile(const std::string& source,unsigned depth=0){
        if(source.empty()||source.size()>1024*1024||depth>16)throw std::invalid_argument("VFX import depth/source limit exceeded");
        auto tree=ryml::parse_in_arena(ryml::csubstr(source.data(),source.size()));if(tree.size()>16384)throw std::invalid_argument("Too many VFX values");auto root=tree.crootref();if(Number(Child(root,"version"))!=1)throw std::invalid_argument("Unsupported VFX version");auto nodes=Child(root,"nodes");if(!nodes.is_seq()||nodes.num_children()>256)throw std::invalid_argument("Invalid VFX node list");
        std::unordered_map<std::string,Node> index;std::string output;
        for(auto node:nodes.children()){auto id=Text(Child(node,"id"));if(id.empty()||id.size()>64||!index.emplace(id,node).second)throw std::invalid_argument("Duplicate VFX node id");if(Text(Child(node,"type"))=="Output"){if(!output.empty())throw std::invalid_argument("VFX needs one Output");output=id;}}
        if(output.empty())throw std::invalid_argument("VFX needs one Output");
        std::unordered_set<std::string> active,done;
        const auto visit=[&](auto&& self,const std::string& id)->void {
            if(done.contains(id))return;if(!active.insert(id).second)throw std::invalid_argument("VFX node cycle");auto found=index.find(id);if(found==index.end())throw std::invalid_argument("Missing VFX input node");auto node=found->second;auto type=Text(Child(node,"type"));auto inputs=Child(node,"inputs");if(!inputs.is_map()||inputs.num_children()>1||(inputs.num_children()&&!inputs.has_child("in")))throw std::invalid_argument("VFX nodes accept one flow input");if(inputs.has_child("in"))self(self,Text(inputs["in"]));auto values=Child(node,"values");if(!values.is_map())throw std::invalid_argument("Invalid VFX node values");if(++Nodes>1024)throw std::invalid_argument("Expanded VFX exceeds 1024 nodes");auto& p=Program;
            if(type=="Spawn") {if(++Spawners>1)throw std::invalid_argument("A VFX pipeline may contain only one Spawn");p.Rate=Value(values,"rate",20);p.Duration=Value(values,"duration",5);p.Delay=Value(values,"delay",0);p.Burst=Count(values,"burst",0,65535);p.Capacity=Count(values,"capacity",4096,32768);p.Loop=Bool(values,"loop",true);}
            else if(type=="Initialize"){p.LifeMin=Value(values,"lifetimeMin",1);p.LifeMax=Value(values,"lifetimeMax",2);p.SpeedMin=Value(values,"speedMin",1);p.SpeedMax=Value(values,"speedMax",2);p.SizeMin=Value(values,"sizeMin",.1f);p.SizeMax=Value(values,"sizeMax",.2f);p.Cone=Value(values,"cone",20);p.AngularSpeed=Value(values,"angularSpeed",0);}
            else if(type=="Box"){p.Box=V3(values,"extents",{.5f,.5f,.5f});p.Radius=0;}
            else if(type=="Sphere"){p.Radius=Value(values,"radius",.5f);p.Box={};}
            else if(type=="Velocity")p.Velocity=V3(values,"velocity");
            else if(type=="Gravity")p.Gravity=V3(values,"acceleration",{0,-9.81f,0});
            else if(type=="Drag")p.Drag=Value(values,"coefficient",1);
            else if(type=="Turbulence"){p.Turbulence=Value(values,"strength",1);p.Frequency=Value(values,"frequency",1);}
            else if(type=="ColorOverLife"){p.StartColor=V4(values,"start",{1,1,1,1});p.EndColor=V4(values,"end",{1,1,1,0});}
            else if(type=="SizeOverLife")p.EndSize=Value(values,"endMultiplier",0);
            else if(type=="Flipbook"){p.Columns=Count(values,"columns",1,64);p.Rows=Count(values,"rows",1,64);}
            else if(type=="Import"){auto id=Asset(values,"asset");if(!id||!Stack.insert(id).second)throw std::invalid_argument("VFX import cycle or empty import");p.Dependencies.push_back(id);if(Resolve)Compile(Source(id),depth+1);Stack.erase(id);}
            else if(type=="Trails"){p.Trails=true;p.TrailLifetime=Value(values,"lifetime",.5f);p.TrailWidth=Value(values,"width",.05f);p.TrailDistance=Value(values,"minDistance",.03f);p.TrailRatio=Value(values,"ratio",1);p.TrailSegments=Count(values,"segments",16,128);p.TrailDieWithParticle=Bool(values,"dieWithParticle",false);}
            else if(type=="Collision"){p.Collisions=true;p.CollisionMode=Count(values,"mode",0,1);p.Restitution=Value(values,"bounce",.5f);p.Friction=Value(values,"friction",.2f);p.CollisionRadius=Value(values,"radius",.05f);p.KillOnCollision=Bool(values,"kill",false);p.PlaneNormal=V3(values,"normal",{0,1,0});p.PlaneDistance=Value(values,"distance",0);p.CollisionLayers=0xffffffffu;if(values.has_child("layerMask")){auto v=Text(values["layerMask"]);std::istringstream in(v);unsigned long long mask;if(!(in>>mask)||in.peek()!=std::char_traits<char>::eof()||mask>0xffffffffULL)throw std::invalid_argument("Invalid collision layer mask");p.CollisionLayers=unsigned(mask);}}
            else if(type=="Physics"){p.SceneGravity=Bool(values,"sceneGravity",true);p.GravityScale=Value(values,"gravityScale",1);p.ParticleMass=Value(values,"mass",.1f);p.TransferImpulse=Bool(values,"transferImpulse",true);}
            else if(type=="Lights"){p.Lights=true;p.LightIntensity=Value(values,"intensity",200);p.LightRange=Value(values,"range",3);p.LightRatio=Value(values,"ratio",1);p.MaxLights=Count(values,"maxLights",8,32);p.LightShadows=Bool(values,"shadows",false);}
            else if(type=="MeshOutput"){p.MeshOutput=true;p.Mesh=Asset(values,"mesh");p.MeshPrimitive=Count(values,"primitive",0,6);float node=Value(values,"node",-1);if(node< -1||node>1000000||std::floor(node)!=node)throw std::invalid_argument("Invalid particle mesh node");p.MeshNode=int(node);p.OutputLit=Bool(values,"lit",true);p.OutputShadows=Bool(values,"shadows",false);}
            else if(type=="Output"){auto material=Asset(values,"material"),texture=Asset(values,"texture");if(material)p.Material=material;if(texture)p.Texture=texture;if(values.has_child("lit"))p.OutputLit=Bool(values,"lit",false);}
            else throw std::invalid_argument("Unknown VFX node: "+type);
            active.erase(id);done.insert(id);
        };visit(visit,output);
        // Unconnected blocks are retained for authoring, but never execute.
        const std::unordered_set<std::string> kinds{"Spawn","Initialize","Box","Sphere","Velocity","Gravity","Drag","Turbulence","ColorOverLife","SizeOverLife","Flipbook","Import","Output","Trails","Collision","Physics","Lights","MeshOutput"};
        for(const auto& [id,node]:index)if(!kinds.contains(Text(Child(node,"type"))))throw std::invalid_argument("Unknown VFX node");
    }
    void Validate(){auto& p=Program;if(p.Rate<0||p.Rate>100000||p.Duration<.01f||p.Duration>3600||p.Delay<0||p.Delay>3600||!p.Capacity||p.LifeMin<=0||p.LifeMax<p.LifeMin||p.LifeMax>600||p.SizeMin<0||p.SizeMax<p.SizeMin||p.SizeMax>10000||p.SpeedMax<p.SpeedMin||p.Cone<0||p.Cone>180||p.Drag<0||p.Turbulence<0||p.Frequency<0||p.EndSize<0||p.EndSize>10000||p.StartColor.W<0||p.StartColor.W>1||p.EndColor.W<0||p.EndColor.W>1||p.Radius<0||p.Box.X<0||p.Box.Y<0||p.Box.Z<0||!p.Columns||!p.Rows)throw std::invalid_argument("Invalid VFX settings range");if(p.TrailLifetime<.01f||p.TrailLifetime>30||p.TrailWidth<=0||p.TrailWidth>1000||p.TrailDistance<.001f||p.TrailDistance>10000||p.TrailSegments<2||p.TrailRatio<0||p.TrailRatio>1||p.Restitution<0||p.Restitution>1||p.Friction<0||p.Friction>1||p.CollisionRadius<=0||p.CollisionRadius>100||p.PlaneNormal.LengthSquared()<1e-8f||p.GravityScale< -100||p.GravityScale>100||p.ParticleMass<=0||p.ParticleMass>10000||p.LightIntensity<0||p.LightIntensity>1000000||p.LightRange<=0||p.LightRange>1000||p.LightRatio<0||p.LightRatio>1||!p.MaxLights)throw std::invalid_argument("Invalid advanced VFX module settings");p.PlaneNormal=p.PlaneNormal.Normalized();for(auto id:{p.Material,p.Texture,p.Mesh})if(id)p.Dependencies.push_back(id);std::sort(p.Dependencies.begin(),p.Dependencies.end());p.Dependencies.erase(std::unique(p.Dependencies.begin(),p.Dependencies.end()),p.Dependencies.end());}
};
}
namespace Runtime {bool VFXAccess::ValidateSource(const std::string& source,std::string& error){try{Compiler compiler;compiler.Resolve=false;compiler.Compile(source);compiler.Validate();error.clear();return true;}catch(const std::exception& e){error=e.what();return false;}}}
bool ParticleEmitter::IsValid() const {return MaxParticles>0&&MaxParticles<=32768&&unsigned(Space)<=1&&unsigned(Backend)<=2&&std::isfinite(SimulationSpeed)&&SimulationSpeed>=0&&SimulationSpeed<=100&&std::isfinite(RateMultiplier)&&RateMultiplier>=0&&RateMultiplier<=100&&std::isfinite(SizeMultiplier)&&SizeMultiplier>=0&&SizeMultiplier<=10000&&std::isfinite(Tint.X)&&std::isfinite(Tint.Y)&&std::isfinite(Tint.Z)&&std::isfinite(Tint.W)&&std::abs(Tint.X)<=65504&&std::abs(Tint.Y)<=65504&&std::abs(Tint.Z)<=65504&&Tint.W>=0&&Tint.W<=1;}
VFXGraph VFXGraph::FromSource(const std::string& source){VFXGraph result;try{Compiler compiler;compiler.Compile(source);compiler.Validate();result.m_program=std::make_shared<Runtime::VFXProgram>(std::move(compiler.Program));}catch(const std::exception& e){result.m_error=e.what();}return result;}
VFXGraph VFXGraph::Load(UUID asset){VFXGraph graph;try{graph=FromSource(Source(asset));graph.m_asset=asset;}catch(const std::exception& e){graph.m_error=e.what();}graph.m_asset=asset;return graph;}
bool VFXGraph::IsValid()const{return bool(m_program);}UUID VFXGraph::GetAssetUUID()const{return m_asset;}std::string VFXGraph::GetError()const{return m_error;}std::vector<UUID> VFXGraph::GetDependencies()const{return m_program?m_program->Dependencies:std::vector<UUID>{};}
}
