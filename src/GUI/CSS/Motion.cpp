#include "GUI/CSS/Internal.h"
#include <cmath>
#include <sstream>
#include <locale>
#include <limits>
namespace Bazzalt::Runtime::CSS {
namespace {
using namespace Detail;
std::vector<Value> Split(const Value& value,bool commas){
    std::vector<Value> result;Value part;int depth=0;
    for(auto t:value.Tokens){if(t.Kind==TokenKind::End)continue;
        if(t.Text=="("||t.Text=="[")++depth;
        if(t.Text==")"||t.Text=="]")--depth;
        if(depth==0&&((commas&&t.Text==",")||(!commas&&t.Kind==TokenKind::Space))){Trim(part.Tokens);if(!part.Tokens.empty())result.push_back(std::move(part));part={};}else part.Tokens.push_back(t);
    }Trim(part.Tokens);if(!part.Tokens.empty())result.push_back(std::move(part));return result;
}
std::string Text(const Value& v){auto t=v.Tokens;Trim(t);return Value{t}.ToString();}
std::string Number(double n){std::ostringstream out;out.imbue(std::locale::classic());out.precision(12);out<<n;return out.str();}
std::optional<double> Seconds(const Value& v){auto t=v.Tokens;Trim(t);if(t.size()!=1||t[0].Kind!=TokenKind::Dimension||(t[0].Unit!="s"&&t[0].Unit!="ms"))return {};double n=t[0].Number*(t[0].Unit=="ms"?.001:1);return std::isfinite(n)&&std::abs(n)<=86400?std::optional<double>{n}:std::nullopt;}
Value Get(const ComputedStyle& s,const std::string& name,const std::string& fallback){auto i=s.Properties.find(name);return i==s.Properties.end()?ParseValue(fallback).Data:i->second;}
Value List(const ComputedStyle& s,const std::string& name,const std::string& fallback,std::size_t index){auto values=Split(Get(s,name,fallback),true);return values.empty()?ParseValue(fallback).Data:values[index%values.size()];}
bool Timing(const std::string& text){return text=="linear"||text=="ease"||text=="ease-in"||text=="ease-out"||text=="ease-in-out"||text=="step-start"||text=="step-end"||text.starts_with("cubic-bezier(")||text.starts_with("steps(");}
double Ease(const std::string& text,double p){
    p=std::clamp(p,0.,1.);if(text=="linear")return p;if(text=="step-start")return 1.;if(text=="step-end")return p>=1?1:0;
    auto value=ParseValue(text).Data;std::vector<double> n;for(auto& t:value.Tokens)if(t.Kind==TokenKind::Number)n.push_back(t.Number);
    if(text.starts_with("steps(")&&!n.empty()&&n[0]>=1&&n[0]<=100000){bool start=text.find("start")!=std::string::npos;return std::clamp((std::floor(p*n[0])+(start?1:0))/n[0],0.,1.);}
    double x1=.25,y1=.1,x2=.25,y2=1;
    if(text=="ease-in"){x1=.42;y1=0;x2=1;}else if(text=="ease-out"){x1=0;y1=0;x2=.58;}else if(text=="ease-in-out"){x1=.42;y1=0;x2=.58;}
    else if(text.starts_with("cubic-bezier(")&&n.size()==4&&n[0]>=0&&n[0]<=1&&n[2]>=0&&n[2]<=1){x1=n[0];y1=n[1];x2=n[2];y2=n[3];}
    auto curve=[](double t,double a,double b){return 3*(1-t)*(1-t)*t*a+3*(1-t)*t*t*b+t*t*t;};double lo=0,hi=1;for(int i=0;i<28;++i){double m=(lo+hi)*.5;if(curve(m,x1,x2)<p)lo=m;else hi=m;}return curve((lo+hi)*.5,y1,y2);
}
bool MotionProperty(const std::string& name){return name=="transition"||name.starts_with("transition-")||name=="animation"||name.starts_with("animation-");}
struct Spec {std::string Name="none",Easing="ease",Direction="normal",Fill="none",Play="running";double Duration=0,Delay=0,Count=1;};
std::vector<Spec> Specs(const ComputedStyle& style,bool animation){
    const std::string prefix=animation?"animation":"transition";std::vector<Spec> out;
    auto shorthand=style.Properties.find(prefix);
    if(shorthand!=style.Properties.end())for(auto& group:Split(shorthand->second,true)){
        Spec spec;spec.Name=animation?"none":"all";int times=0;
        for(auto& value:Split(group,false)){auto text=Text(value);if(auto seconds=Seconds(value)){if(times++==0)spec.Duration=std::max(0.,*seconds);else spec.Delay=*seconds;}
            else if(Timing(text))spec.Easing=text;
            else if(animation&&(text=="normal"||text=="reverse"||text=="alternate"||text=="alternate-reverse"))spec.Direction=text;
            else if(animation&&(text=="forwards"||text=="backwards"||text=="both"))spec.Fill=text;
            else if(animation&&(text=="paused"||text=="running"))spec.Play=text;
            else if(animation&&text=="infinite")spec.Count=std::numeric_limits<double>::infinity();
            else if(animation&&value.Tokens.size()==1&&value.Tokens[0].Kind==TokenKind::Number)spec.Count=std::clamp(value.Tokens[0].Number,0.,1000000.);
            else spec.Name=text;
        }out.push_back(spec);
    }
    auto names=style.Properties.find(prefix+(animation?"-name":"-property"));if(names!=style.Properties.end()){
        auto values=Split(names->second,true);out.resize(values.size());for(std::size_t i=0;i<values.size();++i)out[i].Name=Text(values[i]);
    }
    for(std::size_t i=0;i<out.size();++i){auto& s=out[i];
        auto time=[&](const std::string& suffix,double& field){if(style.Properties.contains(prefix+suffix))if(auto n=Seconds(List(style,prefix+suffix,"0s",i)))field=*n;};
        auto text=[&](const std::string& suffix,std::string& field){if(style.Properties.contains(prefix+suffix))field=Text(List(style,prefix+suffix,"none",i));};
        time("-duration",s.Duration);s.Duration=std::max(0.,s.Duration);time("-delay",s.Delay);text("-timing-function",s.Easing);
        if(animation){text("-direction",s.Direction);text("-fill-mode",s.Fill);text("-play-state",s.Play);
            if(style.Properties.contains("animation-iteration-count")){auto v=List(style,"animation-iteration-count","1",i);if(Text(v)=="infinite")s.Count=std::numeric_limits<double>::infinity();else if(auto n=ResolveLength(v))s.Count=std::clamp(*n,0.,1000000.);}}
    }return out;
}
}
Value ExpandMotion(const std::string& shorthand,const std::string& longhand,const Value& value){
    ComputedStyle style;style.Properties[shorthand]=value;auto specs=Specs(style,shorthand=="animation");std::string result;
    for(const auto& spec:specs){if(!result.empty())result+=",";
        if(longhand.ends_with("-name")||longhand.ends_with("-property"))result+=spec.Name;
        else if(longhand.ends_with("-duration"))result+=Number(spec.Duration)+"s";
        else if(longhand.ends_with("-delay"))result+=Number(spec.Delay)+"s";
        else if(longhand.ends_with("-timing-function"))result+=spec.Easing;
        else if(longhand.ends_with("-iteration-count"))result+=std::isfinite(spec.Count)?Number(spec.Count):"infinite";
        else if(longhand.ends_with("-direction"))result+=spec.Direction;
        else if(longhand.ends_with("-fill-mode"))result+=spec.Fill;
        else if(longhand.ends_with("-play-state"))result+=spec.Play;
    }return ParseValue(result).Data;
}
void ParseKeyframes(Stylesheet& sheet,const Limits& limits){
    std::string requirements;for(const auto& at:sheet.AtRules)if(at.Name=="require")requirements+="@require "+at.Prelude.ToString()+";";
    for(const auto& at:sheet.AtRules)if(at.Name=="keyframes"){
        auto name=at.Prelude.Tokens;Trim(name);if(name.size()!=1||name[0].Kind!=TokenKind::Ident||name[0].Text=="none"){sheet.Diagnostics.push_back({Severity::Error,at.Source,"Invalid keyframes name"});continue;}
        const auto& tokens=at.Body.Tokens;std::map<double,std::vector<Declaration>> frames;std::size_t index=0;
        auto fail=[&](){sheet.Diagnostics.push_back({Severity::Error,at.Source,"Invalid or excessive keyframe block"});};
        while(index<tokens.size()){
            if(tokens[index].Kind==TokenKind::Space){++index;continue;}
            std::vector<Token> offsets;while(index<tokens.size()&&tokens[index].Text!="{")offsets.push_back(tokens[index++]);if(index==tokens.size()){fail();break;}++index;
            std::vector<Token> body;while(index<tokens.size()&&tokens[index].Text!="}"){if(tokens[index].Text=="{"){fail();return;}body.push_back(tokens[index++]);}
            if(index==tokens.size()){fail();break;}++index;
            auto parsed=Parse(requirements+"X{"+Value{body}.ToString()+"}",limits);if(parsed.HasErrors()||parsed.Rules.size()!=1){fail();break;}
            for(const auto& offset:Split(Value{offsets},true)){auto ts=offset.Tokens;Trim(ts);double n=-1;
                if(ts.size()==1){if(ts[0].Text=="from")n=0;else if(ts[0].Text=="to")n=1;else if(ts[0].Kind==TokenKind::Percentage)n=ts[0].Number/100.;}
                if(n<0||n>1||!std::isfinite(n)||frames.size()>=1024){fail();break;}
                auto& declarations=frames[n];for(auto d:parsed.Rules[0].Declarations)if(!d.Important&&!MotionProperty(d.Property))declarations.push_back(std::move(d));
            }
        }
        auto& result=sheet.Keyframes[name[0].Text];result.clear();for(auto& [offset,declarations]:frames)result.push_back({offset,std::move(declarations)});
    }
}
Value Interpolate(const Value& a,const Value& b,double p){
    p=std::clamp(p,-100.,100.);auto ca=ResolveColor(a),cb=ResolveColor(b);
    if(ca&&cb){auto mix=[&](double x,double y){return x+(y-x)*p;};return ParseValue("rgba("+Number(mix(ca->R,cb->R)*255)+","+Number(mix(ca->G,cb->G)*255)+","+Number(mix(ca->B,cb->B)*255)+","+Number(mix(ca->A,cb->A))+")").Data;}
    auto x=a.Tokens,y=b.Tokens;Trim(x);Trim(y);std::erase_if(x,[](const auto& t){return t.Kind==TokenKind::Space;});std::erase_if(y,[](const auto& t){return t.Kind==TokenKind::Space;});
    if(x.size()!=y.size())return p<.5?a:b;
    for(std::size_t i=0;i<x.size();++i){auto& u=x[i];auto& v=y[i];const bool numeric=u.Kind==TokenKind::Number||u.Kind==TokenKind::Dimension||u.Kind==TokenKind::Percentage;
        if(numeric&&u.Kind==v.Kind&&u.Unit==v.Unit){u.Number+=(v.Number-u.Number)*p;u.Text=Number(u.Number);}
        else if(u.Kind!=v.Kind||u.Text!=v.Text||u.Unit!=v.Unit)return p<.5?a:b;
    }return Value{x};
}
void Animate(ComputedStyle& style,const Stylesheet& sheet,MotionState& state,double now,const Environment& env){
    if(!std::isfinite(now))return;
    auto targets=style.Properties;auto transitions=Specs(style,false);
    if(state.Initialized&&!env.ReducedMotion)for(const auto& [name,target]:targets){if(MotionProperty(name))continue;auto old=state.Targets.find(name);if(old==state.Targets.end()||old->second.ToString()==target.ToString())continue;
        for(auto it=transitions.rbegin();it!=transitions.rend();++it)if(it->Name==name||it->Name=="all"){
            if(it->Duration>0&&(Interpolate(old->second,target,.25).ToString()!=old->second.ToString()||Interpolate(old->second,target,.75).ToString()!=target.ToString())){auto display=state.Display.find(name);state.Transitions[name]={display==state.Display.end()?old->second:display->second,target,now,it->Duration,it->Delay,it->Easing};}else state.Transitions.erase(name);break;}}
    auto animations=Specs(style,true);if(animations.size()>64)animations.resize(64);state.Animations.resize(animations.size());
    for(std::size_t i=0;i<animations.size()&&!env.ReducedMotion;++i){const auto& spec=animations[i];auto found=sheet.Keyframes.find(spec.Name);if(found==sheet.Keyframes.end()||found->second.empty())continue;
        std::string signature=spec.Name;for(const auto& frame:found->second){signature+=Number(frame.Offset);for(auto& d:frame.Declarations)signature+=d.Property+":"+d.Data.ToString()+";";}
        auto& clock=state.Animations[i];if(clock.Signature!=signature||now<clock.Started)clock={signature,now,now,0,false};
        if(spec.Play=="paused"&&!clock.Paused){clock.Paused=true;clock.PausedAt=now;}else if(spec.Play!="paused"&&clock.Paused){clock.PausedDuration+=now-clock.PausedAt;clock.Paused=false;}
        double elapsed=(clock.Paused?clock.PausedAt:now)-clock.Started-clock.PausedDuration-spec.Delay;
        const double total=spec.Duration==0?0:spec.Duration*spec.Count;bool before=elapsed<0,after=elapsed>=total;
        if((before&&spec.Fill!="backwards"&&spec.Fill!="both")||(after&&spec.Fill!="forwards"&&spec.Fill!="both"))continue;
        double iteration=0,p=0;if(after){iteration=std::isfinite(spec.Count)?std::max(0.,std::ceil(spec.Count)-1):0;p=std::isfinite(spec.Count)?spec.Count-iteration:1;}else if(!before&&spec.Duration>0){iteration=std::floor(elapsed/spec.Duration);p=std::fmod(elapsed,spec.Duration)/spec.Duration;}
        bool reverse=spec.Direction=="reverse"||spec.Direction=="alternate-reverse";if((spec.Direction=="alternate"||spec.Direction=="alternate-reverse")&&std::fmod(iteration,2.)>=1)reverse=!reverse;if(reverse)p=1-p;
        std::map<std::string,std::map<double,Value>> properties;
        for(const auto& frame:found->second)for(const auto& declaration:frame.Declarations){auto expanded=SubstituteVariables(declaration.Data,style.CustomProperties,style.Diagnostics);if(expanded)properties[declaration.Property][frame.Offset]=*expanded;}
        for(auto& [name,frames]:properties){if(auto base=targets.find(name);base!=targets.end()){frames.try_emplace(0,base->second);frames.try_emplace(1,base->second);}if(frames.empty())continue;
            auto hi=frames.lower_bound(p);Value value;if(hi==frames.begin())value=hi->second;else if(hi==frames.end())value=frames.rbegin()->second;else{auto lo=std::prev(hi);value=Interpolate(lo->second,hi->second,Ease(spec.Easing,(p-lo->first)/(hi->first-lo->first)));}
            // Important declarations win over animations (cascade retains them separately below).
            if(!style.ImportantProperties.contains(name))style.Properties[name]=std::move(value);
        }
    }
    for(auto it=state.Transitions.begin();it!=state.Transitions.end();){auto& [name,track]=*it;auto target=targets.find(name);bool enabled=std::any_of(transitions.begin(),transitions.end(),[&](const auto& s){return s.Name==name||s.Name=="all";});
        if(env.ReducedMotion||!enabled||target==targets.end()||now<track.Start||now>=track.Start+track.Delay+track.Duration){it=state.Transitions.erase(it);continue;}
        double p=std::clamp((now-track.Start-track.Delay)/track.Duration,0.,1.);style.Properties[name]=Interpolate(track.From,track.To,Ease(track.Easing,p));++it;
    }
    state.Targets=std::move(targets);state.Display=style.Properties;state.Initialized=true;
}
}
