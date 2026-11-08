#include "AI/BehaviorTreeRuntime.h"
#include "Bazzalt/AssetManager.h"
#include "Bazzalt/Scene.h"
#include "Bazzalt/Physics.h"
#include <algorithm>
#include "Utils/NumberParsing.h"
#include <cctype>
#include <fstream>
#include <set>
#include <sstream>

namespace Bazzalt::AI {
namespace {
struct Node {
    std::string Tag;
    std::map<std::string,std::string> Attr;
    std::vector<Node> Children;
    std::size_t Cursor=0;
    double Time=0;
    unsigned Repeats=0;
    bool Active=false;
    std::string Get(const std::string& key,std::string fallback={}) const {auto it=Attr.find(key);return it==Attr.end()?fallback:it->second;}
    double Num(const std::string& key,double fallback=0) const {
        auto it=Attr.find(key);if(it==Attr.end())return fallback;
        double value=0;auto& text=it->second;
        if(!Detail::ParseNumber(text,value))throw std::invalid_argument("Invalid behavior number: "+key);
        return value;
    }
    Vec3 Vector() const {return {float(Num("x")),float(Num("y")),float(Num("z"))};}
};
// Restricted, bounded XML reader. No DTD, entity expansion, external resources or executable source.
class Parser {
    const std::string& S;std::size_t P=0,Count=0;
    [[noreturn]] void Error() const {throw std::invalid_argument("Invalid behavior XML at byte "+std::to_string(P));}
    bool At(const char* value) const {return S.compare(P,std::char_traits<char>::length(value),value)==0;}
    void Space(){while(P<S.size()&&std::isspace(static_cast<unsigned char>(S[P])))++P;}
    void Misc(){for(;;){Space();if(At("<!--")){auto end=S.find("-->",P+4);if(end==std::string::npos)Error();P=end+3;}else break;}}
    std::string Name(){auto start=P;while(P<S.size()&&(std::isalnum(static_cast<unsigned char>(S[P]))||S[P]=='_'||S[P]=='-'))++P;if(P==start)Error();return S.substr(start,P-start);}
    std::string Text(char quote){std::string out;while(P<S.size()&&S[P]!=quote){if(S[P]=='<')Error();if(S[P]!='&'){out+=S[P++];continue;}auto end=S.find(';',++P);if(end==std::string::npos)Error();auto entity=S.substr(P,end-P);P=end+1;
        if(entity=="amp")out+='&';else if(entity=="lt")out+='<';else if(entity=="gt")out+='>';else if(entity=="quot")out+='"';else if(entity=="apos")out+='\'';
        else if(entity=="#10")out+='\n';else if(entity=="#9")out+='\t';else if(entity=="#13")out+='\r';else Error();}if(P==S.size())Error();++P;return out;}
    Node Element(unsigned depth){
        if(depth>66||++Count>4100)Error();Misc();if(P==S.size()||S[P++]!='<'||At("!"))Error();Node n;n.Tag=Name();
        for(;;){Space();if(At("/>")){P+=2;return n;}if(At(">")){++P;break;}auto key=Name();Space();if(P==S.size()||S[P++]!='=')Error();Space();if(P==S.size()||(S[P]!='"'&&S[P]!='\''))Error();char quote=S[P++];if(!n.Attr.emplace(key,Text(quote)).second)Error();}
        for(;;){Misc();if(At("</")){P+=2;if(Name()!=n.Tag)Error();Space();if(P==S.size()||S[P++]!='>')Error();return n;}n.Children.push_back(Element(depth+1));}
    }
public:
    explicit Parser(const std::string& source):S(source){}
    Node Parse(){if(S.empty()||S.size()>1024*1024)Error();if(At("\xef\xbb\xbf"))P=3;Space();if(At("<?xml ")){auto end=S.find("?>",P);if(end==std::string::npos)Error();P=end+2;}auto root=Element(0);Misc();if(P!=S.size())Error();return root;}
};
const std::set<std::string> Composites={"Sequence","Selector","ReactiveSequence","ReactiveSelector","Parallel","Utility"};
const std::set<std::string> Decorators={"Invert","AlwaysSucceed","AlwaysFail","Repeat","Timeout","Cooldown","Option"};
const std::set<std::string> Leaves={"Succeed","Fail","Wait","Action","Condition","SetBool","SetNumber","AddNumber","SetString","SetVector","HasKey","ClearKey","IsTrue","Compare","SetVelocity","ApplyForce","ApplyImpulse","Raycast","OverlapSphere","MoveTo","WithinDistance","Stop","Wake","Sleep"};
void Validate(const Node& n){
    auto size=n.Children.size();if(Composites.contains(n.Tag)){if(size==0||size>128)throw std::invalid_argument("Composite requires 1..128 children");}
    else if(Decorators.contains(n.Tag)){if(size!=1)throw std::invalid_argument("Decorator requires one child");}
    else if(!Leaves.contains(n.Tag)||size)throw std::invalid_argument("Unknown behavior or invalid children: "+n.Tag);
    for(auto key:{"seconds","radius","distance","speed","count","weight","x","y","z"})if(n.Attr.contains(key)){
        auto value=n.Num(key);if(std::abs(value)>1e12)throw std::invalid_argument("Behavior number out of range");
        if((std::string(key)=="seconds"||std::string(key)=="radius"||std::string(key)=="distance"||std::string(key)=="speed"||std::string(key)=="count")&&value<0)throw std::invalid_argument("Negative behavior parameter");
        if(std::string(key)=="count"&&(value!=std::floor(value)||value>1000000))throw std::invalid_argument("Invalid repeat count");
    }
    if(n.Tag=="SetNumber"||n.Tag=="AddNumber"||n.Tag=="Compare")n.Num("value");
    if(n.Tag=="SetBool"&&n.Get("value","true")!="true"&&n.Get("value")!="false")throw std::invalid_argument("Invalid behavior boolean");
    if(n.Tag=="Compare"&&!std::set<std::string>{"==","!=",">",">=","<","<="}.contains(n.Get("op",">=")))throw std::invalid_argument("Invalid comparison operator");
    for(auto& child:n.Children){if(n.Tag=="Utility"&&child.Tag!="Option")throw std::invalid_argument("Utility requires Option children");Validate(child);}
}
Status Test(bool value){return value?Status::Success:Status::Failure;}
void ResetNode(Node& n,Context& c,bool preserveCooldown=false){
    auto cooldown=preserveCooldown&&n.Tag=="Cooldown"?n.Time:0;
    const bool active=n.Active;n.Active=false;
    std::exception_ptr failure;
    if(active&&n.Tag=="Action"&&c.Action)try{c.Action(n.Get("name","custom"),c,true);}catch(...){failure=std::current_exception();}
    if(active&&n.Tag=="MoveTo"&&c.Self)c.Self.GetScene()->GetPhysics().SetLinearVelocity(c.Self,{});
    for(auto& child:n.Children)try{ResetNode(child,c,preserveCooldown);}catch(...){if(!failure)failure=std::current_exception();}
    n.Active=false;n.Cursor=0;n.Time=cooldown;n.Repeats=0;
    if(failure)std::rethrow_exception(failure);
}
Status Run(Node& n,Context& c);
Status Evaluate(Node& n,Context& c){
    const auto& t=n.Tag;auto& b=c.Memory;auto key=n.Get("key",t=="MoveTo"||t=="WithinDistance"?"target":"value");
    if(t=="Succeed")return Status::Success;if(t=="Fail")return Status::Failure;
    if(t=="Sequence"||t=="Selector"||t=="ReactiveSequence"||t=="ReactiveSelector"){
        bool sequence=t=="Sequence"||t=="ReactiveSequence",reactive=t.starts_with("Reactive");auto previous=n.Cursor;if(reactive)n.Cursor=0;
        for(;n.Cursor<n.Children.size();++n.Cursor){auto status=Run(n.Children[n.Cursor],c);
            if(status==Status::Running||(sequence?status==Status::Failure:status==Status::Success)){
                if(reactive&&previous!=n.Cursor&&previous<n.Children.size())ResetNode(n.Children[previous],c);return status;
            }}return sequence?Status::Success:Status::Failure;
    }
    if(t=="Parallel"){
        bool running=false;for(auto& child:n.Children){if(child.Cursor==SIZE_MAX)continue;auto status=Run(child,c);if(status==Status::Failure)return status;if(status==Status::Running)running=true;else child.Cursor=SIZE_MAX;}return running?Status::Running:Status::Success;
    }
    if(t=="Utility"){
        std::size_t best=0;double score=-std::numeric_limits<double>::infinity();
        for(std::size_t i=0;i<n.Children.size();++i){auto& option=n.Children[i];auto value=b.Number(option.Get("key","score"))*option.Num("weight",1);if(value>score){score=value;best=i;}}
        if(n.Active&&n.Cursor!=best)ResetNode(n.Children[n.Cursor],c);n.Cursor=best;return Run(n.Children[best],c);
    }
    if(t=="Cooldown"){
        if(n.Time>0){return Status::Failure;}
        auto status=Run(n.Children[0],c);if(status!=Status::Running)n.Time=n.Num("seconds",1);return status;
    }
    if(t=="Timeout"){
        n.Time+=c.DeltaTime;if(n.Time>=n.Num("seconds",1)){ResetNode(n.Children[0],c);return Status::Failure;}return Run(n.Children[0],c);
    }
    if(t=="Repeat"){
        if(n.Repeats>=n.Num("count",3))return Status::Success;
        auto status=Run(n.Children[0],c);if(status==Status::Failure)return status;if(status==Status::Success)++n.Repeats;
        return n.Repeats>=n.Num("count",3)?Status::Success:Status::Running;
    }
    if(t=="Invert"||t=="AlwaysSucceed"||t=="AlwaysFail"||t=="Option"){
        auto status=Run(n.Children[0],c);if(status==Status::Running||t=="Option")return status;
        if(t=="AlwaysSucceed")return Status::Success;if(t=="AlwaysFail")return Status::Failure;return status==Status::Success?Status::Failure:Status::Success;
    }
    if(t=="Wait"){n.Time+=c.DeltaTime;return n.Time>=n.Num("seconds",1)?Status::Success:Status::Running;}
    if(t=="Action")return c.Action?c.Action(n.Get("name","custom"),c,false):Status::Failure;
    if(t=="Condition")return Test(c.Condition&&c.Condition(n.Get("name","custom"),c));
    if(t=="HasKey")return Test(b.Contains(n.Get("key","flag")));
    if(t=="ClearKey"){b.Erase(n.Get("key","flag"));return Status::Success;}
    if(t=="IsTrue"){auto p=b.Get<bool>(n.Get("key","flag"));return Test(p&&*p);}
    if(t=="SetBool")b.Set(n.Get("key","flag"),n.Get("value","true")=="true");
    else if(t=="SetNumber")b.Set(key,n.Num("value",1));
    else if(t=="AddNumber")b.Set(key,b.Number(key)+n.Num("value",1));
    else if(t=="SetString")b.Set(n.Get("key","text"),n.Get("value","hello"));
    else if(t=="SetVector")b.Set(n.Get("key","target"),n.Vector());
    else if(t=="Compare"){
        auto p=b.Get<double>(key);if(!p)return Status::Failure;auto value=n.Num("value"),a=*p;auto op=n.Get("op",">=");
        return Test(op=="=="?a==value:op=="!="?a!=value:op==">"?a>value:op==">="?a>=value:op=="<"?a<value:a<=value);
    }else{
        if(!c.Self)return Status::Failure;auto physics=c.Self.GetScene()->GetPhysics();
        if(t=="SetVelocity")return Test(physics.SetLinearVelocity(c.Self,n.Vector()));
        if(t=="ApplyForce")return Test(physics.ApplyForce(c.Self,n.Vector()));
        if(t=="ApplyImpulse")return Test(physics.ApplyImpulse(c.Self,n.Vector()));
        if(t=="Stop")return Test(physics.SetLinearVelocity(c.Self,{}));
        if(t=="Wake")return Test(physics.WakeUp(c.Self));if(t=="Sleep")return Test(physics.Sleep(c.Self));
        auto position=c.Self.GetWorldTransform().Position;
        PhysicsQueryOptions query;query.ExcludedEntities.push_back(c.Self.GetUUID());
        if(t=="Raycast"){
            auto hit=physics.Raycast(position,{float(n.Num("x")),float(n.Num("y",-1)),float(n.Num("z"))},float(n.Num("distance",10)),query);
            auto out=n.Get("key","hit");b.Set(out,bool(hit));if(hit)b.Set(out+".point",hit.Point);else b.Erase(out+".point");return Test(bool(hit));
        }
        if(t=="OverlapSphere"){auto hits=physics.OverlapSphere(position,float(n.Num("radius",5)),query);b.Set(n.Get("key","nearby"),double(hits.size()));return Test(!hits.empty());}
        if(t=="MoveTo"||t=="WithinDistance"){
            auto target=b.Get<Vec3>(key);if(!target)return Status::Failure;auto delta=*target-position;auto distance=delta.Length();
            if(t=="WithinDistance")return Test(distance<=n.Num("radius",2));
            if(distance<=n.Num("radius",.25)){physics.SetLinearVelocity(c.Self,{});return Status::Success;}
            auto velocity=delta*(float(n.Num("speed",3))/std::max(distance,.0001f));
            return physics.SetLinearVelocity(c.Self,velocity)?Status::Running:Status::Failure;
        }
        return Status::Failure;
    }return Status::Success;
}
Status Run(Node& n,Context& c){
    const bool wasActive=n.Active;
    auto status=Evaluate(n,c);
    // A failed movement must release velocity owned by the previous running tick.
    if(status==Status::Failure&&wasActive&&n.Tag=="MoveTo"&&c.Self)c.Self.GetScene()->GetPhysics().SetLinearVelocity(c.Self,{});
    n.Active=status==Status::Running;
    if(status!=Status::Running){ResetNode(n,c,true);}return status;
}
} // namespace
struct BehaviorTreeRuntime::Impl {Node Root;Blackboard Defaults;};
BehaviorTreeRuntime::BehaviorTreeRuntime()=default;
BehaviorTreeRuntime::~BehaviorTreeRuntime()=default;
BehaviorTreeRuntime::BehaviorTreeRuntime(BehaviorTreeRuntime&&) noexcept=default;
BehaviorTreeRuntime& BehaviorTreeRuntime::operator=(BehaviorTreeRuntime&&) noexcept=default;
BehaviorTreeRuntime BehaviorTreeRuntime::FromSource(const std::string& xml){
    auto document=Parser(xml).Parse();if(document.Tag!="BehaviorTree"||document.Get("version")!="1")throw std::invalid_argument("Expected BehaviorTree version 1");
    if(document.Get("locked","true")!="true"&&document.Get("locked")!="false")throw std::invalid_argument("Invalid locked flag");
    BehaviorTreeRuntime tree;tree.m_impl=std::make_unique<Impl>();bool found=false,blackboard=false;
    for(auto& section:document.Children){
        if(section.Tag=="Tree"){if(found||section.Children.size()!=1)throw std::invalid_argument("Expected one tree root");tree.m_impl->Root=std::move(section.Children[0]);found=true;Validate(tree.m_impl->Root);}
        else if(section.Tag=="Blackboard"){
            if(blackboard)throw std::invalid_argument("Duplicate Blackboard");blackboard=true;
            for(auto& value:section.Children){auto key=value.Get("key");if(key.empty()||tree.m_impl->Defaults.Contains(key)||!value.Children.empty())throw std::invalid_argument("Invalid blackboard entry");
                if(value.Tag=="Number")tree.m_impl->Defaults.Set(key,value.Num("value"));
                else if(value.Tag=="Bool"){auto text=value.Get("value","false");if(text!="true"&&text!="false")throw std::invalid_argument("Invalid blackboard boolean");tree.m_impl->Defaults.Set(key,text=="true");}
                else if(value.Tag=="String")tree.m_impl->Defaults.Set(key,value.Get("value"));
                else if(value.Tag=="Vector")tree.m_impl->Defaults.Set(key,value.Vector());else throw std::invalid_argument("Invalid blackboard type");
            }
        }else throw std::invalid_argument("Unknown behavior section");
    }if(!found)throw std::invalid_argument("Missing Tree");return tree;
}
BehaviorTreeRuntime BehaviorTreeRuntime::Load(UUID id){
    auto asset=AssetManager::GetAsset(id);if(!asset||asset->State!=AssetState::Ready)throw std::invalid_argument("Behavior tree asset is not ready");
    auto extension=asset->SourcePath.extension().string();std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return char(std::tolower(c));});if(extension!=".bbtree")throw std::invalid_argument("Expected a .bbtree asset");
    auto path=std::filesystem::exists(asset->CachePath)?asset->CachePath:asset->SourcePath;std::ifstream file(path,std::ios::binary);
    std::string source(1024*1024+1,'\0');file.read(source.data(),std::streamsize(source.size()));source.resize(std::size_t(file.gcount()));return FromSource(source);
}
namespace {void AdvanceCooldowns(Node& n,float dt){if(n.Tag=="Cooldown")n.Time=std::max(0.0,n.Time-dt);for(auto& child:n.Children)AdvanceCooldowns(child,dt);}}
Status BehaviorTreeRuntime::Tick(Context& context){
    if(!m_impl)return Status::Failure;if(!std::isfinite(context.DeltaTime)||context.DeltaTime<0)throw std::invalid_argument("Invalid behavior delta time");AdvanceCooldowns(m_impl->Root,context.DeltaTime);return Run(m_impl->Root,context);
}
void BehaviorTreeRuntime::Reset(Context& context){if(m_impl)ResetNode(m_impl->Root,context);}
void BehaviorTreeRuntime::Initialize(Blackboard& memory) const {if(m_impl)for(auto& [key,value]:m_impl->Defaults.Values)if(!memory.Contains(key))memory.Set(key,value);}
} // namespace Bazzalt::AI

namespace Bazzalt::AI {
std::vector<UUID> BehaviorTreeRuntime::References(const std::string& source){
    // Parse and validate with the runtime contract, including XML entity decoding.
    auto validated=FromSource(source);auto document=Parser(source).Parse();std::set<UUID> references;
    auto visit=[&](auto&& self,const Node& node)->void{
        if(node.Tag=="String"||node.Tag=="SetString"){UUID id;if(UUID::TryParse(node.Get("value"),id)&&id)references.insert(id);}
        for(const auto& child:node.Children)self(self,child);
    };
    visit(visit,document);return {references.begin(),references.end()};
}
}
