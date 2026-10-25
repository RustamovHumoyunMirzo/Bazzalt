#include "GUI/CSS/Internal.h"
#include <cmath>
#include <functional>
#include <tuple>

namespace Bazzalt::Runtime::CSS {
namespace {
using namespace Detail;
// Conditional dialect: screen/all, and/or/not, parenthesized min/max/exact size,
// orientation, color scheme and reduced motion. Unknown features fail closed.
class Conditions {
    std::vector<Token> Tokens;std::size_t At=0,Depth=0;const Environment& Env;bool Valid=true;
    const Token& Peek(){while(Tokens[At].Kind==TokenKind::Space)++At;return Tokens[At];}
    bool Word(std::string_view word){if(Peek().Kind==TokenKind::Ident&&Lower(Peek().Text)==word){++At;return true;}return false;}
    bool Take(std::string_view symbol){if(Symbol(Peek(),symbol)){++At;return true;}return false;}
    bool Primary(){
        if(++Depth>64){Valid=false;--Depth;return false;}struct Guard{std::size_t& V;~Guard(){--V;}} guard{Depth};
        if(Word("not"))return !Primary();
        if(Take("(")){
            const auto begin=At;const auto name=Peek().Kind==TokenKind::Ident?Lower(Peek().Text):std::string{};
            if(!name.empty())++At;
            if(!name.empty()&&Take(":")){
                auto start=At;std::size_t nesting=0;
                while(Peek().Kind!=TokenKind::End){if(Symbol(Peek(),")")&&nesting==0)break;if(Symbol(Peek(),"("))++nesting;else if(Symbol(Peek(),")"))--nesting;++At;}
                auto tokens=Slice(Tokens,start,At);Trim(tokens);if(!Take(")")){Valid=false;return false;}
                if(name=="orientation"){auto value=ResolveString({tokens});return value&&Lower(*value)==(Env.Width>=Env.Height?"landscape":"portrait");}
                if(name=="prefers-color-scheme"){auto value=ResolveString({tokens});return value&&Lower(*value)==Lower(Env.ColorScheme);}
                if(name=="prefers-reduced-motion"){auto value=ResolveString({tokens});return value&&Lower(*value)==(Env.ReducedMotion?"reduce":"no-preference");}
                if(name=="aspect-ratio"||name=="min-aspect-ratio"||name=="max-aspect-ratio"){
                    auto slash=std::find_if(tokens.begin(),tokens.end(),[](const Token& t){return Symbol(t,"/");});
                    std::optional<double> value;if(slash==tokens.end())value=ResolveLength({tokens});else {auto a=ResolveLength({{tokens.begin(),slash}}),b=ResolveLength({{slash+1,tokens.end()}});if(a&&b&&*b>0)value=*a / *b;}
                    if(!value||*value<=0||Env.Height<=0)return false;const auto actual=Env.Width/Env.Height;
                    return name.starts_with("min-")?actual>=*value:name.starts_with("max-")?actual<=*value:std::fabs(actual-*value)<1e-6;
                }
                const bool width=name=="width"||name=="min-width"||name=="max-width",height=name=="height"||name=="min-height"||name=="max-height";
                if(!width&&!height)return false;
                auto value=ResolveLength({tokens},Env);if(!value||*value<0)return false;const auto actual=width?Env.Width:Env.Height;
                return name.starts_with("min-")?actual>=*value:name.starts_with("max-")?actual<=*value:std::fabs(actual-*value)<1e-6;
            }
            At=begin;
            // Media-query range syntax: (width >= 600px), (400px < width <= 1200px).
            auto end=begin;int nesting=0;bool range=false;for(;end<Tokens.size()&&Tokens[end].Kind!=TokenKind::End;++end){if(Symbol(Tokens[end],")")&&nesting==0)break;if(Symbol(Tokens[end],"("))++nesting;if(Symbol(Tokens[end],")"))--nesting;if(nesting==0&&(Symbol(Tokens[end],"<")||Symbol(Tokens[end],">")||Symbol(Tokens[end],"=")))range=true;}
            if(range){
                std::vector<std::optional<double>> values;std::vector<std::string> operators;auto start=begin;
                const auto operand=[&](std::size_t a,std::size_t b)->std::optional<double>{auto t=Slice(Tokens,a,b);Trim(t);if(t.size()==1&&t[0].Kind==TokenKind::Ident){auto key=Lower(t[0].Text);if(key=="width")return Env.Width;if(key=="height")return Env.Height;return {};}return ResolveLength({t},Env);};
                for(auto i=begin;i<end;++i)if(Symbol(Tokens[i],"<")||Symbol(Tokens[i],">")||Symbol(Tokens[i],"=")){values.push_back(operand(start,i));auto op=Tokens[i].Text;if(i+1<end&&Symbol(Tokens[i+1],"="))op+=Tokens[++i].Text;operators.push_back(op);start=i+1;}
                values.push_back(operand(start,end));At=end;if(!Take(")")||operators.empty()||operators.size()>2){Valid=false;return false;}
                for(std::size_t i=0;i<operators.size();++i){if(!values[i]||!values[i+1])return false;const auto a=*values[i],b=*values[i+1];const auto& op=operators[i];if(!(op=="<"?a<b:op==">"?a>b:op=="<="?a<=b:op==">="?a>=b:op=="="?std::fabs(a-b)<1e-6:false))return false;}return true;
            }
            bool value=Or();if(!Take(")")){Valid=false;return false;}return value;
        }
        if(Word("all")||Word("screen"))return true;
        if(Peek().Kind==TokenKind::Ident){++At;return false;}
        Valid=false;if(Peek().Kind!=TokenKind::End)++At;return false;
    }
    bool And(){bool value=Primary();while(Word("and")){bool right=Primary();value=value&&right;}return value;}
    bool Or(){bool value=And();while(Word("or")||Take(",")){bool right=And();value=value||right;}return value;}
public:
    Conditions(const Value& value,const Environment& environment):Tokens(value.Tokens),Env(environment){Tokens.push_back({TokenKind::End,"","",0,{}});}
    bool Run(){if(Peek().Kind==TokenKind::End)return true;const bool result=Or();return Valid&&Peek().Kind==TokenKind::End&&result;}
};
struct Priority {
    bool Important;long long Layer;Specificity Weight;std::size_t Order;
    auto operator<=>(const Priority&)const=default;
};
std::vector<Value> Parts(const Value& data,std::string_view separator=" "){
    std::vector<Value> parts(1);int depth=0;for(const auto& token:data.Tokens){if(Symbol(token,"("))++depth;if(Symbol(token,")"))--depth;if(depth==0&&((separator==" "&&token.Kind==TokenKind::Space)||Symbol(token,separator))){if(!parts.back().Tokens.empty())parts.emplace_back();}else parts.back().Tokens.push_back(token);}if(parts.back().Tokens.empty())parts.pop_back();return parts;
}
std::vector<std::string> Longhands(const std::string& name){
    if(name=="border-radius")return {"border-top-left-radius","border-top-right-radius","border-bottom-right-radius","border-bottom-left-radius"};
    if(name=="background")return {"background-image","background-color"};
    if(name=="border")return {"border-left-width","border-top-width","border-right-width","border-bottom-width","border-left-color","border-top-color","border-right-color","border-bottom-color","border-style"};
    if(name!="padding"&&name!="margin"&&name!="border-width"&&name!="border-color")return {};
    std::vector<std::string> result;for(const auto* side:{"top","right","bottom","left"})result.push_back(name.starts_with("border-")?"border-"+std::string(side)+name.substr(6):name+"-"+side);return result;
}
std::optional<Value> Expand(const std::string& source,const std::string& name,Value value){
    if(source==name)return value;
    auto tokens=value.Tokens;Trim(tokens);if(tokens.size()==1&&tokens[0].Kind==TokenKind::Ident){auto key=Lower(tokens[0].Text);if(key=="initial"||key=="inherit"||key=="unset")return value;}
    if(source=="background"){const bool color=ResolveColor(value).has_value();if(name=="background-color")return color?value:ParseValue("transparent").Data;return color?ParseValue("none").Data:value;}
    if(source=="border"){
        const bool width=name.ends_with("width"),color=name.ends_with("color");
        for(const auto& part:Parts(value)){if(color&&ResolveColor(part))return part;if(width){auto n=ResolveLength(part);if(n&&*n>=0)return part;}if(!width&&!color){auto key=Lower(part.ToString());if(key=="solid"||key=="none")return part;}}
        return ParseValue(width?"3px":color?"currentColor":"none").Data;
    }
    auto targets=Longhands(source);auto found=std::find(targets.begin(),targets.end(),name);if(found==targets.end())return value;const auto index=std::size_t(found-targets.begin());
    const auto select=[&](const Value& axis)->std::optional<Value>{auto parts=Parts(axis);if(parts.empty()||parts.size()>4)return {};const std::size_t indices[4]={0,parts.size()>1?1u:0u,parts.size()>2?2u:0u,parts.size()>3?3u:parts.size()>1?1u:0u};return parts[indices[index]];};
    auto axes=Parts(value,"/");if(axes.empty()||axes.size()>2||(source!="border-radius"&&axes.size()>1))return {};auto first=select(axes[0]);if(!first)return {};if(axes.size()==1)return first;auto second=select(axes[1]);if(!second)return {};first->Tokens.push_back({TokenKind::Space," ","",0,{}});first->Tokens.insert(first->Tokens.end(),second->Tokens.begin(),second->Tokens.end());return first;
}
}
bool EvaluateCondition(const Condition& condition,const Environment& environment){
    if(condition.Directive!="media"&&condition.Directive!="viewport")return false;
    if(!std::isfinite(environment.Width)||!std::isfinite(environment.Height)||environment.Width<0||environment.Height<0)return false;
    return Conditions(condition.Expression,environment).Run();
}
ComputedStyle Compute(const Stylesheet& sheet,const Node& node,const Environment& environment,const Limits& limits){
    if(!sheet.Requirements.empty()){auto bound=BindRequirements(sheet,{},limits);return Compute(bound,node,environment,limits);}
    ComputedStyle result;
    auto remaining=limits.ResolveWork;
    const auto exhausted=[](){ComputedStyle failed;failed.Diagnostics.push_back({Severity::Error,{},"CSS cascade work limit exceeded"});return failed;};
    if(sheet.HasErrors()){result.Diagnostics=sheet.Diagnostics;return result;}
    std::vector<const Node*> ancestors;
    for(auto* current=&node;current;current=current->Parent){
        if(ancestors.size()>=std::min<std::size_t>(limits.AncestorDepth,128)||std::find(ancestors.begin(),ancestors.end(),current)!=ancestors.end()){
            result.Diagnostics.push_back({Severity::Error,{},"Hierarchy cycle or ancestor depth limit exceeded"});return result;
        }
        ancestors.push_back(current);
    }
    for(auto it=ancestors.rbegin();it!=ancestors.rend();++it){
        const auto& current=**it;
        std::map<std::string,std::pair<Priority,Value>> winners;
        std::map<std::string,std::string> origins;
        for(const auto& rule:sheet.Rules){
            if(remaining==0)return exhausted();
            --remaining;
            bool active=true;for(const auto& c:rule.Conditions)if(!EvaluateCondition(c,environment)){active=false;break;}if(!active)continue;
            std::optional<Specificity> weight;
            for(const auto& selector:rule.Selectors){if(Detail::MatchSelector(selector,current,remaining,limits))weight=weight?std::max(*weight,selector.Weight):selector.Weight;if(!remaining)return exhausted();}
            if(!weight)continue;
            const auto layer=std::find(sheet.Layers.begin(),sheet.Layers.end(),rule.Layer);
            const auto rank=rule.Layer.empty()?static_cast<long long>(sheet.Layers.size()):static_cast<long long>(layer-sheet.Layers.begin());
            for(const auto& declaration:rule.Declarations){
                if(!remaining)return exhausted();
                --remaining;
                Priority priority{declaration.Important,declaration.Important?-rank:rank,*weight,declaration.Order};
                const auto offer=[&](const std::string& name){auto found=winners.find(name);if(found==winners.end()||found->second.first<=priority){winners[name]={priority,declaration.Data};origins[name]=declaration.Property;}};
                offer(declaration.Property);for(const auto& name:Longhands(declaration.Property))offer(name);
            }
        }
        // Freeze inherited variables at the parent's computed value, not the
        // child's redefinitions. Siblings never share a mutable variable map.
        auto variables=result.CustomProperties;
        for(const auto& [name,winner]:winners)if(name.starts_with("--")){
            auto tokens=winner.second.Tokens;Trim(tokens);
            const auto keyword=tokens.size()==1&&tokens[0].Kind==TokenKind::Ident?Lower(tokens[0].Text):std::string{};
            if(keyword=="inherit"||keyword=="unset")continue;
            if(keyword=="initial")variables.erase(name);
            else variables[name]=winner.second;
        }
        std::map<std::string,Value> custom;
        for(const auto& [name,value]:variables){
            if(!remaining)return exhausted();
            --remaining;
            if(auto expanded=SubstituteVariables(value,variables,result.Diagnostics,limits))custom[name]=std::move(*expanded);
        }
        result.CustomProperties=std::move(custom);
        if(&current==&node){
            for(const auto& [name,winner]:winners)if(!name.starts_with("--")){
                if(!remaining)return exhausted();
                --remaining;
                if(auto expanded=SubstituteVariables(winner.second,result.CustomProperties,result.Diagnostics,limits))if(auto value=Expand(origins.at(name),name,std::move(*expanded)))result.Properties[name]=std::move(*value);
            }
        }
    }
    return result;
}
}
