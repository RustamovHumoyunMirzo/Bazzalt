#include "GUI/GuiSerialization.h"
#include "Bazzalt/GUI.h"
#include "Bazzalt/Components/CameraRenderTarget.h"
#include "Bazzalt/Serialization.h"
#include "GUI/CSS/CSS.h"
#include <charconv>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string_view>

namespace Bazzalt::Runtime {
namespace {
template<class F> void Fields(Frame& v,F f){f("Mode",v.Mode);f("ScaleMode",v.ScaleMode);f("ReferenceSize",v.ReferenceSize);f("MatchWidthOrHeight",v.MatchWidthOrHeight);f("PixelsPerUnit",v.PixelsPerUnit);f("Camera",v.Camera);f("ZIndex",v.ZIndex);f("Visible",v.Visible);f("Interactable",v.Interactable);f("Stylesheet",v.Stylesheet);}
template<class F> void Fields(GuiElement& v,F f){f("Stylesheet",v.Stylesheet);f("ID",v.ID);f("Classes",v.Classes);f("Tags",v.Tags);f("Attributes",v.Attributes);f("MaterialAsset",v.MaterialAsset);f("MaterialIncludesChildren",v.MaterialIncludesChildren);f("StyleProperties",v.StyleProperties);}
template<class F> void Fields(RectTransform& v,F f){f("AnchorMin",v.AnchorMin);f("AnchorMax",v.AnchorMax);f("Pivot",v.Pivot);f("Position",v.Position);f("Size",v.Size);f("MinSize",v.MinSize);f("MaxSize",v.MaxSize);f("ZIndex",v.ZIndex);f("Visible",v.Visible);f("ClipChildren",v.ClipChildren);f("Layout",v.Layout);f("Padding",v.Padding);f("Margin",v.Margin);f("Gap",v.Gap);f("FlexGrow",v.FlexGrow);f("FlexShrink",v.FlexShrink);f("Align",v.Align);f("Justify",v.Justify);f("IgnoreLayout",v.IgnoreLayout);}
template<class F> void Fields(Rectangle& v,F f){f("Color",v.Style.Color);f("HoverColor",v.Style.HoverColor);f("PressedColor",v.Style.PressedColor);f("DisabledColor",v.Style.DisabledColor);f("BorderColor",v.Style.BorderColor);f("BorderWidth",v.Style.BorderWidth);f("Opacity",v.Style.Opacity);f("RaycastTarget",v.RaycastTarget);f("InheritColor",v.Style.InheritColor);f("InheritHoverColor",v.Style.InheritHoverColor);f("InheritPressedColor",v.Style.InheritPressedColor);f("InheritDisabledColor",v.Style.InheritDisabledColor);f("InheritBorderColor",v.Style.InheritBorderColor);f("InheritBorderWidth",v.Style.InheritBorderWidth);f("InheritOpacity",v.Style.InheritOpacity);}
template<class F> void Fields(GuiImage& v,F f){f("Texture",v.Texture);f("Camera",v.Camera);f("Color",v.Color);f("RaycastTarget",v.RaycastTarget);f("InheritColor",v.InheritColor);}
template<class F> void Fields(GuiText& v,F f){f("HorizontalAlignment",v.HorizontalAlignment);f("VerticalAlignment",v.VerticalAlignment);f("Value",v.Value);f("Color",v.Color);f("FontSize",v.FontSize);f("Wrap",v.Wrap);f("InheritColor",v.InheritColor);f("InheritFontSize",v.InheritFontSize);f("InheritWrap",v.InheritWrap);f("Font",v.Font);f("FontFace",v.FontFace);f("LetterSpacing",v.LetterSpacing);f("LineSpacing",v.LineSpacing);f("Kerning",v.Kerning);f("InheritFont",v.InheritFont);}
template<class F> void Fields(GuiButton& v,F f){f("Interactable",v.Interactable);f("TabIndex",v.TabIndex);}
template<class F> void Fields(GuiTextInput& v,F f){f("HorizontalAlignment",v.HorizontalAlignment);f("VerticalAlignment",v.VerticalAlignment);f("Value",v.Value);f("Placeholder",v.Placeholder);f("MaxLength",v.MaxLength);f("ReadOnly",v.ReadOnly);f("Password",v.Password);f("Interactable",v.Interactable);f("TabIndex",v.TabIndex);f("Font",v.Font);f("FontFace",v.FontFace);f("FontSize",v.FontSize);}
template<class F> void Fields(CameraRenderTarget& v,F f){f("Width",v.Width);f("Height",v.Height);}
template<class T> std::string Write(const T& value){
    if constexpr(std::is_same_v<T,std::string>)return value;
    else if constexpr(std::is_same_v<T,std::map<std::string,std::string>>){std::string result="{";for(const auto& [key,text]:value){if(result.size()>1)result+=",";CSS::Value name{{CSS::Token{CSS::TokenKind::String,key}}},data{{CSS::Token{CSS::TokenKind::String,text}}};result+=name.ToString()+":"+data.ToString();}return result+"}";}
    else if constexpr(std::is_same_v<T,UUID>)return value.ToString();
    else if constexpr(std::is_same_v<T,bool>)return value?"true":"false";
    else if constexpr(std::is_enum_v<T>)return std::to_string(static_cast<int>(value));
    else {std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(9);if constexpr(std::is_same_v<T,Vec2>)out<<value.X<<','<<value.Y;else if constexpr(std::is_same_v<T,Vec4>)out<<value.X<<','<<value.Y<<','<<value.Z<<','<<value.W;else out<<value;return out.str();}
}
template<class T> bool Read(const std::string& text,T& value){
    if constexpr(std::is_same_v<T,std::string>){if(text.size()>256*1024)return false;value=text;return true;}
    else if constexpr(std::is_same_v<T,std::map<std::string,std::string>>){if(text.size()>65536)return false;auto parsed=CSS::ParseValue(text);if(parsed.HasErrors())return false;std::vector<CSS::Token> tokens;for(const auto& t:parsed.Data.Tokens)if(t.Kind!=CSS::TokenKind::Space)tokens.push_back(t);std::size_t at=0;const auto take=[&](std::string_view text){if(at>=tokens.size()||tokens[at].Kind!=CSS::TokenKind::Symbol||tokens[at].Text!=text)return false;++at;return true;};if(!take("{"))return false;if(take("}"))return at==tokens.size();while(at<tokens.size()){if(value.size()>=128||tokens[at].Kind!=CSS::TokenKind::String)return false;auto key=tokens[at++].Text;if(key.empty()||key.size()>128||!take(":")||at>=tokens.size()||tokens[at].Kind!=CSS::TokenKind::String)return false;auto literal=tokens[at++].Text;if(literal.size()>8192||!value.emplace(key,literal).second)return false;if(take("}"))return at==tokens.size();if(!take(","))return false;}return false;}
    else if constexpr(std::is_same_v<T,UUID>)return UUID::TryParse(text,value);
    else if constexpr(std::is_same_v<T,bool>){if(text=="true"){value=true;return true;}if(text=="false"){value=false;return true;}return false;}
    else if constexpr(std::is_enum_v<T>){int number;auto result=std::from_chars(text.data(),text.data()+text.size(),number);constexpr int maximum=std::is_same_v<T,GuiAlignment>||std::is_same_v<T,GuiJustify>?3:std::is_same_v<T,FrameMode>||std::is_same_v<T,GuiLayoutMode>||std::is_same_v<T,GuiTextAlignment>?2:1;if(result.ec!=std::errc{}||result.ptr!=text.data()+text.size()||number<0||number>maximum)return false;value=static_cast<T>(number);return true;}
    else if constexpr(std::is_same_v<T,Vec2>||std::is_same_v<T,Vec4>){std::istringstream in(text);in.imbue(std::locale::classic());char separator;float numbers[4];constexpr int count=std::is_same_v<T,Vec2>?2:4;for(int i=0;i<count;++i){if(i&&(!(in>>separator)||separator!=','))return false;if(!(in>>numbers[i])||!std::isfinite(numbers[i]))return false;}in>>std::ws;if(!in.eof())return false;if constexpr(count==2)value={numbers[0],numbers[1]};else value={numbers[0],numbers[1],numbers[2],numbers[3]};return true;}
    else {auto result=std::from_chars(text.data(),text.data()+text.size(),value);if(result.ec!=std::errc{}||result.ptr!=text.data()+text.size())return false;if constexpr(std::is_floating_point_v<T>)return std::isfinite(value);else return true;}
}
template<class T> bool Valid(const T& v){
    if constexpr(std::is_same_v<T,Frame>)return v.ReferenceSize.X>0&&v.ReferenceSize.Y>0&&v.PixelsPerUnit>0&&v.MatchWidthOrHeight>=0&&v.MatchWidthOrHeight<=1;
    else if constexpr(std::is_same_v<T,RectTransform>)return v.MinSize.X>=0&&v.MinSize.Y>=0&&v.MaxSize.X>=v.MinSize.X&&v.MaxSize.Y>=v.MinSize.Y&&v.AnchorMax.X>=v.AnchorMin.X&&v.AnchorMax.Y>=v.AnchorMin.Y&&v.Padding.X>=0&&v.Padding.Y>=0&&v.Padding.Z>=0&&v.Padding.W>=0&&v.Margin.X>=0&&v.Margin.Y>=0&&v.Margin.Z>=0&&v.Margin.W>=0&&v.Gap>=0&&v.FlexGrow>=0&&v.FlexShrink>=0;
    else if constexpr(std::is_same_v<T,Rectangle>)return v.Style.BorderWidth>=0&&v.Style.Opacity>=0&&v.Style.Opacity<=1;
    else if constexpr(std::is_same_v<T,GuiElement>){if(v.ID.size()>256||v.Classes.size()>4096||v.Tags.size()>4096||v.Attributes.size()>8192||v.ID.find('\0')!=std::string::npos)return false;std::vector<CSS::Diagnostic> errors;auto selectors=CSS::ParseSelectors("*"+v.Attributes,errors);if(!errors.empty()||selectors.size()!=1||selectors[0].Parts.size()!=1)return false;for(const auto& test:selectors[0].Parts[0].Tests)if(test.Type!=CSS::Test::Kind::Universal&&(test.Type!=CSS::Test::Kind::Attribute||(!test.Operator.empty()&&test.Operator!="=")))return false;return true;}
    else if constexpr(std::is_same_v<T,GuiText>)return v.FontSize>0&&v.FontSize<=512&&v.FontFace<=255&&std::fabs(v.LetterSpacing)<=1000&&v.LineSpacing>=.1f&&v.LineSpacing<=10;
    else if constexpr(std::is_same_v<T,GuiTextInput>)return v.MaxLength<=65536&&v.FontFace<=255&&v.FontSize>=1&&v.FontSize<=512;
    else if constexpr(std::is_same_v<T,CameraRenderTarget>)return v.Width>0&&v.Height>0&&v.Width<=4096&&v.Height<=4096;
    else return true;
}
template<class T> void Register(ComponentSerializationRegistry& registry,const char* name){
    constexpr std::uint32_t current=std::is_same_v<T,GuiText>?4:std::is_same_v<T,GuiTextInput>?3:std::is_same_v<T,GuiElement>?3:std::is_same_v<T,Frame>||std::is_same_v<T,GuiTextInput>?2:std::is_same_v<T,RectTransform>||std::is_same_v<T,Rectangle>||std::is_same_v<T,GuiImage>||std::is_same_v<T,GuiText>?2:1;
    registry.Register<T>(name,current,[](const T& source,PropertyMap& out){T copy=source;Fields(copy,[&](const char* key,auto& value){out[key]=Write(value);});},
        [](T& value,const PropertyMap& input,std::uint32_t version){
            if(version<1||version>current)return false;
            T candidate{};bool valid=true;
            Fields(candidate,[&](const char* key,auto& field){
                auto found=input.find(key);
                if(found==input.end()){
                    const std::string_view name=key;
                    const bool added=name.starts_with("Inherit")||(std::is_same_v<T,RectTransform>&&(name=="Layout"||name=="Padding"||name=="Margin"||name=="Gap"||name=="FlexGrow"||name=="FlexShrink"||name=="Align"||name=="Justify"||name=="IgnoreLayout"));
                    const bool fonts=std::is_same_v<T,GuiText>&&version<3&&(name=="Font"||name=="FontFace"||name=="LetterSpacing"||name=="LineSpacing"||name=="Kerning"||name=="InheritFont");
                    const bool older=version<2&&((std::is_same_v<T,Frame>&&name=="Stylesheet")||(std::is_same_v<T,GuiTextInput>&&(name=="Font"||name=="FontFace"||name=="FontSize")));
                    const bool alignment=(std::is_same_v<T,GuiText>||std::is_same_v<T,GuiTextInput>)&&(name=="HorizontalAlignment"||name=="VerticalAlignment")&&version<current;const bool materials=std::is_same_v<T,GuiElement>&&version<2&&(name=="MaterialAsset"||name=="MaterialIncludesChildren");
                    const bool properties=std::is_same_v<T,GuiElement>&&version<3&&name=="StyleProperties";
                    if(!(version==1&&added)&&!fonts&&!older&&!alignment&&!materials&&!properties)valid=false;
                }else if(!Read(found->second,field))valid=false;
            });
            if(!valid||!Valid(candidate))return false;
            value=std::move(candidate);return true;
        });
}
}
void ModuleRegisterGuiComponents(ComponentSerializationRegistry& registry){Register<Frame>(registry,"Bazzalt.Frame");Register<GuiElement>(registry,"Bazzalt.GuiElement");Register<RectTransform>(registry,"Bazzalt.RectTransform");Register<Rectangle>(registry,"Bazzalt.Rectangle");Register<GuiImage>(registry,"Bazzalt.GuiImage");Register<GuiText>(registry,"Bazzalt.GuiText");Register<GuiButton>(registry,"Bazzalt.GuiButton");Register<GuiTextInput>(registry,"Bazzalt.GuiTextInput");Register<CameraRenderTarget>(registry,"Bazzalt.CameraRenderTarget");}
}
