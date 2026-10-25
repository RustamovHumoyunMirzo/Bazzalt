#pragma once
#include "GUI/CSS/CSS.h"
#include "Bazzalt/Math.h"
#include <array>
namespace Bazzalt::Runtime {
struct GuiGradient {
    int Kind=0,Count=0;bool Repeat=false;float Angle=180;
    Vec2 Center{.5f,.5f},Radius{.5f,.5f};
    std::array<Vec4,8> Colors{};std::array<float,8> Stops{};
};
struct GuiFilter {int Kind=0;float Amount=1;Vec2 Offset{};Vec4 Color{0,0,0,1};};
struct GuiPaint {
    Vec4 Widths{},RadiusX{},RadiusY{}; // widths L/T/R/B; radii TL/TR/BR/BL
    std::array<Vec4,4> BorderColors{};
    std::vector<GuiGradient> Gradients;
    std::vector<GuiFilter> Filters,Backdrop;
    bool HasBorder=false,HasRadius=false,BorderNone=false;
};
GuiPaint ResolveGuiPaint(const CSS::ComputedStyle&,CSS::Environment,Vec2 size,std::vector<std::string>* diagnostics=nullptr,const GuiPaint* parent=nullptr);
bool IsGuiPaintProperty(const std::string& name);
bool GuiPaintContains(const GuiPaint&,Vec2 point,Vec2 size);
}
