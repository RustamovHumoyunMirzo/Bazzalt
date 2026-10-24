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
                const bool width=name=="width"||name=="min-width"||name=="max-width",height=name=="height"||name=="min-height"||name=="max-height";
                if(!width&&!height)return false;
                auto value=ResolveLength({tokens},Env);if(!value||*value<0)return false;const auto actual=width?Env.Width:Env.Height;
                return name.starts_with("min-")?actual>=*value:name.starts_with("max-")?actual<=*value:std::fabs(actual-*value)<1e-6;
            }
            At=begin;bool value=Or();if(!Take(")")){Valid=false;return false;}return value;
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
}
bool EvaluateCondition(const Condition& condition,const Environment& environment){
    if(condition.Directive!="media"&&condition.Directive!="viewport")return false;
    if(!std::isfinite(environment.Width)||!std::isfinite(environment.Height)||environment.Width<0||environment.Height<0)return false;
    return Conditions(condition.Expression,environment).Run();
}
ComputedStyle Compute(const Stylesheet& sheet,const Node& node,const Environment& environment,const Limits& limits){
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
                auto found=winners.find(declaration.Property);
                if(found==winners.end()||found->second.first<=priority)winners[declaration.Property]={priority,declaration.Data};
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
                if(auto expanded=SubstituteVariables(winner.second,result.CustomProperties,result.Diagnostics,limits))result.Properties[name]=std::move(*expanded);
            }
        }
    }
    return result;
}
}
