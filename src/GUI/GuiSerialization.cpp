#include "GUI/GuiSerialization.h"
#include "Bazzalt/GUI.h"
#include "Bazzalt/Components/CameraRenderTarget.h"
#include "Bazzalt/Serialization.h"
#include <charconv>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace Bazzalt::Runtime {
namespace {
template<class F> void Fields(Frame& v,F f){f("Mode",v.Mode);f("ScaleMode",v.ScaleMode);f("ReferenceSize",v.ReferenceSize);f("MatchWidthOrHeight",v.MatchWidthOrHeight);f("PixelsPerUnit",v.PixelsPerUnit);f("Camera",v.Camera);f("ZIndex",v.ZIndex);f("Visible",v.Visible);f("Interactable",v.Interactable);}
template<class F> void Fields(RectTransform& v,F f){f("AnchorMin",v.AnchorMin);f("AnchorMax",v.AnchorMax);f("Pivot",v.Pivot);f("Position",v.Position);f("Size",v.Size);f("MinSize",v.MinSize);f("MaxSize",v.MaxSize);f("ZIndex",v.ZIndex);f("Visible",v.Visible);f("ClipChildren",v.ClipChildren);}
template<class F> void Fields(Rectangle& v,F f){f("Color",v.Style.Color);f("HoverColor",v.Style.HoverColor);f("PressedColor",v.Style.PressedColor);f("DisabledColor",v.Style.DisabledColor);f("BorderColor",v.Style.BorderColor);f("BorderWidth",v.Style.BorderWidth);f("Opacity",v.Style.Opacity);f("RaycastTarget",v.RaycastTarget);}
template<class F> void Fields(GuiImage& v,F f){f("Texture",v.Texture);f("Camera",v.Camera);f("Color",v.Color);f("RaycastTarget",v.RaycastTarget);}
template<class F> void Fields(GuiText& v,F f){f("Value",v.Value);f("Color",v.Color);f("FontSize",v.FontSize);f("Wrap",v.Wrap);}
template<class F> void Fields(GuiButton& v,F f){f("Interactable",v.Interactable);f("TabIndex",v.TabIndex);}
template<class F> void Fields(GuiTextInput& v,F f){f("Value",v.Value);f("Placeholder",v.Placeholder);f("MaxLength",v.MaxLength);f("ReadOnly",v.ReadOnly);f("Password",v.Password);f("Interactable",v.Interactable);f("TabIndex",v.TabIndex);}
template<class F> void Fields(CameraRenderTarget& v,F f){f("Width",v.Width);f("Height",v.Height);}
template<class T> std::string Write(const T& value){
    if constexpr(std::is_same_v<T,std::string>)return value;
    else if constexpr(std::is_same_v<T,UUID>)return value.ToString();
    else if constexpr(std::is_same_v<T,bool>)return value?"true":"false";
    else if constexpr(std::is_enum_v<T>)return std::to_string(static_cast<int>(value));
    else {std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(9);if constexpr(std::is_same_v<T,Vec2>)out<<value.X<<','<<value.Y;else if constexpr(std::is_same_v<T,Vec4>)out<<value.X<<','<<value.Y<<','<<value.Z<<','<<value.W;else out<<value;return out.str();}
}
template<class T> bool Read(const std::string& text,T& value){
    if constexpr(std::is_same_v<T,std::string>){if(text.size()>256*1024)return false;value=text;return true;}
    else if constexpr(std::is_same_v<T,UUID>)return UUID::TryParse(text,value);
    else if constexpr(std::is_same_v<T,bool>){if(text=="true"){value=true;return true;}if(text=="false"){value=false;return true;}return false;}
    else if constexpr(std::is_enum_v<T>){int number;auto result=std::from_chars(text.data(),text.data()+text.size(),number);if(result.ec!=std::errc{}||result.ptr!=text.data()+text.size()||number<0||number>(std::is_same_v<T,FrameMode>?2:1))return false;value=static_cast<T>(number);return true;}
    else if constexpr(std::is_same_v<T,Vec2>||std::is_same_v<T,Vec4>){std::istringstream in(text);in.imbue(std::locale::classic());char separator;float numbers[4];constexpr int count=std::is_same_v<T,Vec2>?2:4;for(int i=0;i<count;++i){if(i&&(!(in>>separator)||separator!=','))return false;if(!(in>>numbers[i])||!std::isfinite(numbers[i]))return false;}in>>std::ws;if(!in.eof())return false;if constexpr(count==2)value={numbers[0],numbers[1]};else value={numbers[0],numbers[1],numbers[2],numbers[3]};return true;}
    else {auto result=std::from_chars(text.data(),text.data()+text.size(),value);if(result.ec!=std::errc{}||result.ptr!=text.data()+text.size())return false;if constexpr(std::is_floating_point_v<T>)return std::isfinite(value);else return true;}
}
template<class T> bool Valid(const T& v){
    if constexpr(std::is_same_v<T,Frame>)return v.ReferenceSize.X>0&&v.ReferenceSize.Y>0&&v.PixelsPerUnit>0&&v.MatchWidthOrHeight>=0&&v.MatchWidthOrHeight<=1;
    else if constexpr(std::is_same_v<T,RectTransform>)return v.MinSize.X>=0&&v.MinSize.Y>=0&&v.MaxSize.X>=v.MinSize.X&&v.MaxSize.Y>=v.MinSize.Y&&v.AnchorMax.X>=v.AnchorMin.X&&v.AnchorMax.Y>=v.AnchorMin.Y;
    else if constexpr(std::is_same_v<T,Rectangle>)return v.Style.BorderWidth>=0&&v.Style.Opacity>=0&&v.Style.Opacity<=1;
    else if constexpr(std::is_same_v<T,GuiText>)return v.FontSize>0&&v.FontSize<=512;
    else if constexpr(std::is_same_v<T,GuiTextInput>)return v.MaxLength<=65536;
    else if constexpr(std::is_same_v<T,CameraRenderTarget>)return v.Width>0&&v.Height>0&&v.Width<=4096&&v.Height<=4096;
    else return true;
}
template<class T> void Register(ComponentSerializationRegistry& registry,const char* name){
    registry.Register<T>(name,1,[](const T& source,PropertyMap& out){T copy=source;Fields(copy,[&](const char* key,auto& value){out[key]=Write(value);});},
        [](T& value,const PropertyMap& input,std::uint32_t version){if(version!=1)return false;bool valid=true;Fields(value,[&](const char* key,auto& field){auto found=input.find(key);if(found==input.end()||!Read(found->second,field))valid=false;});return valid&&Valid(value);});
}
}
void ModuleRegisterGuiComponents(ComponentSerializationRegistry& registry){Register<Frame>(registry,"Bazzalt.Frame");Register<RectTransform>(registry,"Bazzalt.RectTransform");Register<Rectangle>(registry,"Bazzalt.Rectangle");Register<GuiImage>(registry,"Bazzalt.GuiImage");Register<GuiText>(registry,"Bazzalt.GuiText");Register<GuiButton>(registry,"Bazzalt.GuiButton");Register<GuiTextInput>(registry,"Bazzalt.GuiTextInput");Register<CameraRenderTarget>(registry,"Bazzalt.CameraRenderTarget");}
}
