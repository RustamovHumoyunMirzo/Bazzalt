#include "GUI/CSS/Internal.h"
#include <functional>

namespace Bazzalt::Runtime::CSS {
namespace {
using namespace Detail;
bool Balanced(const std::vector<Token>& tokens,std::vector<Diagnostic>& diagnostics,const Limits& limits){
    std::vector<std::string> stack;
    for(const auto& t:tokens){
        if(t.Kind!=TokenKind::Symbol)continue;
        if(t.Text=="("||t.Text=="["||t.Text=="{"){if(stack.size()>=DepthLimit(limits)){diagnostics.push_back({Severity::Error,t.Source,"Value nesting limit exceeded"});return false;}stack.push_back(t.Text);}
        if(t.Text==")"||t.Text=="]"||t.Text=="}"){
            const auto expected=t.Text==")"?"(":t.Text=="]"?"[":"{";
            if(stack.empty()||stack.back()!=expected){diagnostics.push_back({Severity::Error,t.Source,"Mismatched value delimiter"});return false;}stack.pop_back();
        }
    }
    if(!stack.empty()){diagnostics.push_back({Severity::Error,tokens.empty()?Location{}:tokens.back().Source,"Unterminated value"});return false;}return true;
}
class Parser {
    std::vector<Token> Tokens;std::size_t At=0,DeclarationCount=0;const Limits& Bounds;Stylesheet Sheet;
    const Token& Peek()const{return Tokens[std::min(At,Tokens.size()-1)];}
    bool Is(std::string_view s)const{return Symbol(Peek(),s);}
    void Spaces(){while(Peek().Kind==TokenKind::Space)++At;}
    void Error(std::string text){Sheet.Diagnostics.push_back({Severity::Error,Peek().Source,std::move(text)});}
    // Consume a prelude/value without losing the next enclosing rule boundary.
    std::vector<Token> Until(std::string_view stop,bool stopBrace=true){
        auto begin=At;std::size_t depth=0;
        while(Peek().Kind!=TokenKind::End){
            if(depth==0&&((Peek().Kind==TokenKind::Symbol&&stop.find(Peek().Text)!=std::string_view::npos)||(stopBrace&&Is("}"))))break;
            if(Is("(")||Is("[")||Is("{")){
                if(++depth>DepthLimit(Bounds)){Error("Nesting limit exceeded");while(Peek().Kind!=TokenKind::End&&!Is("}"))++At;break;}
            }else if(Is(")")||Is("]")||Is("}")){
                if(depth)--depth;
                // Unmatched ) / ] belong to the malformed fragment. Consume
                // them so declaration recovery always makes progress. Only }
                // may remain for the owning block parser.
                else if(Is("}"))break;
            }
            ++At;
        }
        auto result=Slice(Tokens,begin,At);Trim(result);return result;
    }
    std::vector<Declaration> Declarations(){
        std::vector<Declaration> result;
        while(Peek().Kind!=TokenKind::End&&!Is("}")){
            Spaces();if(Is(";")){++At;continue;}if(Is("}"))break;
            if(DeclarationCount>=Bounds.Declarations){Error("Declaration count limit exceeded");Until("}");break;}
            const auto at=Peek().Source;
            if(Peek().Kind!=TokenKind::Ident){Error("Expected property name; nested style rules are not supported");Until(";");if(Is(";"))++At;continue;}
            auto name=Peek().Text;++At;Spaces();
            if(name=="--")Error("Custom property names require a name after --");
            if(!Is(":")){Error("Expected : after property");Until(";");if(Is(";"))++At;continue;}
            ++At;auto value=Until(";");bool important=false;
            if(!value.empty()&&value.back().Kind==TokenKind::Ident&&Lower(value.back().Text)=="important"){
                auto end=value.size()-1;while(end&&value[end-1].Kind==TokenKind::Space)--end;
                if(end&&Symbol(value[end-1],"!")){value.resize(end-1);Trim(value);important=true;}
            }
            ++DeclarationCount;
            if(value.empty()){
                if(name.starts_with("--"))result.push_back({std::move(name),{},important,at,DeclarationCount});
                else Sheet.Diagnostics.push_back({Severity::Error,at,"Empty declaration value"});
            }else if(Balanced(value,Sheet.Diagnostics,Bounds))result.push_back({name.starts_with("--")?name:Lower(name),{std::move(value)},important,at,DeclarationCount});
            if(Is(";"))++At;
        }
        if(Is("}"))++At;else Error("Unterminated declaration block");return result;
    }
    std::string LayerName(const std::vector<Token>& tokens){
        std::string name;bool ident=true;
        for(const auto& t:tokens){if(t.Kind==TokenKind::Space)continue;
            if(ident&&t.Kind==TokenKind::Ident){name+=t.Text;ident=false;}
            else if(!ident&&Symbol(t,".")){name+='.';ident=true;}
            else return {};
        }
        return ident?std::string{}:name;
    }
    void AddLayer(const std::string& name){if(std::find(Sheet.Layers.begin(),Sheet.Layers.end(),name)==Sheet.Layers.end())Sheet.Layers.push_back(name);}
    void Rules(std::vector<Condition> conditions,std::string layer,std::size_t depth,bool enclosed){
        if(depth>DepthLimit(Bounds)){Error("At-rule nesting limit exceeded");return;}
        while(Peek().Kind!=TokenKind::End){
            Spaces();if(Is("}")){if(enclosed){++At;return;}Error("Unexpected closing block");++At;continue;}
            if(Peek().Kind==TokenKind::End)break;
            if(Sheet.Rules.size()>=Bounds.Rules){Error("Rule count limit exceeded");At=Tokens.size()-1;return;}
            if(Peek().Kind==TokenKind::AtKeyword){
                const auto at=Peek().Source;auto name=Lower(Peek().Text);++At;auto prelude=Until("{;");
                Balanced(prelude,Sheet.Diagnostics,Bounds);
                if(name=="keyframes"&&!conditions.empty())Error("Keyframes must be declared outside conditional blocks");
                const bool block=Is("{");AtRule record{name,{prelude},{},block,at};
                if(Is(";")){
                    ++At;
                    if(name=="require"){if(enclosed||!conditions.empty())Error("@require must be declared at stylesheet root");}
                    else if(name=="layer"){
                        std::vector<Token> segment;
                        for(std::size_t i=0;i<=prelude.size();++i){if(i==prelude.size()||Symbol(prelude[i],",")){auto n=LayerName(segment);if(n.empty())Error("Invalid layer name");else AddLayer(layer.empty()?n:layer+"."+n);segment.clear();}else segment.push_back(prelude[i]);}
                    }else if(name!="keyframes")Sheet.Diagnostics.push_back({Severity::Warning,at,"At-rule retained but not executed: @"+name});
                }else if(block){
                    ++At;
                    if(name=="viewport"||name=="media"){
                        auto nested=conditions;nested.push_back({name,{prelude},at});Rules(std::move(nested),layer,depth+1,true);
                    }else if(name=="layer"){
                        auto child=LayerName(prelude);if(prelude.empty())child="__anonymous_"+std::to_string(Sheet.AtRules.size())+"_"+std::to_string(at.Offset);
                        if(child.empty()){Error("Invalid layer name");Until("}");if(Is("}"))++At;}
                        else {child=layer.empty()?child:layer+"."+child;AddLayer(child);Rules(conditions,std::move(child),depth+1,true);}
                    }else {
                        // Keep opaque rules such as @font-face/@keyframes/@supports;
                        // never accidentally apply their contents as normal style rules.
                        record.Body.Tokens=Until("}",false);if(Is("}"))++At;else Error("Unterminated at-rule");
                        Balanced(record.Body.Tokens,Sheet.Diagnostics,Bounds);
                        if(name!="keyframes")Sheet.Diagnostics.push_back({Severity::Warning,at,"At-rule retained but not executed: @"+name});
                    }
                }else Error("Expected at-rule block or semicolon");
                Sheet.AtRules.push_back(std::move(record));continue;
            }
            const auto at=Peek().Source;auto header=Until("{");
            if(!Is("{")){Error("Expected style rule block");if(Is("}"))continue;At=Tokens.size()-1;break;}
            ++At;auto selectors=SelectorTokens(std::move(header),Sheet.Diagnostics,Bounds);auto declarations=Declarations();
            if(!selectors.empty())Sheet.Rules.push_back({std::move(selectors),std::move(declarations),conditions,layer,at});
        }
        if(enclosed)Error("Unterminated at-rule block");
    }
public:
    Parser(std::string_view source,const Limits& bounds):Bounds(bounds){Tokens=Tokenize(source,Sheet.Diagnostics,bounds);}
    Stylesheet Run(){Rules({},"",0,false);ParseRequirements(Sheet,Bounds);ParseKeyframes(Sheet,Bounds);return std::move(Sheet);}
};
}
Stylesheet Parse(std::string_view source,const Limits& limits){return Parser(source,limits).Run();}
ParseResult ParseValue(std::string_view source,const Limits& limits){ParseResult result;result.Data.Tokens=Tokenize(source,result.Diagnostics,limits);Detail::Trim(result.Data.Tokens);Balanced(result.Data.Tokens,result.Diagnostics,limits);return result;}
}
