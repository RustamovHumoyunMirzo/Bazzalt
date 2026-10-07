#pragma once
#include <cstddef>
#include <string>
#include "Bazzalt/Component.h"
#include "Bazzalt/Entity.h"

namespace Bazzalt {
enum class FrameMode { Viewport, CameraBound, Spatial };
enum class GuiScaleMode { ConstantPixels, ScaleWithViewport };

struct GuiBounds {
    Vec2 Position{}, Size{};
    [[nodiscard]] bool Contains(Vec2 point) const {
        return Size.X>0 && Size.Y>0 && point.X>=Position.X && point.Y>=Position.Y &&
            point.X<Position.X+Size.X && point.Y<Position.Y+Size.Y;
    }
};
// Frame owns its descendants, never other widgets on the Frame entity itself.
struct Frame : Component {
    FrameMode Mode=FrameMode::Viewport;
    GuiScaleMode ScaleMode=GuiScaleMode::ScaleWithViewport;
    Vec2 ReferenceSize{1280,720};
    float MatchWidthOrHeight=0.5f;
    float PixelsPerUnit=100;
    UUID Camera{};
    int ZIndex=0;
    bool Visible=true;
    bool Interactable=true;
};
// Top-left coordinates, anchor fractions relative to parent, pixel offsets.
struct RectTransform : Component {
    Vec2 AnchorMin{},AnchorMax{},Pivot{};
    Vec2 Position{},Size{160,40};
    Vec2 MinSize{},MaxSize{100000,100000};
    int ZIndex=0;
    bool Visible=true;
    bool ClipChildren=false;
};
struct GuiStyle {
    Vec4 Color{0.22f,0.24f,0.28f,1};
    Vec4 HoverColor{0.3f,0.34f,0.4f,1};
    Vec4 PressedColor{0.12f,0.22f,0.36f,1};
    Vec4 DisabledColor{0.16f,0.17f,0.19f,0.7f};
    Vec4 BorderColor{0.05f,0.06f,0.08f,1};
    float BorderWidth=0;
    float Opacity=1;
};
struct Rectangle : Component { GuiStyle Style{}; bool RaycastTarget=true; };
struct GuiImage : Component { UUID Texture{},Camera{}; Vec4 Color{1,1,1,1}; bool RaycastTarget=false; };
struct GuiText : Component { std::string Value; Vec4 Color{1,1,1,1}; float FontSize=16; bool Wrap=true; };
struct GuiButton : Component { bool Interactable=true; int TabIndex=0; };
struct GuiTextInput : Component {
    std::string Value,Placeholder;
    std::size_t MaxLength=256; // Unicode code points, not bytes.
    bool ReadOnly=false;
    bool Password=false;
    bool Interactable=true;
    int TabIndex=0;
};
struct GuiInteraction {
    bool Hovered=false,Pressed=false,Focused=false;
    bool PointerEntered=false,PointerExited=false,Clicked=false;
    bool ValueChanged=false,Submitted=false;
};
// Read interaction pulses during gameplay Update. Runtime owns event dispatch.
class BAZZALT_API GUI final {
public:
    GUI()=delete;
    [[nodiscard]] static GuiInteraction GetInteraction(Entity entity);
    [[nodiscard]] static GuiBounds GetBounds(Entity entity);
    [[nodiscard]] static Entity GetFrame(Entity entity);
    static bool Focus(Entity entity);
    static void ClearFocus(Scene& scene);
    [[nodiscard]] static bool IsPointerOverUI(Scene& scene);
};
}
