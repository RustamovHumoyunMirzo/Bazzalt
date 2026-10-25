#include "GUI/GuiPaint.h"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace Bazzalt;
using namespace Bazzalt::Runtime;
static GuiPaint Paint(const std::string& text,std::vector<std::string>* errors=nullptr){auto sheet=CSS::Parse("Rectangle{"+text+"}");assert(!sheet.HasErrors());CSS::Node node;node.Type="Rectangle";return ResolveGuiPaint(CSS::Compute(sheet,node),{},Vec2{200,100},errors);}
int main(){
    auto border=Paint("border-width:1px 2px 3px 4px;border-radius:10px 20px 30px 40px / 5px 10px 15px 20px;border-right-width:7px");
    assert(border.Widths.X==4&&border.Widths.Y==1&&border.Widths.Z==7&&border.Widths.W==3);
    assert(border.RadiusX.X==10&&border.RadiusX.Y==20&&border.RadiusX.Z==30&&border.RadiusX.W==40&&border.RadiusY.W==20);
    auto cascade=Paint("border-left-width:9px;border-width:2px;border-top-left-radius:30px;border-radius:4px");assert(cascade.Widths.X==2&&cascade.RadiusX.X==4);
    auto variables=Paint("--r:3px 5px;border-radius:var(--r);border-width:1px!important;border-left-width:9px");assert(variables.RadiusX.X==3&&variables.RadiusX.Y==5&&variables.Widths.X==1);
    auto ellipse=Paint("border-radius:50%");assert(ellipse.RadiusX.X==100&&ellipse.RadiusY.X==50);
    assert(!GuiPaintContains(ellipse,{1,1},{200,100})&&GuiPaintContains(ellipse,{100,50},{200,100}));
    auto constrained=Paint("border-radius:100px");assert(constrained.RadiusY.X==50&&constrained.RadiusX.X==50);
    auto parent=Paint("border-radius:8px;border-width:3px;filter:invert(1)");CSS::Node child;child.Type="Rectangle";auto inheritedStyle=CSS::Compute(CSS::Parse("Rectangle{border-radius:inherit;border-width:inherit;filter:inherit}"),child);auto inherited=ResolveGuiPaint(inheritedStyle,{},Vec2{200,100},nullptr,&parent);assert(inherited.RadiusX.X==8&&inherited.Widths.X==3&&inherited.Filters.size()==1);
    for(const auto* type:{"linear-gradient","radial-gradient","conic-gradient","repeating-linear-gradient","repeating-radial-gradient","repeating-conic-gradient"}){
        auto gradient=Paint(std::string("background-image:")+type+"(red 0%, blue 100%)");assert(gradient.Gradients.size()==1&&gradient.Gradients[0].Count==2);assert(gradient.Gradients[0].Stops[0]==0&&gradient.Gradients[0].Stops[1]==1);
    }
    auto layers=Paint("background-image:linear-gradient(to right,red,blue),radial-gradient(circle at 20% 30%,white,transparent)");assert(layers.Gradients.size()==2&&layers.Gradients[1].Center.X==.2f);
    auto sized=Paint("background-image:radial-gradient(ellipse 40px 30px at center,red,blue)");assert(sized.Gradients.size()==1&&sized.Gradients[0].Radius.X==.2f&&sized.Gradients[0].Radius.Y==.3f);
    auto filters=Paint("filter:blur(15px) brightness(120%) contrast(2) grayscale(.5) invert(20%) opacity(.7) saturate(2) sepia(1);backdrop-filter:hue-rotate(90deg) drop-shadow(2px 4px 3px #123456)");
    assert(filters.Filters.size()==8&&filters.Filters[0].Kind==9&&filters.Filters[0].Amount==15);assert(filters.Backdrop.size()==2&&filters.Backdrop[1].Kind==10&&filters.Backdrop[1].Offset.Y==4);
    std::vector<std::string> errors;auto invalid=Paint("filter:blur(-1px);background-image:linear-gradient(red)",&errors);assert(!errors.empty()&&invalid.Filters.empty()&&invalid.Gradients.empty());
    CSS::Environment env;env.Width=800;env.Height=600;
    for(const auto* condition:{"(width >= 600px)","(400px < width <= 900px)","(min-aspect-ratio: 4/3)","(orientation:landscape)"})assert(CSS::EvaluateCondition({"viewport",CSS::ParseValue(condition).Data,{}},env));
    assert(!CSS::EvaluateCondition({"viewport",CSS::ParseValue("(width < 400px)").Data,{}},env));
    std::cout<<"CSS borders, gradients, filters and viewport conditions passed\n";
}
