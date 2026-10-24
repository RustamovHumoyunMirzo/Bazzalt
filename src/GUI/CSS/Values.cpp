#include "GUI/CSS/Internal.h"
#include <cmath>
#include <functional>
#include <numbers>

namespace Bazzalt::Runtime::CSS {
namespace {
using namespace Detail;
struct Quantity {double Value=0;bool Length=false;};
class Arithmetic {
    std::vector<Token> Tokens;const Environment& Env;std::size_t At=0,Depth=0;
    const Token& Peek(){while(At<Tokens.size()&&Tokens[At].Kind==TokenKind::Space)++At;return Tokens[std::min(At,Tokens.size()-1)];}
    bool Take(std::string_view s){if(Symbol(Peek(),s)){++At;return true;}return false;}
    std::optional<Quantity> Factor(){
        if(++Depth>64){--Depth;return {};}
        struct Guard{std::size_t& Value;~Guard(){--Value;}} guard{Depth};
        if(Take("+"))return Factor();
        if(Take("-")){auto v=Factor();if(v)v->Value=-v->Value;return v;}
        if(Take("(")){auto v=Expression();if(!Take(")"))return {};return v;}
        const auto token=Peek();
        if(token.Kind==TokenKind::Ident){
            const auto name=Lower(token.Text);++At;if(!Take("("))return {};
            std::vector<Quantity> values;
            do{auto v=Expression();if(!v)return {};values.push_back(*v);if(values.size()>256)return {};}while(Take(","));
            if(!Take(")")||values.empty())return {};
            for(const auto& v:values)if(v.Length!=values[0].Length)return {};
            if(name=="calc"&&values.size()==1)return values[0];
            if(name=="min"||name=="max"){auto v=values[0];for(const auto& q:values)v.Value=name=="min"?std::min(v.Value,q.Value):std::max(v.Value,q.Value);return v;}
            if(name=="clamp"&&values.size()==3)return Quantity{std::max(values[0].Value,std::min(values[1].Value,values[2].Value)),values[0].Length};
            return {};
        }
        if(token.Kind!=TokenKind::Number&&token.Kind!=TokenKind::Dimension&&token.Kind!=TokenKind::Percentage)return {};
        ++At;if(!std::isfinite(token.Number))return {};
        if(token.Kind==TokenKind::Number)return Quantity{token.Number,false};
        double scale=0;auto unit=Lower(token.Unit);
        if(unit=="px"||unit=="dp")scale=1;else if(unit=="%")scale=Env.PercentageBase/100;
        else if(unit=="em")scale=Env.FontSize;else if(unit=="rem")scale=Env.RootFontSize;
        else if(unit=="vw")scale=Env.Width/100;else if(unit=="vh")scale=Env.Height/100;
        else if(unit=="vmin")scale=std::min(Env.Width,Env.Height)/100;else if(unit=="vmax")scale=std::max(Env.Width,Env.Height)/100;
        else return {};
        return Quantity{token.Number*scale,true};
    }
    std::optional<Quantity> Product(){
        auto left=Factor();if(!left)return {};
        while(true){
            bool multiply=Take("*");if(!multiply&&!Take("/"))break;
            auto right=Factor();if(!right)return {};
            if(multiply){if(left->Length&&right->Length)return {};left->Value*=right->Value;left->Length=left->Length||right->Length;}
            else {if(right->Value==0||(!left->Length&&right->Length))return {};left->Value/=right->Value;left->Length=left->Length!=right->Length;}
        }
        return std::isfinite(left->Value)?left:std::nullopt;
    }
    std::optional<Quantity> Expression(){
        auto left=Product();if(!left)return {};
        while(true){bool add=Take("+");if(!add&&!Take("-"))break;auto right=Product();if(!right||left->Length!=right->Length)return {};left->Value+=add?right->Value:-right->Value;}
        return std::isfinite(left->Value)?left:std::nullopt;
    }
public:
    Arithmetic(const Value& value,const Environment& env):Tokens(value.Tokens),Env(env){Tokens.push_back({TokenKind::End,"","",0,{}});}
    std::optional<double> Run(){auto value=Expression();return value&&Peek().Kind==TokenKind::End&&std::isfinite(value->Value)?std::optional<double>(value->Value):std::nullopt;}
};
std::vector<Token> Compact(const Value& value){auto result=value.Tokens;std::erase_if(result,[](const Token& t){return t.Kind==TokenKind::Space||t.Kind==TokenKind::End;});return result;}
bool FunctionGap(const Value& value){
    for(std::size_t i=0;i+2<value.Tokens.size();++i)if(value.Tokens[i].Kind==TokenKind::Ident&&value.Tokens[i+1].Kind==TokenKind::Space){
        auto next=i+1;while(next<value.Tokens.size()&&value.Tokens[next].Kind==TokenKind::Space)++next;
        if(next<value.Tokens.size()&&Symbol(value.Tokens[next],"("))return true;
    }return false;
}
double Clamp(double value){return std::clamp(value,0.,1.);}
std::optional<double> Channel(const Token& token,double range){if(!std::isfinite(token.Number))return {};if(token.Kind==TokenKind::Number)return token.Number/range;if(token.Kind==TokenKind::Percentage)return token.Number/100;return {};}
}
std::optional<double> ResolveLength(const Value& value,const Environment& environment){return FunctionGap(value)?std::nullopt:Arithmetic(value,environment).Run();}
std::optional<std::string> ResolveString(const Value& value){auto t=Compact(value);if(t.size()==1&&(t[0].Kind==TokenKind::String||t[0].Kind==TokenKind::Ident))return t[0].Text;return {};}
std::optional<bool> ResolveBoolean(const Value& value){auto text=ResolveString(value);if(!text)return {};if(Lower(*text)=="true")return true;if(Lower(*text)=="false")return false;return {};}
std::optional<std::string> ResolveReference(const Value& value){
    if(FunctionGap(value))return {};
    auto tokens=Compact(value);if(tokens.size()<4||tokens[0].Kind!=TokenKind::Ident||(Lower(tokens[0].Text)!="url"&&Lower(tokens[0].Text)!="asset")||!Symbol(tokens[1],"(")||!Symbol(tokens.back(),")"))return {};
    if(tokens.size()==4&&tokens[2].Kind==TokenKind::String)return tokens[2].Text;
    auto original=value.Tokens;Trim(original);
    if(original.size()>=3){auto body=Slice(original,2,original.size()-1);Trim(body);for(const auto& t:body)if(t.Kind==TokenKind::Space)return {};}
    std::string reference;for(std::size_t i=2;i+1<tokens.size();++i){if(tokens[i].Kind==TokenKind::String||Symbol(tokens[i],"(")||Symbol(tokens[i],")"))return {};if(tokens[i].Kind==TokenKind::Hash)reference+='#';reference+=tokens[i].Text;reference+=tokens[i].Unit;}
    return reference.empty()?std::nullopt:std::optional<std::string>(reference);
}
std::optional<Color> ResolveColor(const Value& value,std::optional<Color> currentColor){
    if(FunctionGap(value))return {};
    auto t=Compact(value);if(t.empty())return {};
    if(t.size()==1&&t[0].Kind==TokenKind::Hash){
        const auto& hex=t[0].Text;const auto n=hex.size();if(n!=3&&n!=4&&n!=6&&n!=8)return {};
        auto digit=[](char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;};
        double channels[4]={0,0,0,1};for(std::size_t i=0;i<(n==3||n==4?n:n/2);++i){int a=digit(hex[n<=4?i:i*2]),b=n<=4?a:digit(hex[i*2+1]);if(a<0||b<0)return {};channels[i]=(a*16+b)/255.;}
        return Color{channels[0],channels[1],channels[2],channels[3]};
    }
    if(t.size()==1&&t[0].Kind==TokenKind::Ident){
        const auto name=Lower(t[0].Text);if(name=="currentcolor")return currentColor;
        if(name=="transparent")return Color{0,0,0,0};
        static const std::map<std::string,std::uint32_t> colors={
            {"black",0x000000},{"silver",0xc0c0c0},{"gray",0x808080},{"grey",0x808080},{"white",0xffffff},
            {"maroon",0x800000},{"red",0xff0000},{"purple",0x800080},{"fuchsia",0xff00ff},{"green",0x008000},{"lime",0x00ff00},
            {"olive",0x808000},{"yellow",0xffff00},{"navy",0x000080},{"blue",0x0000ff},{"teal",0x008080},{"aqua",0x00ffff},
            {"orange",0xffa500},{"rebeccapurple",0x663399},{"magenta",0xff00ff},{"cyan",0x00ffff}};
        auto found=colors.find(name);if(found==colors.end())return {};auto c=found->second;return Color{double((c>>16)&255)/255,double((c>>8)&255)/255,double(c&255)/255,1};
    }
    if(t.size()<6||t[0].Kind!=TokenKind::Ident||!Symbol(t[1],"(")||!Symbol(t.back(),")"))return {};
    const auto name=Lower(t[0].Text);std::vector<Token> channels;bool comma=false,slash=false;std::size_t slashAt=0,commas=0;
    for(std::size_t i=2;i+1<t.size();++i){
        if(Symbol(t[i],",")){if(slash||channels.empty()||Symbol(t[i-1],",")||channels.size()!=commas+1)return {};comma=true;++commas;continue;}
        if(Symbol(t[i],"/")){if(comma||slash||channels.size()!=std::size_t(name=="color"?4:3))return {};slash=true;slashAt=3;continue;}
        channels.push_back(t[i]);
    }
    if(Symbol(t[t.size()-2],","))return {};
    if(comma&&commas+1!=channels.size())return {};
    if(channels.size()==std::size_t(name=="color"?5:4)&&!slash&&!comma)return {};
    if(name=="color"){
        if(comma)return {};
        if(channels.empty()||Lower(channels.front().Text)!="srgb")return {};
        channels.erase(channels.begin());
        // color(srgb r g b / a) has its color-space identifier before the channels.
        if(channels.size()!=3&&channels.size()!=4)return {};
    }else if(channels.size()!=3&&channels.size()!=4)return {};
    if(slash&&slashAt!=3)return {};
    double alpha=1;if(channels.size()==4){auto a=Channel(channels[3],1);if(!a)return {};alpha=Clamp(*a);}
    if(name=="rgb"||name=="rgba"||name=="color"){
        double c[3];for(int i=0;i<3;++i){auto v=Channel(channels[i],name=="color"?1:255);if(!v)return {};c[i]=Clamp(*v);}return Color{c[0],c[1],c[2],alpha};
    }
    if(name!="hsl"&&name!="hsla"&&name!="hwb")return {};
    if(name=="hwb"&&comma)return {};
    double hue=channels[0].Number;const auto unit=Lower(channels[0].Unit);
    if(channels[0].Kind==TokenKind::Dimension){if(unit=="turn")hue*=360;else if(unit=="rad")hue*=180/std::numbers::pi;else if(unit=="grad")hue*=.9;else if(unit!="deg")return {};}
    else if(channels[0].Kind!=TokenKind::Number)return {};
    if(!std::isfinite(hue)||channels[1].Kind!=TokenKind::Percentage||channels[2].Kind!=TokenKind::Percentage)return {};
    hue=std::fmod(std::fmod(hue,360.)+360.,360.)/60.;const double x=1-std::fabs(std::fmod(hue,2.)-1);
    Color c=hue<1?Color{1,x,0,alpha}:hue<2?Color{x,1,0,alpha}:hue<3?Color{0,1,x,alpha}:hue<4?Color{0,x,1,alpha}:hue<5?Color{x,0,1,alpha}:Color{1,0,x,alpha};
    if(!std::isfinite(channels[1].Number)||!std::isfinite(channels[2].Number))return {};
    const double p=Clamp(channels[1].Number/100),q=Clamp(channels[2].Number/100);
    if(name=="hwb"){
        if(p+q>=1)return Color{p/(p+q),p/(p+q),p/(p+q),alpha};
        const auto scale=1-p-q;return Color{c.R*scale+p,c.G*scale+p,c.B*scale+p,alpha};
    }
    const double chroma=(1-std::fabs(2*q-1))*p,m=q-chroma*.5;
    return Color{c.R*chroma+m,c.G*chroma+m,c.B*chroma+m,alpha};
}
std::optional<Value> SubstituteVariables(const Value& value,const std::map<std::string,Value>& variables,std::vector<Diagnostic>& diagnostics,const Limits& limits){
    std::vector<std::string> stack;std::set<std::string> invalid,visited;std::size_t bytes=0,work=0;
    bool exhausted=false;
    std::function<void(const Value&,std::size_t)> dependencies;
    std::function<void(const std::string&,std::size_t)> visit;
    visit=[&](const std::string& name,std::size_t depth){
        if(depth>DepthLimit(limits)){exhausted=true;return;}
        const auto cycle=std::find(stack.begin(),stack.end(),name);
        if(cycle!=stack.end()){invalid.insert(cycle,stack.end());return;}
        if(visited.contains(name))return;
        auto found=variables.find(name);if(found==variables.end())return;
        stack.push_back(name);dependencies(found->second,depth+1);stack.pop_back();visited.insert(name);
    };
    dependencies=[&](const Value& data,std::size_t depth){
        for(std::size_t i=0;i<data.Tokens.size();++i){
            if(++work>limits.Tokens){exhausted=true;return;}
            const auto& token=data.Tokens[i];
            if(token.Kind!=TokenKind::Ident||Lower(token.Text)!="var"||i+1>=data.Tokens.size()||!Symbol(data.Tokens[i+1],"("))continue;
            auto key=i+2;while(key<data.Tokens.size()&&data.Tokens[key].Kind==TokenKind::Space)++key;
            if(key<data.Tokens.size()&&data.Tokens[key].Kind==TokenKind::Ident)visit(data.Tokens[key].Text,depth);
        }
    };
    // Dependency cycles include references in fallbacks, even if that fallback
    // would not be selected. Do not invalidate noncyclic consumers of a cycle.
    dependencies(value,0);
    std::function<std::optional<Value>(const Value&,std::size_t)> expand;
    expand=[&](const Value& input,std::size_t depth)->std::optional<Value>{
        if(depth>DepthLimit(limits))return {};
        Value result;
        for(std::size_t i=0;i<input.Tokens.size();++i){
            const auto& token=input.Tokens[i];if(++work>limits.Tokens)return {};
            if(token.Kind==TokenKind::Ident&&Lower(token.Text)=="var"&&i+1<input.Tokens.size()&&Symbol(input.Tokens[i+1],"(")){
                std::size_t end=i+2,nesting=1,comma=input.Tokens.size();
                for(;end<input.Tokens.size();++end){const auto& t=input.Tokens[end];if(Symbol(t,"("))++nesting;if(Symbol(t,")")&&!--nesting)break;if(nesting==1&&Symbol(t,",")&&comma==input.Tokens.size())comma=end;}
                if(end==input.Tokens.size())return {};
                auto key=Slice(input.Tokens,i+2,std::min(comma,end));Trim(key);
                if(key.size()!=1||key[0].Kind!=TokenKind::Ident||!key[0].Text.starts_with("--")||key[0].Text.size()==2)return {};
                std::optional<Value> replacement;auto found=variables.find(key[0].Text);
                if(found!=variables.end()&&!invalid.contains(key[0].Text)){
                    replacement=expand(found->second,depth+1);
                }
                if(!replacement&&comma<end)replacement=expand(Value{Slice(input.Tokens,comma+1,end)},depth+1);
                if(!replacement)return {};
                result.Tokens.insert(result.Tokens.end(),replacement->Tokens.begin(),replacement->Tokens.end());i=end;
            }else {bytes+=token.Text.size()+token.Unit.size()+1;if(bytes>limits.ExpansionBytes)return {};result.Tokens.push_back(token);}
            if(result.Tokens.size()>limits.Tokens)return {};
        }
        return result;
    };
    auto result=exhausted?std::nullopt:expand(value,0);
    if(!result)diagnostics.push_back({Severity::Error,value.Tokens.empty()?Location{}:value.Tokens.front().Source,"Invalid, cyclic, missing or over-limit custom property expansion"});
    return result;
}
}
