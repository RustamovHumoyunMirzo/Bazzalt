#pragma once
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>
#include <stdexcept>
#include <utility>
#include <vector>
namespace Bazzalt {
using FloatList=std::vector<float>;
using IntList=std::vector<int>;
namespace Detail {
template<class T> bool ReadPropertyList(const char* text,std::vector<T>& output){
    if(!text||std::char_traits<char>::length(text)>65536)return false;
    std::istringstream input(text);input.imbue(std::locale::classic());std::vector<T> next;input>>std::ws;
    if(input.eof()){output.clear();return true;}
    while(next.size()<1024){T value;if(!(input>>value)||!std::isfinite(double(value)))return false;next.push_back(value);input>>std::ws;if(input.eof()){output=std::move(next);return true;}char separator;if(!(input>>separator)||separator!=',')return false;}
    return false;
}
template<class T> std::string WritePropertyList(const std::vector<T>& values){
    if(values.size()>1024)throw std::length_error("Inspector list exceeds 1024 items");
    std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(17);bool first=true;
    for(auto value:values){if(!std::isfinite(double(value)))throw std::invalid_argument("Non-finite inspector list value");if(!first)out<<',';first=false;out<<value;}return out.str();
}
}
}
