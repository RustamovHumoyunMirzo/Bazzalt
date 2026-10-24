#include "GUI/CSS/CSS.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cctype>
#include <limits>

namespace Bazzalt::Runtime::CSS {
namespace {
bool Space(char c){return c==' '||c=='\t'||c=='\r'||c=='\n'||c=='\f';}
bool NameStart(char c){return std::isalpha(static_cast<unsigned char>(c))||c=='_'||static_cast<unsigned char>(c)>=128;}
bool Name(char c){return NameStart(c)||std::isdigit(static_cast<unsigned char>(c))||c=='-';}
std::optional<std::size_t> InvalidUTF8(std::string_view source){
    for(std::size_t i=0;i<source.size();){
        const auto start=i;const auto c=static_cast<unsigned char>(source[i++]);
        if(c==0)return start;
        if(c<128)continue;
        int count=0;std::uint32_t cp=0,minimum=0;
        if(c>=0xc2&&c<=0xdf){count=1;cp=c&31;minimum=0x80;}
        else if(c>=0xe0&&c<=0xef){count=2;cp=c&15;minimum=0x800;}
        else if(c>=0xf0&&c<=0xf4){count=3;cp=c&7;minimum=0x10000;}
        else return start;
        while(count--){if(i==source.size())return start;const auto next=static_cast<unsigned char>(source[i++]);if((next&0xc0)!=0x80)return start;cp=(cp<<6)|(next&63);}
        if(cp<minimum||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff))return start;
    }return {};
}
void UTF8(std::string& out,std::uint32_t cp){
    if(cp==0||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff))cp=0xfffd;
    if(cp<128)out+=char(cp);
    else if(cp<2048){out+=char(0xc0|(cp>>6));out+=char(0x80|(cp&63));}
    else if(cp<65536){out+=char(0xe0|(cp>>12));out+=char(0x80|((cp>>6)&63));out+=char(0x80|(cp&63));}
    else {out+=char(0xf0|(cp>>18));out+=char(0x80|((cp>>12)&63));out+=char(0x80|((cp>>6)&63));out+=char(0x80|(cp&63));}
}
class Lexer {
    std::string_view Source;std::vector<Diagnostic>& Diagnostics;const Limits& Bounds;
    std::size_t Offset=0,Line=1,Column=1;
    char Peek(std::size_t n=0)const{return Offset+n<Source.size()?Source[Offset+n]:'\0';}
    void Next(){if(Peek()=='\n'){++Line;Column=1;}else ++Column;++Offset;}
    Location Here()const{return {Offset,Line,Column};}
    void Escape(std::string& out){
        const auto at=Here();Next();
        if(!Peek()){Diagnostics.push_back({Severity::Error,at,"Incomplete escape"});UTF8(out,0xfffd);return;}
        if(std::isxdigit(static_cast<unsigned char>(Peek()))){
            std::uint32_t cp=0;std::size_t count=0;
            while(count++<6&&std::isxdigit(static_cast<unsigned char>(Peek()))){char c=Peek();cp=cp*16+(c<='9'?c-'0':std::tolower(static_cast<unsigned char>(c))-'a'+10);Next();}
            if(Space(Peek())){if(Peek()=='\r'&&Peek(1)=='\n')Next();Next();}UTF8(out,cp);
        }else if(Peek()=='\n'||Peek()=='\r'){if(Peek()=='\r'&&Peek(1)=='\n')Next();Next();}
        else {out+=Peek();Next();}
    }
    std::string Identifier(){std::string value;while(Offset<Source.size()&&(Name(Peek())||Peek()=='\\')){if(Peek()=='\\')Escape(value);else {value+=Peek();Next();}}return value;}
    bool StartsName(std::size_t n=0)const{return NameStart(Peek(n))||Peek(n)=='\\'||(Peek(n)=='-'&&(NameStart(Peek(n+1))||Peek(n+1)=='-'||Peek(n+1)=='\\'));}
    bool StartsNumber()const{std::size_t n=Peek()=='+'||Peek()=='-'?1:0;return std::isdigit(static_cast<unsigned char>(Peek(n)))||(Peek(n)=='.'&&std::isdigit(static_cast<unsigned char>(Peek(n+1))));}
public:
    Lexer(std::string_view source,std::vector<Diagnostic>& diagnostics,const Limits& bounds):Source(source),Diagnostics(diagnostics),Bounds(bounds){}
    std::vector<Token> Run(){
        std::vector<Token> tokens;
        if(Source.size()>Bounds.Bytes){Diagnostics.push_back({Severity::Error,Here(),"Stylesheet exceeds byte limit"});return {{TokenKind::End,"","",0,Here()}};}
        if(auto invalid=InvalidUTF8(Source)){
            while(Offset<*invalid)Next();
            Diagnostics.push_back({Severity::Error,Here(),"CSS must be valid UTF-8 without NUL bytes"});return {{TokenKind::End,"","",0,Here()}};
        }
        if(Source.starts_with("\xef\xbb\xbf")){Next();Next();Next();}
        while(Offset<Source.size()){
            const auto at=Here();
            if(tokens.size()>=Bounds.Tokens){Diagnostics.push_back({Severity::Error,at,"Stylesheet exceeds token limit"});break;}
            char c=Peek();
            if(Space(c)){while(Offset<Source.size()&&Space(Peek()))Next();tokens.push_back({TokenKind::Space," ","",0,at});}
            else if(c=='/'&&Peek(1)=='*'){
                Next();Next();while(Offset<Source.size()&&!(Peek()=='*'&&Peek(1)=='/'))Next();
                if(Offset==Source.size())Diagnostics.push_back({Severity::Error,at,"Unterminated comment"});else {Next();Next();}
                // CSS comments disappear; they do not invent a descendant combinator.
            }else if(c=='"'||c=='\''){
                Next();std::string value;bool closed=false;
                while(Offset<Source.size()){
                    if(Peek()==c){Next();closed=true;break;}
                    if(Peek()=='\n'||Peek()=='\r'){Diagnostics.push_back({Severity::Error,at,"Unescaped newline in string"});break;}
                    if(Peek()=='\0')Diagnostics.push_back({Severity::Error,Here(),"NUL is not permitted in CSS strings"});
                    if(Peek()=='\\')Escape(value);else {value+=Peek();Next();}
                }
                if(!closed)Diagnostics.push_back({Severity::Error,at,"Unterminated string"});
                tokens.push_back({TokenKind::String,std::move(value),"",0,at});
            }else if((c=='#'||c=='@')&&(StartsName(1)||(c=='#'&&std::isdigit(static_cast<unsigned char>(Peek(1)))))){
                const bool identifier=StartsName(1);Next();tokens.push_back({c=='#'?TokenKind::Hash:TokenKind::AtKeyword,Identifier(),"",0,at,identifier});
            }else if(StartsNumber()){
                const auto begin=Offset;if(c=='+'||c=='-')Next();while(std::isdigit(static_cast<unsigned char>(Peek())))Next();
                if(Peek()=='.'&&std::isdigit(static_cast<unsigned char>(Peek(1)))){Next();while(std::isdigit(static_cast<unsigned char>(Peek())))Next();}
                if((Peek()=='e'||Peek()=='E')&&(std::isdigit(static_cast<unsigned char>(Peek(1)))||((Peek(1)=='+'||Peek(1)=='-')&&std::isdigit(static_cast<unsigned char>(Peek(2)))))){
                    Next();if(Peek()=='+'||Peek()=='-')Next();while(std::isdigit(static_cast<unsigned char>(Peek())))Next();
                }
                std::string text(Source.substr(begin,Offset-begin));double number=0;auto numeric=std::string_view(text);if(numeric.starts_with('+'))numeric.remove_prefix(1);
                const auto parsed=std::from_chars(numeric.data(),numeric.data()+numeric.size(),number);
                if(parsed.ec!=std::errc{}||!std::isfinite(number)){Diagnostics.push_back({Severity::Error,at,"Invalid or nonfinite number"});number=std::numeric_limits<double>::quiet_NaN();}
                TokenKind kind=TokenKind::Number;std::string unit;
                if(Peek()=='%'){kind=TokenKind::Percentage;unit="%";Next();}else if(StartsName()){kind=TokenKind::Dimension;unit=Identifier();}
                tokens.push_back({kind,std::move(text),std::move(unit),number,at});
            }else if(StartsName())tokens.push_back({TokenKind::Ident,Identifier(),"",0,at});
            else {if(c==0)Diagnostics.push_back({Severity::Error,at,"NUL is not permitted in CSS"});Next();tokens.push_back({TokenKind::Symbol,std::string(1,c),"",0,at});}
        }
        tokens.push_back({TokenKind::End,"","",0,Here()});return tokens;
    }
};
std::string EscapeIdentifier(std::string_view value){
    std::string result;const char* hex="0123456789abcdef";
    for(std::size_t i=0;i<value.size();++i){const auto c=static_cast<unsigned char>(value[i]);
        if((i==0||(i==1&&value[0]=='-'))&&std::isdigit(c)){result+='\\';result+=hex[c>>4];result+=hex[c&15];result+=' ';}
        else if(Name(char(c)))result+=char(c);
        else {result+='\\';result+=char(c);}
    }return result;
}
}
std::vector<Token> Tokenize(std::string_view source,std::vector<Diagnostic>& diagnostics,const Limits& limits){return Lexer(source,diagnostics,limits).Run();}
std::string Value::ToString()const{
    std::string result;const Token* previous=nullptr;
    const auto word=[](const Token& t){return t.Kind==TokenKind::Ident||t.Kind==TokenKind::Number||t.Kind==TokenKind::Dimension||t.Kind==TokenKind::Percentage||t.Kind==TokenKind::Hash||t.Kind==TokenKind::AtKeyword;};
    for(const auto& token:Tokens){
        if(token.Kind==TokenKind::End)continue;
        if(previous&&((word(*previous)&&word(token))||(previous->Kind==TokenKind::Symbol&&previous->Text=="/"&&token.Kind==TokenKind::Symbol&&token.Text=="*")))result+=' ';
        if(token.Kind==TokenKind::String){result+='"';for(unsigned char c:token.Text){if(c<32||c==127){const char* hex="0123456789abcdef";result+='\\';result+=hex[c>>4];result+=hex[c&15];result+=' ';}else {if(c=='"'||c=='\\')result+='\\';result+=char(c);}}result+='"';}
        else if(token.Kind==TokenKind::Ident)result+=EscapeIdentifier(token.Text);
        else {if(token.Kind==TokenKind::Hash)result+='#';if(token.Kind==TokenKind::AtKeyword)result+='@';result+=(token.Kind==TokenKind::Hash&&token.IdentifierHash)||token.Kind==TokenKind::AtKeyword?EscapeIdentifier(token.Text):token.Text;if(token.Kind==TokenKind::Dimension)result+=EscapeIdentifier(token.Unit);else if(token.Kind==TokenKind::Percentage)result+=token.Unit;}
        previous=&token;
    }
    return result;
}
bool Stylesheet::HasErrors()const{for(const auto& d:Diagnostics)if(d.Level==Severity::Error)return true;return false;}
bool ParseResult::HasErrors()const{for(const auto& d:Diagnostics)if(d.Level==Severity::Error)return true;return false;}
}
