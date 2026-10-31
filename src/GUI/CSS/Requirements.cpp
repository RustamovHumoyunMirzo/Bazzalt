#include "GUI/CSS/Internal.h"
#include <cmath>
#include <functional>

namespace Bazzalt::Runtime::CSS {
namespace {
using namespace Detail;
std::vector<Value> Arguments(Value value){
    Trim(value.Tokens);std::vector<Value> result(1);int depth=0;
    for(const auto& t:value.Tokens){if(Symbol(t,"("))++depth;if(Symbol(t,")"))--depth;if(!depth&&Symbol(t,","))result.emplace_back();else result.back().Tokens.push_back(t);}
    for(auto& part:result)Trim(part.Tokens);return result;
}
std::optional<Value> Expand(const Value& value,const std::map<std::string,Value>& values,std::vector<Diagnostic>& errors,const Limits& limits){
    Value output;std::size_t bytes=0;
    for(std::size_t i=0;i<value.Tokens.size();++i){const auto& t=value.Tokens[i];
        if(t.Kind==TokenKind::Ident&&Lower(t.Text)=="require"&&i+1<value.Tokens.size()&&Symbol(value.Tokens[i+1],"(")){
            std::size_t end=i+2;while(end<value.Tokens.size()&&!Symbol(value.Tokens[end],")"))++end;
            Value key{{value.Tokens.begin()+i+2,value.Tokens.begin()+end}};Trim(key.Tokens);
            // Both require(name) and the explicit marker spelling require(-> name <-) are accepted.
            std::string text;for(const auto& token:key.Tokens)if(token.Kind!=TokenKind::Space)text+=token.Text;
            if(text.starts_with("->")&&text.ends_with("<-"))text=text.substr(2,text.size()-4);
            auto found=values.find(text);if(end==value.Tokens.size()||found==values.end()){errors.push_back({Severity::Error,t.Source,"Unknown or malformed stylesheet requirement: "+text});return {};}
            for(const auto& replacement:found->second.Tokens){bytes+=replacement.Text.size()+replacement.Unit.size()+1;if(bytes>limits.ExpansionBytes){errors.push_back({Severity::Error,t.Source,"Stylesheet requirement expansion limit exceeded"});return {};}output.Tokens.push_back(replacement);}i=end;
        }else{bytes+=t.Text.size()+t.Unit.size()+1;output.Tokens.push_back(t);}
        if(bytes>limits.ExpansionBytes||output.Tokens.size()>limits.Tokens){errors.push_back({Severity::Error,t.Source,"Stylesheet requirement expansion limit exceeded"});return {};}
    }return output;
}
}
bool ValidateRequirement(const Requirement& r,const Value& value){
    auto tokens=value.Tokens;Detail::Trim(tokens);if(tokens.empty()||tokens.size()>256)return false;
    for(const auto& t:tokens)if(t.Kind==TokenKind::AtKeyword||(t.Kind==TokenKind::Symbol&&(t.Text==";"||t.Text=="{"||t.Text=="}")))return false;
    if(r.Type=="vec2"||r.Type=="vec3"||r.Type=="vec4"){
        std::size_t count=0;for(const auto& token:tokens)if(token.Kind!=TokenKind::Space){Requirement number=r;number.Type="length";if(!ValidateRequirement(number,{{token}}))return false;++count;}return count==std::size_t(r.Type.back()-'0');
    }
    if(r.Type=="float"||r.Type=="int"||r.Type=="length"){
        if(tokens.size()!=1)return false;const auto& t=tokens[0];if(r.Type=="length"){if(t.Kind!=TokenKind::Number&&t.Kind!=TokenKind::Dimension&&t.Kind!=TokenKind::Percentage)return false;Environment e;e.PercentageBase=100;if(!ResolveLength({tokens},e))return false;}else if(t.Kind!=TokenKind::Number)return false;
        const auto number=t.Number;return std::isfinite(number)&&(r.Type!="int"||(std::trunc(number)==number&&number>=-2147483648.&&number<=2147483647.))&&(!r.Minimum||number>=*r.Minimum)&&(!r.Maximum||number<=*r.Maximum);
    }
    if(r.Type=="color")return ResolveColor(value).has_value();
    if(r.Type=="bool")return ResolveBoolean(value).has_value();
    if(r.Type=="string")return tokens.size()==1&&tokens[0].Kind==TokenKind::String;
    if(r.Type=="enum"){auto text=ResolveString(value);return text&&std::find(r.Options.begin(),r.Options.end(),*text)!=r.Options.end();}
    if(r.Type=="texture"||r.Type=="font"){
        if(ResolveString(value)==std::optional<std::string>{"none"})return true;
        auto reference=ResolveReference(value);if(!reference)return false;if(*reference=="none")return true;auto text=*reference;if(text.starts_with("asset://"))text.erase(0,8);
        if(text.size()!=36)return false;for(std::size_t i=0;i<text.size();++i)if(i==8||i==13||i==18||i==23){if(text[i]!='-')return false;}else if(!std::isxdigit(static_cast<unsigned char>(text[i])))return false;return true;
    }return false;
}
void ParseRequirements(Stylesheet& sheet,const Limits& limits){
    std::set<std::string> names;
    for(const auto& rule:sheet.AtRules)if(rule.Name=="require"){
        const auto fail=[&](std::string text){sheet.Diagnostics.push_back({Severity::Error,rule.Source,std::move(text)});};
        if(sheet.Requirements.size()>=128){fail("Stylesheet requirement limit exceeded");break;}
        auto tokens=rule.Prelude.Tokens;Trim(tokens);if(rule.HasBlock||tokens.size()<2||!Symbol(tokens.front(),"(")||!Symbol(tokens.back(),")")){fail("Expected @require(name, default, type, ...);");continue;}
        auto args=Arguments({{tokens.begin()+1,tokens.end()-1}});if(args.size()<3){fail("@require needs a name, default and type");continue;}
        auto name=ResolveString(args[0]),type=ResolveString(args[2]);if(!name||args[0].Tokens.size()!=1||args[0].Tokens[0].Kind!=TokenKind::Ident||name->starts_with("--")||name->size()>128||!type||!names.insert(*name).second){fail("Invalid or duplicate stylesheet requirement name");continue;}
        Requirement r;r.Name=*name;r.Type=Lower(*type);r.Label=*name;r.Default=args[1];r.Source=rule.Source;
        if(r.Type=="number")r.Type="float";if(r.Type=="integer")r.Type="int";if(r.Type=="boolean")r.Type="bool";if(r.Type=="image")r.Type="texture";
        bool valid=true;
        for(std::size_t i=3;i<args.size();++i){auto t=args[i].Tokens;if(t.size()<3||t.front().Kind!=TokenKind::Ident||!Symbol(t[1],"(")||!Symbol(t.back(),")")){valid=false;break;}auto key=Lower(t.front().Text);Value body{{t.begin()+2,t.end()-1}};
            if(key=="min"||key=="max"){auto n=ResolveLength(body);if(!n){valid=false;break;}if(key=="min")r.Minimum=n;else r.Maximum=n;}
            else if(key=="label"||key=="tooltip"){auto text=ResolveString(body);if(!text||text->size()>1024){valid=false;break;}if(key=="label")r.Label=*text;else r.Tooltip=*text;}
            else if(key=="options"){for(const auto& option:Arguments(body)){auto text=ResolveString(option);if(!text||text->size()>256||r.Options.size()>=128){valid=false;break;}if(std::find(r.Options.begin(),r.Options.end(),*text)==r.Options.end())r.Options.push_back(*text);}}
            else valid=false;
        }
        if((r.Minimum||r.Maximum)&&r.Type!="float"&&r.Type!="int"&&r.Type!="length"&&r.Type!="vec2"&&r.Type!="vec3"&&r.Type!="vec4")valid=false;
        if(!valid||r.Default.ToString().size()>8192||(r.Minimum&&r.Maximum&&*r.Minimum>*r.Maximum)||!ValidateRequirement(r,r.Default)){fail("Invalid @require type, default or options for "+r.Name);continue;}sheet.Requirements.push_back(std::move(r));
    }
    if(!sheet.HasErrors()){std::map<std::string,Value> defaults;for(const auto& r:sheet.Requirements)defaults[r.Name]=r.Default;for(const auto& rule:sheet.Rules){for(const auto& declaration:rule.Declarations)(void)Expand(declaration.Data,defaults,sheet.Diagnostics,limits);for(const auto& condition:rule.Conditions)(void)Expand(condition.Expression,defaults,sheet.Diagnostics,limits);}}
}
Stylesheet BindRequirements(const Stylesheet& sheet,const std::map<std::string,std::string>& overrides,const Limits& limits){
    Stylesheet result=sheet;std::map<std::string,Value> values;
    for(const auto& r:sheet.Requirements){values[r.Name]=r.Default;auto found=overrides.find(r.Name);if(found==overrides.end())continue;auto parsed=ParseValue(found->second,limits);if(parsed.HasErrors()||!ValidateRequirement(r,parsed.Data)){result.Diagnostics.push_back({Severity::Warning,r.Source,"Invalid stylesheet override; using default: "+r.Name});continue;}values[r.Name]=std::move(parsed.Data);}
    for(auto& rule:result.Rules){for(auto& d:rule.Declarations){auto value=Expand(d.Data,values,result.Diagnostics,limits);d.Data=value.value_or(Value{});}for(auto& c:rule.Conditions){auto value=Expand(c.Expression,values,result.Diagnostics,limits);c.Expression=value.value_or(Value{});}}
    for(auto& [name,frames]:result.Keyframes)for(auto& frame:frames)for(auto& d:frame.Declarations){auto value=Expand(d.Data,values,result.Diagnostics,limits);d.Data=value.value_or(Value{});}
    result.Requirements.clear();return result;
}
}
