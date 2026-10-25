#pragma once
#include <cstddef>
#include <string>
#include "Bazzalt/Component.h"
#include "Bazzalt/Entity.h"
#include "Bazzalt/RenderTexture.h"
#include "Bazzalt/Font.h"
#include "Bazzalt/Material.h"

namespace Bazzalt {
enum class FrameMode { Viewport, CameraBound, Spatial };
enum class GuiScaleMode { ConstantPixels, ScaleWithViewport };
enum class GuiLayoutMode { Absolute, Row, Column };
enum class GuiAlignment { Start, Center, End, Stretch };
enum class GuiJustify { Start, Center, End, SpaceBetween };
enum class GuiTextAlignment { Start, Center, End };

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
    // Legacy scene compatibility. Assign new styles to GuiElement::Stylesheet.
    UUID Stylesheet{};
};
// Common GUI selector metadata and an optional stylesheet scoped to this subtree.
struct GuiElement : Component {
    UUID Stylesheet{};
    std::string ID,Classes,Tags,Attributes;
    UUID MaterialAsset{};
    bool MaterialIncludesChildren=false;
    void SetMaterial(Bazzalt::Material material){if(material.GetAssetUUID()&&!material.IsCanvasCompatible())throw std::invalid_argument("GUI requires a canvas-compatible material");MaterialAsset=material.GetAssetUUID();}
    [[nodiscard]] Bazzalt::Material GetMaterial()const{return Bazzalt::Material::Load(MaterialAsset);}
};
// Top-left coordinates, anchor fractions relative to parent, pixel offsets.
struct RectTransform : Component {
    Vec2 AnchorMin{},AnchorMax{},Pivot{};
    Vec2 Position{},Size{160,40};
    Vec2 MinSize{},MaxSize{100000,100000};
    int ZIndex=0;
    bool Visible=true;
    bool ClipChildren=false;
    // Layout controls direct RectTransform children. Insets are left/top/right/bottom.
    GuiLayoutMode Layout=GuiLayoutMode::Absolute;
    Vec4 Padding{},Margin{};
    float Gap=0,FlexGrow=0,FlexShrink=1;
    GuiAlignment Align=GuiAlignment::Start;
    GuiJustify Justify=GuiJustify::Start;
    bool IgnoreLayout=false;
    void SetCentered(Vec2 size){AnchorMin=AnchorMax=Pivot={.5f,.5f};Position={};Size=size;}
    void SetStretch(Vec4 offsets={}){AnchorMin=Pivot={};AnchorMax={1,1};Position={offsets.X,offsets.Y};Size={-offsets.X-offsets.Z,-offsets.Y-offsets.W};}
};
struct GuiStyle {
    Vec4 Color{0.22f,0.24f,0.28f,1};
    Vec4 HoverColor{0.3f,0.34f,0.4f,1};
    Vec4 PressedColor{0.12f,0.22f,0.36f,1};
    Vec4 DisabledColor{0.16f,0.17f,0.19f,0.7f};
    Vec4 BorderColor{0.05f,0.06f,0.08f,1};
    float BorderWidth=0;
    float Opacity=1;
    // Explicit, per-property inheritance. Local authored values remain unchanged.
    bool InheritColor=false,InheritHoverColor=false,InheritPressedColor=false;
    bool InheritDisabledColor=false,InheritBorderColor=false;
    bool InheritBorderWidth=false,InheritOpacity=false;
};
struct Rectangle : Component { GuiStyle Style{}; bool RaycastTarget=true; };
struct GuiImage : Component {
    UUID Texture{},Camera{}; Vec4 Color{1,1,1,1}; bool RaycastTarget=false;
    bool InheritColor=false;
    void SetTexture(UUID asset) { Texture=asset;Camera={}; }
    void SetTexture(Bazzalt::Texture asset) { SetTexture(asset.GetAssetUUID()); }
    void SetRenderTexture(RenderTexture value) { Camera=value.GetCameraUUID();Texture={}; }
    [[nodiscard]] RenderTexture GetRenderTexture() const { return RenderTexture::FromCameraUUID(Camera); }
};
struct GuiText : Component {
    std::string Value; Vec4 Color{1,1,1,1}; float FontSize=16; bool Wrap=true;
    bool InheritColor=false,InheritFontSize=false,InheritWrap=false;
    UUID Font{};std::uint32_t FontFace=0;
    float LetterSpacing=0,LineSpacing=1;
    bool Kerning=true,InheritFont=false;
    GuiTextAlignment HorizontalAlignment=GuiTextAlignment::Start,VerticalAlignment=GuiTextAlignment::Start;
    void SetFont(Bazzalt::Font font){Font=font.GetAssetUUID();FontFace=font.GetFaceIndex();}
};
struct GuiButton : Component { bool Interactable=true; int TabIndex=0; };
struct GuiTextInput : Component {
    std::string Value,Placeholder;
    std::size_t MaxLength=256; // Unicode code points, not bytes.
    bool ReadOnly=false;
    bool Password=false;
    bool Interactable=true;
    int TabIndex=0;
    UUID Font{};std::uint32_t FontFace=0;
    float FontSize=16;
    GuiTextAlignment HorizontalAlignment=GuiTextAlignment::Start,VerticalAlignment=GuiTextAlignment::Center;
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
    // Return effective copies, following ancestors up to and including the nearest Frame.
    [[nodiscard]] static GuiStyle ResolveStyle(Entity entity);
    [[nodiscard]] static GuiText ResolveText(Entity entity);
    [[nodiscard]] static GuiImage ResolveImage(Entity entity);
    [[nodiscard]] static std::vector<std::string> GetStyleDiagnostics(Entity entity);
    static bool Focus(Entity entity);
    static bool SetParent(Entity entity,Entity parent,bool preservePosition=true);
    static void ClearFocus(Scene& scene);
    [[nodiscard]] static bool IsPointerOverUI(Scene& scene);
};
}
