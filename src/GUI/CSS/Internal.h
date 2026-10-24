#pragma once
#include "GUI/CSS/CSS.h"
#include <algorithm>
#include <cctype>
#include <span>
namespace Bazzalt::Runtime::CSS::Detail {
inline std::string Lower(std::string value){for(auto& c:value)if(c>='A'&&c<='Z')c=char(c-'A'+'a');return value;}
inline bool Symbol(const Token& token,std::string_view value){return token.Kind==TokenKind::Symbol&&token.Text==value;}
inline void Trim(std::vector<Token>& tokens){while(!tokens.empty()&&(tokens.back().Kind==TokenKind::Space||tokens.back().Kind==TokenKind::End))tokens.pop_back();auto i=std::find_if(tokens.begin(),tokens.end(),[](const Token& t){return t.Kind!=TokenKind::Space;});tokens.erase(tokens.begin(),i);}
inline std::vector<Token> Slice(const std::vector<Token>& tokens,std::size_t a,std::size_t b){return {tokens.begin()+a,tokens.begin()+b};}
std::vector<Selector> SelectorTokens(std::vector<Token> tokens,std::vector<Diagnostic>& diagnostics,const Limits& limits,std::size_t depth=0);
bool MatchSelector(const Selector& selector,const Node& node,std::size_t& remaining,const Limits& limits);
inline std::size_t DepthLimit(const Limits& limits){return std::min<std::size_t>(limits.Nesting,128);}
}
