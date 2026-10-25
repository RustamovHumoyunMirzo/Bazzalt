#pragma once
#include <charconv>
#include <cmath>
#include <locale>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>

namespace Bazzalt::Detail {
inline bool IsDecimalNumber(std::string_view text) {
    std::size_t at=0;if(at<text.size()&&text[at]=='-')++at;
    const auto digits=[&](){const auto begin=at;while(at<text.size()&&text[at]>='0'&&text[at]<='9')++at;return at>begin;};
    bool mantissa=digits();if(at<text.size()&&text[at]=='.'){++at;mantissa=digits()||mantissa;}if(!mantissa)return false;
    if(at<text.size()&&(text[at]=='e'||text[at]=='E')){++at;if(at<text.size()&&(text[at]=='+'||text[at]=='-'))++at;if(!digits())return false;}
    return at==text.size();
}
// libc++ 18 has no floating-point from_chars. Keep the same strict, locale-free
// decimal input contract without requiring a newer runtime ABI or global locale.
template<class T> bool ParseDecimalFallback(std::string_view text,T& value) {
    static_assert(std::is_floating_point_v<T>);
    if(!IsDecimalNumber(text))return false;
    T parsed{};std::istringstream input{std::string(text)};input.imbue(std::locale::classic());
    input>>std::noskipws>>parsed;
    if(!input||input.peek()!=std::char_traits<char>::eof()||!std::isfinite(parsed))return false;
    // Some num_get implementations report a rounded-to-zero underflow without
    // failbit, unlike from_chars. Reject it without rejecting literal zero.
    if(parsed==T{})for(char c:text){if(c=='e'||c=='E')break;if(c>='1'&&c<='9')return false;}
    value=parsed;return true;
}

template<class T> bool ParseNumber(std::string_view text,T& value) {
    static_assert(std::is_arithmetic_v<T> && !std::is_same_v<T,bool>);
    if(text.empty())return false;
    if constexpr(requires(const char* begin,T& number){std::from_chars(begin,begin,number);}) {
        T parsed{};const auto end=text.data()+text.size();auto result=std::from_chars(text.data(),end,parsed);
        if(result.ec!=std::errc{}||result.ptr!=end)return false;
        if constexpr(std::is_floating_point_v<T>)if(!std::isfinite(parsed))return false;
        value=parsed;return true;
    }else return ParseDecimalFallback(text,value);
}
}
