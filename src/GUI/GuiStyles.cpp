#include "GUI/GuiSystem.h"
#include "Bazzalt/AssetManager.h"
#include "Bazzalt/Components/Name.h"
#include <fstream>
#include <sstream>
#include <map>
#include <functional>
#include <algorithm>
#include <cmath>
namespace Bazzalt::Runtime {
namespace {
std::vector<std::string> Words(const std::string& text){std::istringstream in(text);std::vector<std::string> result;for(std::string word;in>>word;)result.push_back(std::move(word));return result;}
std::string Type(Entity e){if(e.HasComponent<Frame>())return "Frame";if(e.HasComponent<GuiTextInput>())return "TextInput";if(e.HasComponent<GuiButton>())return "Button";if(e.HasComponent<GuiImage>())return "Image";if(e.HasComponent<GuiText>())return "Text";if(e.HasComponent<Rectangle>())return "Rectangle";return "Container";}
std::map<std::string,std::string> Attributes(const std::string& text){
    if(text.empty())return {};
    static thread_local std::map<std::string,std::map<std::string,std::string>> cache;
    if(auto found=cache.find(text);found!=cache.end())return found->second;
    std::vector<CSS::Diagnostic> diagnostics;auto selectors=CSS::ParseSelectors("*"+text,diagnostics);std::map<std::string,std::string> result;
    if(!diagnostics.empty()||selectors.size()!=1||selectors[0].Parts.size()!=1)return result;
    for(const auto& test:selectors[0].Parts[0].Tests)if(test.Type==CSS::Test::Kind::Attribute&&(test.Operator.empty()||test.Operator=="="))result[test.Name]=test.Argument;
    if(cache.size()>=128)cache.clear();cache[text]=result;return result;
}
struct CachedSheet {std::filesystem::path Path;std::filesystem::file_time_type Revision{};std::shared_ptr<CSS::Stylesheet> Sheet;};
std::shared_ptr<CSS::Stylesheet> ReadSheet(UUID id,std::vector<std::string>& errors){
    static std::map<UUID,CachedSheet> cache;
    auto info=AssetManager::GetAsset(id);if(!info||info->State!=AssetState::Ready||info->Importer!="Bazzalt.Stylesheet"){errors.push_back("Stylesheet asset is unavailable: "+id.ToString());return {};}
    std::error_code ec;auto revision=std::filesystem::last_write_time(info->CachePath,ec);if(ec){errors.push_back("Cannot read stylesheet cache");return {};}
    auto found=cache.find(id);if(found!=cache.end()&&found->second.Path==info->CachePath&&found->second.Revision==revision){for(const auto& diagnostic:found->second.Sheet->Diagnostics)if(errors.size()<64)errors.push_back("CSS "+std::to_string(diagnostic.Source.Line)+":"+std::to_string(diagnostic.Source.Column)+": "+diagnostic.Message);return found->second.Sheet;}
    std::ifstream in(info->CachePath,std::ios::binary|std::ios::ate);if(!in||in.tellg()<0||in.tellg()>1024*1024){errors.push_back("Stylesheet cache exceeds limits");return {};}
    std::string source(static_cast<std::size_t>(in.tellg()),'\0');in.seekg(0);in.read(source.data(),source.size());if(!in)return {};
    auto sheet=std::make_shared<CSS::Stylesheet>(CSS::Parse(source));
    for(const auto& diagnostic:sheet->Diagnostics)if(errors.size()<64)errors.push_back("CSS "+std::to_string(diagnostic.Source.Line)+":"+std::to_string(diagnostic.Source.Column)+": "+diagnostic.Message);
    if(cache.size()>=128)cache.clear();cache[id]={info->CachePath,revision,sheet};return sheet;
}
Vec4 Color(const CSS::Value& v,Vec4 fallback){auto color=CSS::ResolveColor(v,CSS::Color{fallback.X,fallback.Y,fallback.Z,fallback.W});return color?Vec4{float(color->R),float(color->G),float(color->B),float(color->A)}:fallback;}
std::optional<std::string> Keyword(const CSS::Value& data){std::optional<std::string> result;for(const auto& token:data.Tokens){if(token.Kind==CSS::TokenKind::Space)continue;if(result||token.Kind!=CSS::TokenKind::Ident)return {};result=token.Text;}if(result)for(auto& c:*result)if(c>='A'&&c<='Z')c=char(c-'A'+'a');return result;}
std::vector<CSS::Value> Parts(const CSS::Value& value){
    std::vector<CSS::Value> result(1);int depth=0;
    for(const auto& token:value.Tokens){if(token.Kind==CSS::TokenKind::Symbol&&token.Text=="(")++depth;if(token.Kind==CSS::TokenKind::Symbol&&token.Text==")")--depth;
        if(token.Kind==CSS::TokenKind::Space&&depth==0){if(!result.back().Tokens.empty())result.emplace_back();}else result.back().Tokens.push_back(token);}
    if(result.back().Tokens.empty())result.pop_back();return result;
}
void Insets(Vec4& out,const CSS::Value& data,CSS::Environment env){auto parts=Parts(data);if(parts.empty()||parts.size()>4)return;float v[4]{};for(std::size_t i=0;i<parts.size();++i){auto n=CSS::ResolveLength(parts[i],env);if(!n||*n<0||*n>100000)return;v[i]=float(*n);}float top=v[0],right=parts.size()>1?v[1]:top,bottom=parts.size()>2?v[2]:top,left=parts.size()>3?v[3]:right;out={left,top,right,bottom};}
UUID Reference(const CSS::Value& value){auto reference=CSS::ResolveReference(value);UUID id;if(reference){auto text=*reference;if(text.starts_with("asset://"))text.erase(0,8);if(!UUID::TryParse(text,id))return {};}return id;}
void Apply(GuiComputed& value,const GuiComputed* parent){
    auto& text=value.Text;auto& style=value.Style;const GuiComputed initial;
    for(const auto& [name,data]:value.Cascade.Properties){
        auto keyword=Keyword(data);const bool inherit=keyword&&(*keyword=="inherit"||(*keyword=="unset"&&(name=="color"||name=="font-family"||name=="font-size")));
        const auto& source=inherit&&parent?*parent:initial;
        if(keyword&&(*keyword=="inherit"||*keyword=="initial"||*keyword=="unset")){
            if(name=="color"){text.Color=source.Text.Color;value.Image.Color=source.Image.Color;}
            else if(name=="background-color"){style.Color=source.Style.Color;style.HoverColor=style.PressedColor=style.DisabledColor=style.Color;}
            else if(name=="font-size")text.FontSize=source.Text.FontSize;
            else if(name=="font-family"){text.Font=source.Text.Font;text.FontFace=source.Text.FontFace;}
            else if(name=="opacity")style.Opacity=source.Style.Opacity;
            else if(name=="border-color")style.BorderColor=source.Style.BorderColor;
            else if(name=="border-width")style.BorderWidth=source.Style.BorderWidth;
            else if(name=="letter-spacing")text.LetterSpacing=source.Text.LetterSpacing;
            else if(name=="line-height")text.LineSpacing=source.Text.LineSpacing;
            else if(name=="white-space")text.Wrap=source.Text.Wrap;
            else if(name=="text-align"){text.HorizontalAlignment=source.Text.HorizontalAlignment;value.Input.HorizontalAlignment=source.Input.HorizontalAlignment;}
            else if(name=="vertical-align"){text.VerticalAlignment=source.Text.VerticalAlignment;value.Input.VerticalAlignment=source.Input.VerticalAlignment;}
            else if(name=="pointer-events")value.PointerEvents=source.PointerEvents;
            else if(name=="gap")value.Rect.Gap=source.Rect.Gap;
            else if(name=="padding")value.Rect.Padding=source.Rect.Padding;
            else if(name=="margin")value.Rect.Margin=source.Rect.Margin;
            else if(name=="width"){value.Rect.Size.X=source.Rect.Size.X;value.Rect.AnchorMax.X=value.Rect.AnchorMin.X;}
            else if(name=="height"){value.Rect.Size.Y=source.Rect.Size.Y;value.Rect.AnchorMax.Y=value.Rect.AnchorMin.Y;}
            else if(name=="min-width")value.Rect.MinSize.X=source.Rect.MinSize.X;
            else if(name=="min-height")value.Rect.MinSize.Y=source.Rect.MinSize.Y;
            else if(name=="max-width")value.Rect.MaxSize.X=source.Rect.MaxSize.X;
            else if(name=="max-height")value.Rect.MaxSize.Y=source.Rect.MaxSize.Y;
            else if(name=="left")value.Rect.Position.X=source.Rect.Position.X;
            else if(name=="top")value.Rect.Position.Y=source.Rect.Position.Y;
            else if(name=="padding-left")value.Rect.Padding.X=source.Rect.Padding.X;
            else if(name=="padding-top")value.Rect.Padding.Y=source.Rect.Padding.Y;
            else if(name=="padding-right")value.Rect.Padding.Z=source.Rect.Padding.Z;
            else if(name=="padding-bottom")value.Rect.Padding.W=source.Rect.Padding.W;
            else if(name=="margin-left")value.Rect.Margin.X=source.Rect.Margin.X;
            else if(name=="margin-top")value.Rect.Margin.Y=source.Rect.Margin.Y;
            else if(name=="margin-right")value.Rect.Margin.Z=source.Rect.Margin.Z;
            else if(name=="margin-bottom")value.Rect.Margin.W=source.Rect.Margin.W;
            else if(name=="background-image")value.BackgroundTexture=source.BackgroundTexture;
            else if(name=="flex-grow")value.Rect.FlexGrow=source.Rect.FlexGrow;
            else if(name=="flex-shrink")value.Rect.FlexShrink=source.Rect.FlexShrink;
            else if(name=="flex-direction")value.Rect.Layout=source.Rect.Layout;
            else if(name=="align-items")value.Rect.Align=source.Rect.Align;
            else if(name=="justify-content")value.Rect.Justify=source.Rect.Justify;
            else if(name=="overflow")value.Rect.ClipChildren=source.Rect.ClipChildren;
            else if(name=="z-index")value.Rect.ZIndex=source.Rect.ZIndex;
            else if(name=="display"){value.Hidden=source.Hidden;value.Rect.Layout=source.Rect.Layout;}
            else if(name=="visibility")value.Hidden=value.Hidden||source.Hidden;
            continue;
        }
        if(name=="color"){text.Color=Color(data,text.Color);value.Image.Color=Color(data,value.Image.Color);}
        else if(name=="background-color"){style.Color=Color(data,style.Color);style.HoverColor=style.PressedColor=style.DisabledColor=style.Color;}
        else if(name=="border-color")style.BorderColor=Color(data,style.BorderColor);
        else if(name=="font-size"){auto env=value.Context;env.PercentageBase=parent?parent->Text.FontSize:16;if(auto n=CSS::ResolveLength(data,env);n&&*n>=1&&*n<=512)text.FontSize=float(*n);}
        else if(name=="font-family"){if(auto id=Reference(data);id){auto info=AssetManager::GetAsset(id);if(info&&info->State==AssetState::Ready&&info->Importer=="Bazzalt.Font")text.Font=id;}}
        else if(name=="background-image"||name=="background"){if(auto id=Reference(data);id)value.BackgroundTexture=id;else if(auto color=CSS::ResolveColor(data)){style.Color={float(color->R),float(color->G),float(color->B),float(color->A)};style.HoverColor=style.PressedColor=style.DisabledColor=style.Color;}}
        else if(name=="opacity"){if(auto n=CSS::ResolveLength(data);n&&*n>=0&&*n<=1)style.Opacity=float(*n);}
        else if(name=="border-width"){if(auto n=CSS::ResolveLength(data,value.Context);n&&*n>=0&&*n<=10000)style.BorderWidth=float(*n);}
        else if(name=="letter-spacing"){if(auto n=CSS::ResolveLength(data,value.Context);n&&std::fabs(*n)<=1000)text.LetterSpacing=float(*n);}
        else if(name=="line-height"){auto n=CSS::ResolveLength(data,value.Context);if(n&&*n>0&&*n<=512)text.LineSpacing=float(data.Tokens.size()==1&&data.Tokens[0].Kind==CSS::TokenKind::Number?*n:*n/text.FontSize);}
        else if(name=="white-space"&&keyword)text.Wrap=*keyword!="nowrap"&&*keyword!="pre";
        else if((name=="text-align"||name=="vertical-align")&&keyword){const auto a=*keyword=="center"||*keyword=="middle"?GuiTextAlignment::Center:*keyword=="right"||*keyword=="bottom"||*keyword=="end"?GuiTextAlignment::End:GuiTextAlignment::Start;if(name=="text-align"){text.HorizontalAlignment=a;value.Input.HorizontalAlignment=a;}else{text.VerticalAlignment=a;value.Input.VerticalAlignment=a;}}
        else if(name=="font-kerning"&&keyword)text.Kerning=*keyword!="none";
        else if(name=="display"&&keyword){value.Hidden=*keyword=="none";if(*keyword=="flex")value.Rect.Layout=GuiLayoutMode::Row;else if(*keyword=="block")value.Rect.Layout=GuiLayoutMode::Absolute;}
        else if(name=="visibility"&&keyword)value.Hidden=value.Hidden||*keyword=="hidden"||*keyword=="collapse";
        else if(name=="pointer-events"&&keyword)value.PointerEvents=*keyword!="none";
        else if(name=="flex-direction"&&keyword)value.Rect.Layout=*keyword=="column"?GuiLayoutMode::Column:GuiLayoutMode::Row;
        else if(name=="align-items"&&keyword)value.Rect.Align=*keyword=="center"?GuiAlignment::Center:*keyword=="end"||*keyword=="flex-end"?GuiAlignment::End:*keyword=="stretch"?GuiAlignment::Stretch:GuiAlignment::Start;
        else if(name=="justify-content"&&keyword)value.Rect.Justify=*keyword=="center"?GuiJustify::Center:*keyword=="end"||*keyword=="flex-end"?GuiJustify::End:*keyword=="space-between"?GuiJustify::SpaceBetween:GuiJustify::Start;
        else if(name=="overflow"&&keyword)value.Rect.ClipChildren=*keyword=="hidden"||*keyword=="clip";
        else if(name=="position"&&keyword)value.Rect.IgnoreLayout=*keyword=="absolute";
        else if(name=="z-index"){if(auto n=CSS::ResolveLength(data);n&&*n>=-100000&&*n<=100000)value.Rect.ZIndex=int(*n);}
    }
    value.Rect.Visible=value.Rect.Visible&&!value.Hidden;
    if(value.Cascade.Properties.contains("background-color"))style.InheritColor=style.InheritHoverColor=style.InheritPressedColor=style.InheritDisabledColor=false;
    if(value.Cascade.Properties.contains("border-color"))style.InheritBorderColor=false;
    if(value.Cascade.Properties.contains("border-width"))style.InheritBorderWidth=false;
    if(value.Cascade.Properties.contains("opacity"))style.InheritOpacity=false;
    if(value.Cascade.Properties.contains("color")){text.InheritColor=false;value.Image.InheritColor=false;}
    if(value.Cascade.Properties.contains("font-size"))text.InheritFontSize=false;
    if(value.Cascade.Properties.contains("font-family"))text.InheritFont=false;
    if(value.Cascade.Properties.contains("white-space"))text.InheritWrap=false;
    if(!value.PointerEvents){value.Button.Interactable=false;value.Input.Interactable=false;value.Image.RaycastTarget=false;}
    value.Input.Font=text.Font?text.Font:value.Input.Font;value.Input.FontFace=text.Font?text.FontFace:value.Input.FontFace;
    if(value.Cascade.Properties.contains("font-size"))value.Input.FontSize=text.FontSize;
}
}
void ApplyGuiLayout(GuiComputed& value,Vec2 parentSize){
    const auto number=[&](const std::string& name,float& field,bool vertical,bool negative=false){auto found=value.Cascade.Properties.find(name);if(found==value.Cascade.Properties.end())return;auto env=value.Context;env.PercentageBase=vertical?parentSize.Y:parentSize.X;auto n=CSS::ResolveLength(found->second,env);if(n&&std::fabs(*n)<=100000&&(negative||*n>=0))field=float(*n);};
    auto& r=value.Rect;number("width",r.Size.X,false);number("height",r.Size.Y,true);number("min-width",r.MinSize.X,false);number("min-height",r.MinSize.Y,true);number("max-width",r.MaxSize.X,false);number("max-height",r.MaxSize.Y,true);
    if(auto aspect=value.Cascade.Properties.find("aspect-ratio");aspect!=value.Cascade.Properties.end()){
        const auto& tokens=aspect->second.Tokens;auto slash=std::find_if(tokens.begin(),tokens.end(),[](const auto& t){return t.Text=="/";});std::optional<double> ratio;
        if(slash==tokens.end())ratio=CSS::ResolveLength(aspect->second);else {auto a=CSS::ResolveLength(CSS::Value{{tokens.begin(),slash}}),b=CSS::ResolveLength(CSS::Value{{slash+1,tokens.end()}});if(a&&b&&*b>0)ratio=*a / *b;}
        if(ratio&&*ratio>0&&*ratio<10000){if(value.Cascade.Properties.contains("width")&&!value.Cascade.Properties.contains("height"))r.Size.Y=float(r.Size.X / *ratio);else if(value.Cascade.Properties.contains("height")&&!value.Cascade.Properties.contains("width"))r.Size.X=float(r.Size.Y * *ratio);}
    }
    number("left",r.Position.X,false,true);number("top",r.Position.Y,true,true);number("gap",r.Gap,false);number("flex-grow",r.FlexGrow,false);number("flex-shrink",r.FlexShrink,false);
    if(value.Cascade.Properties.contains("right")&&!value.Cascade.Properties.contains("left")){float right=0;number("right",right,false,true);r.Position.X=parentSize.X-r.Size.X-right;r.AnchorMin.X=r.AnchorMax.X=r.Pivot.X=0;}
    if(value.Cascade.Properties.contains("bottom")&&!value.Cascade.Properties.contains("top")){float bottom=0;number("bottom",bottom,true,true);r.Position.Y=parentSize.Y-r.Size.Y-bottom;r.AnchorMin.Y=r.AnchorMax.Y=r.Pivot.Y=0;}
    auto env=value.Context;env.PercentageBase=parentSize.X;
    if(auto p=value.Cascade.Properties.find("padding");p!=value.Cascade.Properties.end())Insets(r.Padding,p->second,env);
    if(auto p=value.Cascade.Properties.find("margin");p!=value.Cascade.Properties.end())Insets(r.Margin,p->second,env);
    number("padding-left",r.Padding.X,false);number("padding-top",r.Padding.Y,false);number("padding-right",r.Padding.Z,false);number("padding-bottom",r.Padding.W,false);
    number("margin-left",r.Margin.X,false);number("margin-top",r.Margin.Y,false);number("margin-right",r.Margin.Z,false);number("margin-bottom",r.Margin.W,false);
    r.MaxSize.X=std::max(r.MinSize.X,r.MaxSize.X);r.MaxSize.Y=std::max(r.MinSize.Y,r.MaxSize.Y);
    if(value.Cascade.Properties.contains("width"))r.AnchorMax.X=r.AnchorMin.X;
    if(value.Cascade.Properties.contains("height"))r.AnchorMax.Y=r.AnchorMin.Y;
}
void GuiSystem::PrepareStyles(Entity root,GuiBounds surface,float scale){
    struct Scope {UUID Asset,Owner;std::shared_ptr<CSS::Stylesheet> Sheet;};
    std::map<UUID,CSS::Node> nodes;std::map<UUID,std::shared_ptr<CSS::Stylesheet>> loaded;std::size_t count=0;
    std::function<void(Entity,const CSS::Node*,const CSS::Node*,std::vector<Scope>,std::size_t,std::size_t,std::size_t,std::size_t,std::size_t)> visit;
    visit=[&](Entity e,const CSS::Node* parent,const CSS::Node* previous,std::vector<Scope> scopes,std::size_t index,std::size_t siblings,std::size_t typeIndex,std::size_t typeCount,std::size_t depth){
        if(depth>128||++count>4096){if(StyleDiagnostics.size()<64)StyleDiagnostics.push_back("GUI stylesheet node/depth limit exceeded");return;}
        const auto* metadata=e.TryGetComponent<GuiElement>();if(metadata&&!metadata->Enabled)metadata=nullptr;
        const auto add=[&](UUID asset){if(!asset||scopes.size()>=16)return;if(std::any_of(scopes.begin(),scopes.end(),[&](const auto& s){return s.Asset==asset&&s.Owner==e.GetUUID();}))return;
            if(!loaded.contains(asset))loaded[asset]=ReadSheet(asset,StyleDiagnostics);auto sheet=loaded[asset];if(sheet&&!sheet->HasErrors())scopes.push_back({asset,e.GetUUID(),sheet});};
        if(e==root&&(!metadata||!metadata->Stylesheet))add(root.GetComponent<Frame>().Stylesheet);if(metadata)add(metadata->Stylesheet);
        auto& node=nodes[e.GetUUID()];node.Type=Type(e);node.Parent=parent;node.PreviousSibling=previous;node.ChildIndex=index;node.ChildCount=siblings;node.TypeIndex=typeIndex;node.TypeCount=typeCount;
        if(metadata){node.ID=metadata->ID;for(const auto& word:Words(metadata->Classes))node.Classes.insert(word);node.Attributes=Attributes(metadata->Attributes);node.Attributes["tags"]=metadata->Tags;if(auto state=node.Attributes.find("state");state!=node.Attributes.end())node.States.insert(state->second);}
        if(node.ID.empty())node.ID=e.GetComponent<Name>().Value;
        node.Attributes["id"]=node.ID;node.Attributes["name"]=e.GetComponent<Name>().Value;node.Attributes["type"]=node.Type;
        if(metadata)node.Attributes["class"]=metadata->Classes;
        for(const auto* state:{"disabled","checked","selected"})if(node.Attributes.contains(state))node.States.insert(state);
        auto interaction=Interactions.find(e.GetUUID());if(interaction!=Interactions.end()){if(interaction->second.Hovered)node.States.insert("hover");if(interaction->second.Pressed)node.States.insert("active");if(interaction->second.Focused){node.States.insert("focus");node.States.insert("focus-visible");}}
        for(auto current=Focused?e.GetScene()->GetEntity(Focused):Entity{};current;current=current.GetParent()){if(current==e){node.States.insert("focus-within");break;}if(current==root)break;}
        const auto* button=e.TryGetComponent<GuiButton>();const auto* input=e.TryGetComponent<GuiTextInput>();
        if(!root.GetComponent<Frame>().Interactable||(button&&(!button->Enabled||!button->Interactable))||(input&&(!input->Enabled||!input->Interactable))){node.States.insert("disabled");node.Attributes["disabled"]="";}
        auto children=e.GetChildren();std::erase_if(children,[](Entity child){return child.HasComponent<Frame>();});if(children.empty())node.States.insert("empty");
        for(const auto& scope:scopes)node.States.insert("__scope_"+scope.Owner.ToString());
        std::size_t rules=0;for(const auto& scope:scopes)rules+=scope.Sheet->Rules.size();
        if(rules>10000){if(StyleDiagnostics.size()<64)StyleDiagnostics.push_back("GUI combined stylesheet rule limit exceeded");scopes.clear();}
        if(!scopes.empty()){
            CSS::Stylesheet combined;std::size_t order=0;
            for(const auto& scope:scopes){for(const auto& layer:scope.Sheet->Layers)if(std::find(combined.Layers.begin(),combined.Layers.end(),layer)==combined.Layers.end())combined.Layers.push_back(layer);
                for(auto rule:scope.Sheet->Rules){for(auto& selector:rule.Selectors){CSS::Test test;test.Type=CSS::Test::Kind::Pseudo;test.Name="state";test.Argument="__scope_"+scope.Owner.ToString();selector.Parts.back().Tests.push_back(std::move(test));}for(auto& declaration:rule.Declarations)declaration.Order=++order;combined.Rules.push_back(std::move(rule));}}
            GuiComputed value;if(auto* r=e.TryGetComponent<RectTransform>())value.Rect=*r;value.Style=GUI::ResolveStyle(e);value.Text=GUI::ResolveText(e);value.Image=GUI::ResolveImage(e);if(button)value.Button=*button;if(input)value.Input=*input;
            value.Context.Width=surface.Size.X/scale;value.Context.Height=surface.Size.Y/scale;value.Context.FontSize=value.Text.FontSize;
            CSS::Limits limits;limits.ResolveWork=100000;value.Cascade=CSS::Compute(combined,node,value.Context,limits);
            static const std::set<std::string> supported={"text-align","vertical-align","color","background-color","border-color","border-width","opacity","font-family","font-size","font-kerning","letter-spacing","line-height","white-space","display","visibility","pointer-events","flex-direction","align-items","justify-content","overflow","z-index","width","height","min-width","min-height","max-width","max-height","left","top","gap","flex-grow","flex-shrink","padding","margin","padding-left","padding-top","padding-right","padding-bottom","margin-left","margin-top","margin-right","margin-bottom","background-image"};
            for(const auto& [name,data]:value.Cascade.Properties)if(!supported.contains(name)&&!IsGuiPaintProperty(name)&&name!="position"&&name!="aspect-ratio"&&name!="right"&&name!="bottom"&&StyleDiagnostics.size()<64)StyleDiagnostics.push_back("Unsupported GUI CSS property: "+name);
            for(const auto& diagnostic:value.Cascade.Diagnostics)if(StyleDiagnostics.size()<64)StyleDiagnostics.push_back(diagnostic.Message);
            const GuiComputed* inherited=nullptr;if(parent){auto found=Computed.find(e.GetParent().GetUUID());if(found!=Computed.end())inherited=&found->second;}
            if(inherited)value.Context.FontSize=inherited->Text.FontSize;
            if(auto found=Computed.find(root.GetUUID());found!=Computed.end())value.Context.RootFontSize=found->second.Text.FontSize;
            Apply(value,inherited);if(node.Attributes.contains("disabled")){value.Button.Interactable=false;value.Input.Interactable=false;}
            value.Context.FontSize=value.Text.FontSize;if(e==root)value.Context.RootFontSize=value.Text.FontSize;
            ApplyGuiLayout(value,{float(value.Context.Width),float(value.Context.Height)});value.Paint=ResolveGuiPaint(value.Cascade,value.Context,e==root?Vec2{float(value.Context.Width),float(value.Context.Height)}:value.Rect.Size,&StyleDiagnostics,inherited?&inherited->Paint:nullptr);Computed[e.GetUUID()]=std::move(value);
        }
        std::map<std::string,std::size_t> totals,indices;for(auto child:children)++totals[Type(child)];const CSS::Node* prior=nullptr;
        for(std::size_t i=0;i<children.size();++i){auto child=children[i];auto type=Type(child);visit(child,&node,prior,scopes,i+1,children.size(),++indices[type],totals[type],depth+1);prior=&nodes[child.GetUUID()];}
    };
    visit(root,nullptr,nullptr,{},1,1,1,1,0);
}
}
