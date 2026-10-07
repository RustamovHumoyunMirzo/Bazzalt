#include "Script/LuaBindings.h"
#include "Bazzalt/GUI.h"
#include "Bazzalt/Components/CameraRenderTarget.h"
namespace Bazzalt::Runtime {
namespace {
template<class T> void Operations(sol::table api,const char* name){
    sol::table operations=api["_ComponentOperations"];auto value=operations.create_named(name);
    value["Has"]=[](Entity e){return e.HasComponent<T>();};value["Get"]=[](Entity e){return T(e.GetComponent<T>());};
    value["Add"]=[](Entity e){return T(e.AddComponent<T>());};value["Set"]=[](Entity e,T v){e.GetComponent<T>()=std::move(v);};
    value["Remove"]=[](Entity e){e.RemoveComponent<T>();};value["SetEnabled"]=[](Entity e,bool v){e.SetComponentEnabled<T>(v);};value["IsEnabled"]=[](Entity e){return e.IsComponentEnabled<T>();};
    value["Inherited"]=[](Entity e)->std::optional<T>{const auto* v=e.GetScene()?e.GetScene()->TryGetInheritedComponent<T>(e):nullptr;return v?std::optional<T>(*v):std::nullopt;};
}
template<class T> auto Type(sol::table api,const char* name){Operations<T>(api,name);return api.new_usertype<T>(name,sol::constructors<T()>(),sol::base_classes,sol::bases<Component>());}
}
void BindLuaGUI(sol::table api){
    api.new_enum("FrameMode","Viewport",FrameMode::Viewport,"CameraBound",FrameMode::CameraBound,"Spatial",FrameMode::Spatial);
    api.new_enum("GuiScaleMode","ConstantPixels",GuiScaleMode::ConstantPixels,"ScaleWithViewport",GuiScaleMode::ScaleWithViewport);
    {auto t=Type<Frame>(api,"Frame");t["Mode"]=&Frame::Mode;t["ScaleMode"]=&Frame::ScaleMode;t["ReferenceSize"]=&Frame::ReferenceSize;t["MatchWidthOrHeight"]=&Frame::MatchWidthOrHeight;t["PixelsPerUnit"]=&Frame::PixelsPerUnit;t["Camera"]=&Frame::Camera;t["ZIndex"]=&Frame::ZIndex;t["Visible"]=&Frame::Visible;t["Interactable"]=&Frame::Interactable;}
    {auto t=Type<RectTransform>(api,"RectTransform");t["AnchorMin"]=&RectTransform::AnchorMin;t["AnchorMax"]=&RectTransform::AnchorMax;t["Pivot"]=&RectTransform::Pivot;t["Position"]=&RectTransform::Position;t["Size"]=&RectTransform::Size;t["MinSize"]=&RectTransform::MinSize;t["MaxSize"]=&RectTransform::MaxSize;t["ZIndex"]=&RectTransform::ZIndex;t["Visible"]=&RectTransform::Visible;t["ClipChildren"]=&RectTransform::ClipChildren;}
    {auto t=api.new_usertype<GuiStyle>("GuiStyle",sol::constructors<GuiStyle()>());t["Color"]=&GuiStyle::Color;t["HoverColor"]=&GuiStyle::HoverColor;t["PressedColor"]=&GuiStyle::PressedColor;t["DisabledColor"]=&GuiStyle::DisabledColor;t["BorderColor"]=&GuiStyle::BorderColor;t["BorderWidth"]=&GuiStyle::BorderWidth;t["Opacity"]=&GuiStyle::Opacity;}
    {auto t=Type<Rectangle>(api,"Rectangle");t["Style"]=&Rectangle::Style;t["RaycastTarget"]=&Rectangle::RaycastTarget;}
    {auto t=Type<GuiImage>(api,"GuiImage");t["Texture"]=&GuiImage::Texture;t["Camera"]=&GuiImage::Camera;t["Color"]=&GuiImage::Color;t["RaycastTarget"]=&GuiImage::RaycastTarget;}
    {auto t=Type<GuiText>(api,"GuiText");t["Value"]=&GuiText::Value;t["Color"]=&GuiText::Color;t["FontSize"]=&GuiText::FontSize;t["Wrap"]=&GuiText::Wrap;}
    {auto t=Type<GuiButton>(api,"GuiButton");t["Interactable"]=&GuiButton::Interactable;t["TabIndex"]=&GuiButton::TabIndex;}
    {auto t=Type<GuiTextInput>(api,"GuiTextInput");t["Value"]=&GuiTextInput::Value;t["Placeholder"]=&GuiTextInput::Placeholder;t["MaxLength"]=&GuiTextInput::MaxLength;t["ReadOnly"]=&GuiTextInput::ReadOnly;t["Password"]=&GuiTextInput::Password;t["Interactable"]=&GuiTextInput::Interactable;t["TabIndex"]=&GuiTextInput::TabIndex;}
    {auto t=Type<CameraRenderTarget>(api,"CameraRenderTarget");t["Width"]=&CameraRenderTarget::Width;t["Height"]=&CameraRenderTarget::Height;}
    {auto t=api.new_usertype<GuiInteraction>("GuiInteraction",sol::constructors<GuiInteraction()>());t["Hovered"]=&GuiInteraction::Hovered;t["Pressed"]=&GuiInteraction::Pressed;t["Focused"]=&GuiInteraction::Focused;t["PointerEntered"]=&GuiInteraction::PointerEntered;t["PointerExited"]=&GuiInteraction::PointerExited;t["Clicked"]=&GuiInteraction::Clicked;t["ValueChanged"]=&GuiInteraction::ValueChanged;t["Submitted"]=&GuiInteraction::Submitted;}
    {auto t=api.new_usertype<GuiBounds>("GuiBounds",sol::constructors<GuiBounds()>());t["Position"]=&GuiBounds::Position;t["Size"]=&GuiBounds::Size;t["Contains"]=&GuiBounds::Contains;}
    auto gui=api.create_named("GUI");gui["GetInteraction"]=&GUI::GetInteraction;gui["GetBounds"]=&GUI::GetBounds;gui["GetFrame"]=&GUI::GetFrame;gui["Focus"]=&GUI::Focus;gui["ClearFocus"]=&GUI::ClearFocus;gui["IsPointerOverUI"]=&GUI::IsPointerOverUI;
}
}
