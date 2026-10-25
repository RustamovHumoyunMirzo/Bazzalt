#include "GUI/GuiPaint.h"
#include <algorithm>
#include <cmath>
#include <set>
namespace Bazzalt::Runtime {
namespace {
CSS::Value Trim(CSS::Value value){while(!value.Tokens.empty()&&value.Tokens.front().Kind==CSS::TokenKind::Space)value.Tokens.erase(value.Tokens.begin());while(!value.Tokens.empty()&&value.Tokens.back().Kind==CSS::TokenKind::Space)value.Tokens.pop_back();return value;}
std::vector<CSS::Value> Split(const CSS::Value& value,const std::string& separator){std::vector<CSS::Value> result(1);int depth=0;for(const auto& token:value.Tokens){if(token.Text=="(")++depth;if(token.Text==")")--depth;if(!depth&&((separator==" "&&token.Kind==CSS::TokenKind::Space)||token.Text==separator)){if(!result.back().Tokens.empty())result.emplace_back();}else result.back().Tokens.push_back(token);}if(result.back().Tokens.empty())result.pop_back();for(auto& part:result)part=Trim(std::move(part));return result;}
std::string Lower(std::string value){for(auto& c:value)if(c>='A'&&c<='Z')c=char(c-'A'+'a');return value;}
std::optional<float> Length(const CSS::Value& value,CSS::Environment env){auto n=CSS::ResolveLength(value,env);if(!n||!std::isfinite(*n)||*n<0||*n>100000)return {};return float(*n);}
std::optional<float> Angle(const CSS::Value& value){auto tokens=Trim(value).Tokens;if(tokens.size()!=1)return {};auto& t=tokens[0];double degrees=t.Number;if(t.Kind==CSS::TokenKind::Dimension){auto unit=Lower(t.Unit);if(unit=="rad")degrees*=180/3.141592653589793;else if(unit=="turn")degrees*=360;else if(unit=="grad")degrees*=.9;else if(unit!="deg")return {};}else if(t.Kind!=CSS::TokenKind::Number||degrees!=0)return {};if(!std::isfinite(degrees))return {};return std::fabs(degrees)<=100000000?std::optional<float>{float(degrees)}:std::optional<float>{};}
std::optional<Vec4> Color(const CSS::Value& value){auto c=CSS::ResolveColor(value);if(!c)return {};return Vec4{float(c->R),float(c->G),float(c->B),float(c->A)};}
bool QuadValues(const CSS::Value& data,CSS::Environment env,Vec4& out,bool corners=false){auto parts=Split(data," ");if(parts.empty()||parts.size()>4)return false;float v[4]{};for(std::size_t i=0;i<parts.size();++i){auto n=Length(parts[i],env);if(!n)return false;v[i]=*n;}const float top=v[0],right=parts.size()>1?v[1]:top,bottom=parts.size()>2?v[2]:top,left=parts.size()>3?v[3]:right;out=corners?Vec4{top,right,bottom,left}:Vec4{left,top,right,bottom};return true;}
bool Gradient(const CSS::Value& data,CSS::Environment env,Vec2 size,GuiGradient& out){
    const auto value=Trim(data);if(value.Tokens.size()<4||value.Tokens[1].Text!="("||value.Tokens.back().Text!=")")return false;
    auto name=Lower(value.Tokens[0].Text);out.Repeat=name.starts_with("repeating-");if(out.Repeat)name.erase(0,10);
    out.Kind=name=="linear-gradient"?1:name=="radial-gradient"?2:name=="conic-gradient"?3:0;if(!out.Kind)return false;
    auto arguments=Split(CSS::Value{{value.Tokens.begin()+2,value.Tokens.end()-1}},",");if(arguments.size()<2)return false;
    auto first=Split(arguments[0]," ");std::size_t begin=0;
    if(!first.empty()&&!Color(first[0])){
        begin=1;auto prelude=Lower(arguments[0].ToString());
        if(out.Kind==1){if(auto angle=Angle(arguments[0]))out.Angle=*angle;else if(prelude.starts_with("to ")){const float dx=prelude.find("right")!=std::string::npos?1.f:prelude.find("left")!=std::string::npos?-1.f:0.f,dy=prelude.find("bottom")!=std::string::npos?1.f:prelude.find("top")!=std::string::npos?-1.f:0.f;if(dx==0&&dy==0)return false;out.Angle=std::atan2(dx,-dy)*180/3.14159265f;}else return false;}
        else {
            std::size_t at=first.size();for(std::size_t i=0;i<first.size();++i)if(Lower(first[i].ToString())=="at"){at=i;break;}
            if(out.Kind==3){out.Angle=0;if(at!=0&&at!=first.size()){if(at!=2||Lower(first[0].ToString())!="from")return false;auto angle=Angle(first[1]);if(!angle)return false;out.Angle=*angle;}else if(at==first.size()&&!first.empty()){if(first.size()!=2||Lower(first[0].ToString())!="from")return false;auto angle=Angle(first[1]);if(!angle)return false;out.Angle=*angle;}}
            if(at<first.size()){
                auto position=[&](const CSS::Value& v,bool vertical)->std::optional<float>{auto key=Lower(v.ToString());if(key=="center")return .5f;if(key==(vertical?"top":"left"))return 0.f;if(key==(vertical?"bottom":"right"))return 1.f;auto e=env;e.PercentageBase=vertical?size.Y:size.X;auto n=CSS::ResolveLength(v,e);if(!n)return {};if(!std::isfinite(*n)||std::fabs(*n)>100000)return {};return float(*n)/std::max(1.f,vertical?size.Y:size.X);};
                if(first.size()-at<2||first.size()-at>3)return false;auto x=position(first[at+1],false),y=first.size()-at==3?position(first[at+2],true):std::optional<float>{.5f};if(!x||!y)return false;out.Center={*x,*y};
            }
            if(out.Kind==2){
                bool circle=false,ellipse=false,hasExtent=false;std::string extent="farthest-corner";std::vector<float> radii;
                for(std::size_t i=0;i<at;++i){auto word=Lower(first[i].ToString());
                    if(word=="circle")circle=true;else if(word=="ellipse")ellipse=true;
                    else if(word=="closest-side"||word=="farthest-side"||word=="closest-corner"||word=="farthest-corner"){extent=word;hasExtent=true;}
                    else {auto e=env;e.PercentageBase=radii.empty()?size.X:size.Y;auto radius=Length(first[i],e);if(!radius||radii.size()>=2)return false;radii.push_back(*radius);}
                }
                if(circle&&ellipse)return false;
                if(!radii.empty()){if(hasExtent||(circle&&radii.size()!=1)||(ellipse&&radii.size()!=2))return false;if(radii.size()==1)out.Radius={radii[0]/std::max(1.f,size.X),radii[0]/std::max(1.f,size.Y)};else out.Radius={radii[0]/std::max(1.f,size.X),radii[1]/std::max(1.f,size.Y)};}
                else {const bool closest=extent.starts_with("closest"),corner=extent.ends_with("corner");const float x=closest?std::min(out.Center.X,1-out.Center.X):std::max(out.Center.X,1-out.Center.X),y=closest?std::min(out.Center.Y,1-out.Center.Y):std::max(out.Center.Y,1-out.Center.Y);
                    if(circle){const float radius=corner?std::hypot(x*size.X,y*size.Y):closest?std::min(x*size.X,y*size.Y):std::max(x*size.X,y*size.Y);out.Radius={radius/std::max(1.f,size.X),radius/std::max(1.f,size.Y)};}
                    else out.Radius={x*(corner?1.41421356f:1.f),y*(corner?1.41421356f:1.f)};
                }
            }
        }
    }else if(out.Kind==3)out.Angle=0;
    if(out.Kind==2&&begin==0)out.Radius={.70710678f,.70710678f};
    std::array<bool,8> specified{};const float radians=out.Angle*3.14159265f/180;
    env.PercentageBase=out.Kind==1?std::fabs(std::sin(radians))*size.X+std::fabs(std::cos(radians))*size.Y:out.Kind==2?std::max(size.X*out.Radius.X,size.Y*out.Radius.Y):360;
    for(std::size_t i=begin;i<arguments.size();++i){auto parts=Split(arguments[i]," ");if(parts.empty()||parts.size()>3)return false;auto color=Color(parts[0]);if(!color)return false;const auto copies=std::max<std::size_t>(1,parts.size()-1);
        for(std::size_t j=0;j<copies;++j){if(out.Count>=8)return false;out.Colors[out.Count]=*color;if(parts.size()>1){std::optional<float> n;if(out.Kind==3){auto token=Trim(parts[j+1]);if(token.Tokens.size()==1&&token.Tokens[0].Kind==CSS::TokenKind::Percentage)n=float(token.Tokens[0].Number)/100;else if(auto angle=Angle(token))n=*angle/360;}else if(auto length=CSS::ResolveLength(parts[j+1],env))n=float(*length/std::max(1.,env.PercentageBase));if(!n||!std::isfinite(*n)||std::fabs(*n)>100000)return false;out.Stops[out.Count]=*n;specified[out.Count]=true;}++out.Count;}}
    if(out.Count<2)return false;if(!specified[0]){out.Stops[0]=0;specified[0]=true;}if(!specified[out.Count-1]){out.Stops[out.Count-1]=1;specified[out.Count-1]=true;}
    for(int i=1;i<out.Count;++i)if(specified[i]){int start=i-1;while(!specified[start])--start;out.Stops[i]=std::max(out.Stops[start],out.Stops[i]);for(int j=start+1;j<i;++j)out.Stops[j]=out.Stops[start]+(out.Stops[i]-out.Stops[start])*float(j-start)/float(i-start);}
    return true;
}
bool Filters(const CSS::Value& data,CSS::Environment env,std::vector<GuiFilter>& out){auto value=Trim(data);if(Lower(value.ToString())=="none")return true;std::size_t at=0;
    while(at<value.Tokens.size()){if(value.Tokens[at].Kind==CSS::TokenKind::Space){++at;continue;}if(at+1>=value.Tokens.size()||value.Tokens[at+1].Text!="("||out.size()>=8)return false;auto name=Lower(value.Tokens[at].Text);std::size_t end=at+2;int depth=1;for(;end<value.Tokens.size();++end){if(value.Tokens[end].Text=="(")++depth;if(value.Tokens[end].Text==")"&&!--depth)break;}if(end==value.Tokens.size())return false;auto argument=Trim(CSS::Value{{value.Tokens.begin()+at+2,value.Tokens.begin()+end}});GuiFilter filter;
        static const std::map<std::string,int> kinds={{"brightness",1},{"contrast",2},{"grayscale",3},{"invert",4},{"opacity",5},{"saturate",6},{"sepia",7},{"hue-rotate",8},{"blur",9},{"drop-shadow",10}};auto found=kinds.find(name);if(found==kinds.end())return false;filter.Kind=found->second;
        if(filter.Kind==10){auto parts=Split(argument," ");std::vector<float> lengths;for(const auto& part:parts){if(auto color=Color(part))filter.Color=*color;else {auto n=CSS::ResolveLength(part,env);if(!n||std::fabs(*n)>10000)return false;lengths.push_back(float(*n));}}if(lengths.size()<2||lengths.size()>3)return false;filter.Offset={lengths[0],lengths[1]};filter.Amount=lengths.size()==3?lengths[2]:0;if(filter.Amount<0||filter.Amount>128)return false;}
        else if(filter.Kind==8){auto angle=Angle(argument);if(!angle)return false;filter.Amount=*angle*3.14159265f/180;}
        else {if(argument.Tokens.empty())filter.Amount=filter.Kind==9?0:1;else if(filter.Kind==9){auto n=Length(argument,env);if(!n||*n>128)return false;filter.Amount=*n;}else {if(std::any_of(argument.Tokens.begin(),argument.Tokens.end(),[](const CSS::Token& token){return token.Kind==CSS::TokenKind::Dimension;}))return false;auto n=CSS::ResolveLength(argument,CSS::Environment{.PercentageBase=1});if(!n||*n<0||*n>16)return false;filter.Amount=float(*n);if(filter.Kind==3||filter.Kind==4||filter.Kind==5||filter.Kind==7)filter.Amount=std::min(1.f,filter.Amount);}}
        out.push_back(filter);at=end+1;
    }return !out.empty();
}
}
bool IsGuiPaintProperty(const std::string& name){return name=="background"||name=="border"||name=="border-style"||name=="border-radius"||name=="filter"||name=="backdrop-filter"||name=="border-top-left-radius"||name=="border-top-right-radius"||name=="border-bottom-right-radius"||name=="border-bottom-left-radius"||name=="border-left-width"||name=="border-top-width"||name=="border-right-width"||name=="border-bottom-width"||name=="border-left-color"||name=="border-top-color"||name=="border-right-color"||name=="border-bottom-color";}
bool GuiPaintContains(const GuiPaint& paint,Vec2 p,Vec2 size){
    if(p.X<0||p.Y<0||p.X>=size.X||p.Y>=size.Y)return false;if(!paint.HasRadius)return true;
    const bool left=p.X<size.X*.5f,top=p.Y<size.Y*.5f;const int corner=top?(left?0:1):(left?3:2);
    const float xs[]={paint.RadiusX.X,paint.RadiusX.Y,paint.RadiusX.Z,paint.RadiusX.W},ys[]={paint.RadiusY.X,paint.RadiusY.Y,paint.RadiusY.Z,paint.RadiusY.W};const float rx=xs[corner],ry=ys[corner];
    if(rx<=0||ry<=0)return true;const float x=(p.X-(left?rx:size.X-rx))*(left?-1.f:1.f),y=(p.Y-(top?ry:size.Y-ry))*(top?-1.f:1.f);return x<=0||y<=0||(x*x/(rx*rx)+y*y/(ry*ry)<=1);
}
GuiPaint ResolveGuiPaint(const CSS::ComputedStyle& style,CSS::Environment env,Vec2 size,std::vector<std::string>* diagnostics,const GuiPaint* parent){
    GuiPaint result;result.BorderColors.fill({.05f,.06f,.08f,1});auto error=[&](const std::string& name){if(diagnostics&&diagnostics->size()<64)diagnostics->push_back("Invalid or unsupported GUI CSS value: "+name);};
    const GuiPaint initial=result;
    auto find=[&](const std::string& name)->const CSS::Value*{
        auto it=style.Properties.find(name);if(it==style.Properties.end())return nullptr;
        const auto key=Lower(Trim(it->second).ToString());if(key!="inherit"&&key!="initial"&&key!="unset")return &it->second;
        const auto& source=key=="inherit"&&parent?*parent:initial;
        if(name=="border"||name=="border-width"){result.Widths=source.Widths;result.HasBorder=true;}
        if(name=="border"||name=="border-color")result.BorderColors=source.BorderColors;
        if(name=="border"||name=="border-style"){result.BorderNone=source.BorderNone;if(result.BorderNone){result.Widths={};result.HasBorder=true;}}
        if(name=="border-radius"){result.RadiusX=source.RadiusX;result.RadiusY=source.RadiusY;result.HasRadius=source.HasRadius;}
        const char* sides[]={"left","top","right","bottom"};float* widths[]={&result.Widths.X,&result.Widths.Y,&result.Widths.Z,&result.Widths.W};const float sw[]={source.Widths.X,source.Widths.Y,source.Widths.Z,source.Widths.W};
        const char* corners[]={"top-left","top-right","bottom-right","bottom-left"};float* rx[]={&result.RadiusX.X,&result.RadiusX.Y,&result.RadiusX.Z,&result.RadiusX.W};float* ry[]={&result.RadiusY.X,&result.RadiusY.Y,&result.RadiusY.Z,&result.RadiusY.W};const float sx[]={source.RadiusX.X,source.RadiusX.Y,source.RadiusX.Z,source.RadiusX.W},sy[]={source.RadiusY.X,source.RadiusY.Y,source.RadiusY.Z,source.RadiusY.W};
        for(int i=0;i<4;++i){if(name==std::string("border-")+sides[i]+"-width"){*widths[i]=sw[i];result.HasBorder=true;}if(name==std::string("border-")+sides[i]+"-color")result.BorderColors[i]=source.BorderColors[i];if(name==std::string("border-")+corners[i]+"-radius"){*rx[i]=sx[i];*ry[i]=sy[i];result.HasRadius=true;}}
        if(name=="filter")result.Filters=source.Filters;if(name=="backdrop-filter")result.Backdrop=source.Backdrop;
        if(name=="background"||name=="background-image")result.Gradients=source.Gradients;
        return nullptr;
    };
    if(auto* data=find("border")){for(const auto& part:Split(*data," ")){if(auto color=Color(part))result.BorderColors.fill(*color);else if(Lower(part.ToString())=="solid"){}else if(auto n=Length(part,env)){result.Widths={*n,*n,*n,*n};result.HasBorder=true;}else error("border");}}
    if(auto* data=find("border-width")){if(!QuadValues(*data,env,result.Widths))error("border-width");else result.HasBorder=true;}
    if(auto* data=find("border-color")){auto parts=Split(*data," ");if(parts.size()==1){if(auto color=Color(*data))result.BorderColors.fill(*color);}else if(parts.size()<=4){Vec4 colors[4];bool valid=true;for(std::size_t i=0;i<parts.size();++i){auto c=Color(parts[i]);if(c)colors[i]=*c;else valid=false;}if(valid&&!parts.empty()){result.BorderColors={parts.size()>3?colors[3]:parts.size()>1?colors[1]:colors[0],colors[0],parts.size()>1?colors[1]:colors[0],parts.size()>2?colors[2]:colors[0]};}else error("border-color");}}
    float* widths[]={&result.Widths.X,&result.Widths.Y,&result.Widths.Z,&result.Widths.W};const char* sides[]={"left","top","right","bottom"};
    for(int i=0;i<4;++i){if(auto* data=find(std::string("border-")+sides[i]+"-width")){if(auto n=Length(*data,env)){*widths[i]=*n;result.HasBorder=true;}else error("border-width");}if(auto* data=find(std::string("border-")+sides[i]+"-color")){if(auto c=Color(*data))result.BorderColors[i]=*c;else error("border-color");}}
    if(auto* data=find("border-style")){auto key=Lower(data->ToString());if(key=="none"){result.Widths={};result.HasBorder=true;result.BorderNone=true;}else if(key!="solid")error("border-style");}
    if(auto* data=find("border-radius")){auto axes=Split(*data,"/");auto x=env,y=env;x.PercentageBase=size.X;y.PercentageBase=size.Y;if(axes.empty()||axes.size()>2||!QuadValues(axes[0],x,result.RadiusX,true)||!QuadValues(axes.size()>1?axes[1]:axes[0],y,result.RadiusY,true))error("border-radius");else result.HasRadius=true;}
    float* rx[]={&result.RadiusX.X,&result.RadiusX.Y,&result.RadiusX.Z,&result.RadiusX.W};float* ry[]={&result.RadiusY.X,&result.RadiusY.Y,&result.RadiusY.Z,&result.RadiusY.W};const char* corners[]={"top-left","top-right","bottom-right","bottom-left"};
    for(int i=0;i<4;++i)if(auto* data=find(std::string("border-")+corners[i]+"-radius")){auto parts=Split(*data," ");auto x=env,y=env;x.PercentageBase=size.X;y.PercentageBase=size.Y;if(parts.empty()||parts.size()>2){error("border-radius");continue;}auto a=Length(parts[0],x),b=Length(parts.size()>1?parts[1]:parts[0],y);if(a&&b){*rx[i]=*a;*ry[i]=*b;result.HasRadius=true;}else error("border-radius");}
    // CSS corner-overlap reduction scales all elliptical radii by one factor.
    const float factor=std::min({1.f,size.X/std::max(1.f,result.RadiusX.X+result.RadiusX.Y),size.X/std::max(1.f,result.RadiusX.W+result.RadiusX.Z),size.Y/std::max(1.f,result.RadiusY.X+result.RadiusY.W),size.Y/std::max(1.f,result.RadiusY.Y+result.RadiusY.Z)});result.RadiusX=result.RadiusX*factor;result.RadiusY=result.RadiusY*factor;
    const CSS::Value* background=style.Properties.contains("background-image")?find("background-image"):find("background");if(background&&Lower(background->ToString())!="none"){for(const auto& layer:Split(*background,",")){if(CSS::ResolveReference(layer)||Color(layer))continue;GuiGradient gradient;if(result.Gradients.size()>=4||!Gradient(layer,env,size,gradient))error("background-image");else result.Gradients.push_back(gradient);}}
    if(auto* data=find("filter");data&&!Filters(*data,env,result.Filters)){result.Filters.clear();error("filter");}if(auto* data=find("backdrop-filter");data&&!Filters(*data,env,result.Backdrop)){result.Backdrop.clear();error("backdrop-filter");}
    return result;
}
}
