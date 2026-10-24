#include "GUI/CSS/Internal.h"
#include <charconv>
#include <functional>
#include <limits>

namespace Bazzalt::Runtime::CSS {
namespace {
using namespace Detail;
bool ValidNth(const std::string& expression);
class SelectorParser {
    std::vector<Token> Tokens;std::size_t At=0,Depth;std::vector<Diagnostic>& Diagnostics;const Limits& Bounds;bool Failed=false;
    const Token& Peek()const{return Tokens[std::min(At,Tokens.size()-1)];}
    bool Is(std::string_view s)const{return Symbol(Peek(),s);}
    bool Spaces(){auto before=At;while(Peek().Kind==TokenKind::Space)++At;return At!=before;}
    void Error(std::string message){Diagnostics.push_back({Severity::Error,Peek().Source,std::move(message)});Failed=true;}
    std::vector<Token> Argument(){
        const auto begin=++At;std::size_t level=1;
        while(Peek().Kind!=TokenKind::End){if(Is("("))++level;if(Is(")")&&!--level){auto result=Slice(Tokens,begin,At++);return result;}++At;}
        Error("Unterminated selector argument");return {};
    }
    bool CompoundSelector(Compound& part,Specificity& weight){
        bool initial=true;
        while(Peek().Kind!=TokenKind::End){
            Test test;const auto token=Peek();
            if(initial&&(token.Kind==TokenKind::Ident||Is("*"))){
                test.Type=Is("*")?Test::Kind::Universal:Test::Kind::Type;test.Name=token.Text;++At;if(test.Type==Test::Kind::Type)++weight.Types;
            }else if(token.Kind==TokenKind::Hash){if(!token.IdentifierHash){Error("Escape a leading digit in an ID selector");return false;}test.Type=Test::Kind::ID;test.Name=token.Text;++weight.IDs;++At;}
            else if(Is(".")){++At;if(Peek().Kind!=TokenKind::Ident){Error("Expected class name");return false;}test.Type=Test::Kind::Class;test.Name=Peek().Text;++weight.Classes;++At;}
            else if(Is("[")){
                ++At;Spaces();if(Peek().Kind!=TokenKind::Ident){Error("Expected attribute name");return false;}
                test.Type=Test::Kind::Attribute;test.Name=Peek().Text;++weight.Classes;++At;Spaces();
                if(!Is("]")){
                    if(Is("=")){test.Operator="=";++At;}
                    else if(Is("~")||Is("|")||Is("^")||Is("$")||Is("*")){test.Operator=Peek().Text;++At;if(!Is("=")){Error("Expected attribute comparison");return false;}test.Operator+='=';++At;}
                    else {Error("Unsupported attribute operator");return false;}
                    Spaces();if(Peek().Kind!=TokenKind::Ident&&Peek().Kind!=TokenKind::String){Error("Attribute value must be a string or identifier");return false;}
                    test.Argument=Peek().Text;++At;Spaces();
                    if(Peek().Kind==TokenKind::Ident){const auto flag=Lower(Peek().Text);if(flag!="i"&&flag!="s"){Error("Unknown attribute comparison flag");return false;}test.CaseInsensitive=flag=="i";++At;Spaces();}
                }
                if(!Is("]")){Error("Expected ]");return false;}++At;
            }else if(Is(":")){
                ++At;if(Is(":")){Error("Pseudo-elements are not GUI entities");return false;}
                if(Peek().Kind!=TokenKind::Ident){Error("Expected pseudo-class");return false;}
                test.Type=Test::Kind::Pseudo;test.Name=Lower(Peek().Text);++At;
                if(Is("(")){
                    auto args=Argument();if(Failed)return false;
                    if(test.Name=="is"||test.Name=="not"||test.Name=="where"){
                        if(Depth>=DepthLimit(Bounds)){Error("Selector nesting limit exceeded");return false;}
                        const auto errors=Diagnostics.size();test.Alternatives=SelectorTokens(std::move(args),Diagnostics,Bounds,Depth+1);
                        if(test.Alternatives.empty()||Diagnostics.size()!=errors){Failed=true;return false;}
                        if(test.Name!="where"){Specificity maximum;for(const auto& a:test.Alternatives)maximum=std::max(maximum,a.Weight);weight+=maximum;}
                    }else if(test.Name=="nth-child"||test.Name=="nth-last-child"||test.Name=="nth-of-type"||test.Name=="nth-last-of-type"){
                        test.Argument=Lower(Value{args}.ToString());++weight.Classes;
                        // Matching validates the An+B grammar without evaluating untrusted code.
                        if(!ValidNth(test.Argument)){Error("Invalid An+B expression");return false;}
                    }else if(test.Name=="state"){
                        Trim(args);if(args.size()!=1||(args[0].Kind!=TokenKind::String&&args[0].Kind!=TokenKind::Ident)){Error("state() requires one state name");return false;}
                        test.Argument=args[0].Text;++weight.Classes;
                    }else {Error("Unsupported functional pseudo-class: "+test.Name);return false;}
                }else{
                    static const std::set<std::string> names={"hover","active","focus","focus-visible","focus-within","disabled","enabled","checked","selected","root","empty","first-child","last-child","only-child","first-of-type","last-of-type","only-of-type"};
                    if(!names.contains(test.Name)){Error("Unsupported pseudo-class: "+test.Name);return false;}++weight.Classes;
                }
            }else break;
            part.Tests.push_back(std::move(test));initial=false;
        }
        return !part.Tests.empty();
    }
public:
    SelectorParser(std::vector<Token> tokens,std::vector<Diagnostic>& diagnostics,const Limits& limits,std::size_t depth):Tokens(std::move(tokens)),Depth(depth),Diagnostics(diagnostics),Bounds(limits){if(Tokens.empty()||Tokens.back().Kind!=TokenKind::End)Tokens.push_back({TokenKind::End,"","",0,{}});}
    std::vector<Selector> Run(){
        std::vector<Selector> result;Spaces();
        while(Peek().Kind!=TokenKind::End){
            if(result.size()>=Bounds.Selectors){Error("Selector count limit exceeded");break;}
            Selector selector;selector.Source=Peek().Source;Combinator relation=Combinator::None;
            while(true){
                Compound part;part.Relation=relation;
                if(!CompoundSelector(part,selector.Weight)){if(!Failed)Error("Expected a compound selector");return {};}
                selector.Parts.push_back(std::move(part));if(selector.Parts.size()>DepthLimit(Bounds)){Error("Selector chain limit exceeded");return {};}
                const bool spaced=Spaces();if(Peek().Kind==TokenKind::End||Is(","))break;
                if(Is(">")||Is("+")||Is("~")){relation=Is(">")?Combinator::Child:Is("+")?Combinator::AdjacentSibling:Combinator::GeneralSibling;++At;Spaces();}
                else if(spaced)relation=Combinator::Descendant;
                else {Error("Unexpected selector token");return {};}
            }
            if(Failed)return {};
            result.push_back(std::move(selector));
            if(Is(",")){++At;Spaces();if(Peek().Kind==TokenKind::End){Error("Trailing selector comma");return {};}}
        }
        if(result.empty()&&!Failed)Error("Empty selector list");
        return Failed?std::vector<Selector>{}:result;
    }
};
bool Integer(std::string_view text,long long& value){if(text.starts_with('+'))text.remove_prefix(1);if(text.empty())return false;auto r=std::from_chars(text.data(),text.data()+text.size(),value);return r.ec==std::errc{}&&r.ptr==text.data()+text.size();}
std::optional<std::pair<long long,long long>> NthTerms(std::string expression){
    for(std::size_t i=1;i+1<expression.size();++i)if(std::isspace(static_cast<unsigned char>(expression[i]))){
        auto next=i;while(next<expression.size()&&std::isspace(static_cast<unsigned char>(expression[next])))++next;
        if(next<expression.size()&&std::isalnum(static_cast<unsigned char>(expression[i-1]))&&std::isalnum(static_cast<unsigned char>(expression[next])))return {};
    }
    std::erase_if(expression,[](char c){return std::isspace(static_cast<unsigned char>(c));});
    if(expression=="odd")return std::pair<long long,long long>{2,1};
    if(expression=="even")return std::pair<long long,long long>{2,0};
    auto n=expression.find('n');long long a=0,b=0;
    if(n==std::string::npos){if(Integer(expression,b))return std::pair<long long,long long>{0,b};return {};}
    const auto head=expression.substr(0,n),tail=expression.substr(n+1);
    if(head.empty()||head=="+")a=1;else if(head=="-")a=-1;else if(!Integer(head,a))return {};
    if(!tail.empty()&&((tail[0]!='+'&&tail[0]!='-')||!Integer(tail,b)))return {};
    // Bound arithmetic and reject hostile coefficients before subtraction/modulo.
    if(a<-1000000000||a>1000000000||b<-1000000000||b>1000000000)return {};
    return std::pair<long long,long long>{a,b};
}
bool ValidNth(const std::string& expression){return NthTerms(expression).has_value();}
bool Nth(std::string expression,std::size_t index){
    if(index==0||index>1000000000)return false;
    auto terms=NthTerms(std::move(expression));if(!terms)return false;
    const auto [a,b]=*terms;const auto delta=static_cast<long long>(index)-b;return a==0?delta==0:delta%a==0&&delta/a>=0;
}
struct Budget {std::size_t Remaining,MaxDepth;operator std::size_t()const{return Remaining;}Budget& operator--(){--Remaining;return *this;}};
bool Match(const Selector&,std::size_t,const Node*,Budget&,std::size_t);
bool TestNode(const Test& test,const Node& node,Budget& budget,std::size_t depth){
    if(budget==0||depth>budget.MaxDepth)return false;
    --budget;
    switch(test.Type){
    case Test::Kind::Universal:return true;
    case Test::Kind::Type:return Lower(test.Name)==Lower(node.Type);
    case Test::Kind::ID:return test.Name==node.ID;
    case Test::Kind::Class:return node.Classes.contains(test.Name);
    case Test::Kind::Attribute:{
        auto found=node.Attributes.find(test.Name);if(found==node.Attributes.end())return false;if(test.Operator.empty())return true;
        auto a=found->second,b=test.Argument;if(test.CaseInsensitive){a=Lower(a);b=Lower(b);}
        if(test.Operator=="=")return a==b;
        if(b.empty())return false;
        if(test.Operator=="^=")return a.starts_with(b);
        if(test.Operator=="$=")return a.ends_with(b);
        if(test.Operator=="*=")return a.find(b)!=std::string::npos;
        if(test.Operator=="|=")return a==b||a.starts_with(b+"-");
        if(test.Operator=="~="){std::size_t at=0;while(at<a.size()){while(at<a.size()&&std::isspace(static_cast<unsigned char>(a[at])))++at;auto end=at;while(end<a.size()&&!std::isspace(static_cast<unsigned char>(a[end])))++end;if(a.substr(at,end-at)==b)return true;at=end;}return false;}return false;
    }
    case Test::Kind::Pseudo:break;
    }
    const auto& name=test.Name;
    if(name=="is"||name=="where"||name=="not"){
        bool any=false;for(const auto& alternative:test.Alternatives)if(Match(alternative,alternative.Parts.size()-1,&node,budget,depth+1)){any=true;break;}if(!budget)return false;return name=="not"?!any:any;
    }
    if(name=="root")return !node.Parent;
    if(name=="enabled")return !node.States.contains("disabled");
    if(name=="first-child")return node.ChildIndex==1;
    if(name=="last-child")return node.ChildIndex==node.ChildCount;
    if(name=="only-child")return node.ChildCount==1;
    if(name=="first-of-type")return node.TypeIndex==1;
    if(name=="last-of-type")return node.TypeIndex==node.TypeCount;
    if(name=="only-of-type")return node.TypeCount==1;
    if(name=="nth-child")return Nth(test.Argument,node.ChildIndex);
    if(name=="nth-last-child")return node.ChildCount>=node.ChildIndex&&Nth(test.Argument,node.ChildCount-node.ChildIndex+1);
    if(name=="nth-of-type")return Nth(test.Argument,node.TypeIndex);
    if(name=="nth-last-of-type")return node.TypeCount>=node.TypeIndex&&Nth(test.Argument,node.TypeCount-node.TypeIndex+1);
    if(name=="state")return node.States.contains(test.Argument);
    // focus-within and empty are explicit snapshot states; the adapter owns children/focus.
    return node.States.contains(name);
}
bool Match(const Selector& selector,std::size_t index,const Node* node,Budget& budget,std::size_t depth){
    if(!node||budget==0||depth>budget.MaxDepth||index>=selector.Parts.size())return false;
    --budget;const auto& part=selector.Parts[index];for(const auto& test:part.Tests)if(!TestNode(test,*node,budget,depth))return false;
    if(index==0)return true;
    const bool sibling=part.Relation==Combinator::AdjacentSibling||part.Relation==Combinator::GeneralSibling;
    auto* candidate=sibling?node->PreviousSibling:node->Parent;
    for(std::size_t visited=0;candidate&&budget&&visited<budget.MaxDepth;++visited){
        if(Match(selector,index-1,candidate,budget,depth+1))return true;
        if(part.Relation==Combinator::Child||part.Relation==Combinator::AdjacentSibling)break;
        candidate=sibling?candidate->PreviousSibling:candidate->Parent;
    }
    return false;
}
}
namespace Detail {std::vector<Selector> SelectorTokens(std::vector<Token> tokens,std::vector<Diagnostic>& diagnostics,const Limits& limits,std::size_t depth){return SelectorParser(std::move(tokens),diagnostics,limits,depth).Run();}}
namespace Detail {bool MatchSelector(const Selector& selector,const Node& node,std::size_t& remaining,const Limits& limits){Budget budget{remaining,std::min<std::size_t>(128,limits.AncestorDepth)};const bool result=!selector.Parts.empty()&&Match(selector,selector.Parts.size()-1,&node,budget,0);remaining=budget.Remaining;return result;}}
std::vector<Selector> ParseSelectors(std::string_view source,std::vector<Diagnostic>& diagnostics,const Limits& limits){auto tokens=Tokenize(source,diagnostics,limits);return Detail::SelectorTokens(std::move(tokens),diagnostics,limits);}
bool Matches(const Selector& selector,const Node& node,const Limits& limits){auto budget=limits.ResolveWork;return Detail::MatchSelector(selector,node,budget,limits);}
}
