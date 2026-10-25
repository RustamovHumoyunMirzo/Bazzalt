#include "Utils/NumberParsing.h"
#include <cassert>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <sstream>

using Bazzalt::Detail::ParseNumber;
using Bazzalt::Detail::ParseDecimalFallback;
struct CommaDecimal : std::numpunct<char> { char do_decimal_point() const override { return ','; } };

template<class T> void FloatingCases(bool fallback) {
    const auto parse=[fallback](std::string_view text,T& value){return fallback?ParseDecimalFallback(text,value):ParseNumber(text,value);};
    T value{};
    assert(parse("1.25",value)&&value==T(1.25));assert(parse("-.5",value)&&value==T(-.5));
    assert(parse("1.25e+2",value)&&value==T(125));assert(parse("-0",value)&&std::signbit(value));
    for(auto invalid:{""," 1","1 ","+1","1,25","0x1p2","1x","1e","nan","inf","-inf","1e9999","1e-9999"}) {
        value=T(42);const bool accepted=parse(invalid,value);if(accepted)std::cerr<<"Unexpected numeric input accepted: "<<invalid<<" (fallback="<<fallback<<")\n";assert(!accepted);assert(value==T(42));
    }
    assert(!parse(std::string_view("1\0x",3),value));
    for(T input:{std::numeric_limits<T>::max(),std::numeric_limits<T>::min(),std::numeric_limits<T>::denorm_min(),T(1)/T(3)}) {
        std::ostringstream text;text.imbue(std::locale::classic());text<<std::setprecision(std::numeric_limits<T>::max_digits10)<<input;
        const bool accepted=parse(text.str(),value);if(!accepted||value!=input)std::cerr<<"Numeric round-trip failed: "<<text.str()<<" (fallback="<<fallback<<")\n";assert(accepted&&value==input);
    }
}
int main(){
    auto previous=std::locale::global(std::locale(std::locale::classic(),new CommaDecimal));
    FloatingCases<float>(false);FloatingCases<float>(true);FloatingCases<double>(false);FloatingCases<double>(true);
    int integer=3;assert(ParseNumber("-12",integer)&&integer==-12);assert(!ParseNumber("12.5",integer)&&integer==-12);
    unsigned positive=1;assert(!ParseNumber("-1",positive)&&positive==1);
    std::locale::global(previous);
}
