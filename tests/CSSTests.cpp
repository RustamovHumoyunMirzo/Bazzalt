#include "GUI/CSS/CSS.h"
#include <cmath>
#include <iostream>
#include <random>
#include <stdexcept>
using namespace Bazzalt::Runtime::CSS;
#define CHECK(...) do{if(!(__VA_ARGS__))throw std::runtime_error("CSS test line "+std::to_string(__LINE__)+": " #__VA_ARGS__);}while(false)
static bool Near(double a,double b){return std::fabs(a-b)<1e-7;}
static Value V(std::string_view source){auto parsed=ParseValue(source);CHECK(!parsed.HasErrors());return parsed.Data;}
static bool Match(std::string_view text,const Node& node){std::vector<Diagnostic> diagnostics;auto selectors=ParseSelectors(text,diagnostics);CHECK(diagnostics.empty());for(const auto& s:selectors)if(Matches(s,node))return true;return false;}
static std::string Property(const ComputedStyle& style,const std::string& name){auto found=style.Properties.find(name);return found==style.Properties.end()?"":found->second.ToString();}
static void Lexical(){
    std::vector<Diagnostic> d;auto tokens=Tokenize("/* note */ .\\31 23 { --accent: #0f08; size: -2.5e+2px; text: \"hi\\000021\"; }",d);
    CHECK(d.empty());bool identifier=false,number=false,string=false;
    for(const auto& t:tokens){identifier|=t.Kind==TokenKind::Ident&&t.Text=="123";number|=t.Kind==TokenKind::Dimension&&Near(t.Number,-250)&&t.Unit=="px";string|=t.Kind==TokenKind::String&&t.Text=="hi!";}
    CHECK(identifier&&number&&string);
    d.clear();(void)Tokenize("\"unterminated",d);CHECK(!d.empty());d.clear();(void)Tokenize("/*unterminated",d);CHECK(!d.empty());
    auto bad=ParseValue("calc(1px + 2px");CHECK(bad.HasErrors());
    CHECK(ParseValue("([)]").HasErrors());CHECK(ParseValue("1e999px").HasErrors());
    Limits limits;limits.Bytes=8;d.clear();(void)Tokenize("long input",d,limits);CHECK(!d.empty());
    limits=Limits{};limits.Tokens=3;d.clear();(void)Tokenize("a b c d",d,limits);CHECK(!d.empty());
    CHECK(ResolveString(V("\"line\\20 space\""))=="line space");
    CHECK(ResolveString(V(V("\"line\\a break\"").ToString()))=="line\nbreak");
    CHECK(V("foo/**/bar").ToString()=="foo bar");
    CHECK(ParseValue(std::string("\"bad\0value\"",11)).HasErrors());
    CHECK(ParseValue(std::string("\xc0\xaf",2)).HasErrors());
    CHECK(!ParseValue("\"Привет\"").HasErrors());
    CHECK(Parse("Button {\n color red; }").HasErrors());
    auto sheet=Parse("\nButton { color: red; }\n");CHECK(!sheet.HasErrors()&&sheet.Rules[0].Source.Line==2&&sheet.Rules[0].Declarations[0].Source.Line==2);
}
static void Selectors(){
    Node root;root.Type="Frame";root.Classes={"container"};
    Node first;first.Type="Button";first.Parent=&root;first.ChildIndex=1;first.ChildCount=2;first.TypeIndex=1;first.TypeCount=2;
    Node node=first;node.ID="play";node.Classes={"primary","wide"};node.Attributes={{"state","active"},{"disabled",""},{"lang","en-US"},{"tags","one two"},{"title","HelloWorld"}};
    node.States={"hover","disabled","focus","focus-within","empty"};node.ChildIndex=2;node.TypeIndex=2;node.PreviousSibling=&first;
    for(auto selector:{"*","Button","button#play.primary.wide",".container > Button","Frame Button","Button + Button","Button ~ Button","[disabled]","[state=active]","[lang|=en]","[tags~=two]","[title^=hello i]","[title$=World]","[title*=loW]",":hover:disabled:focus",":focus-within:empty",":last-child",":nth-child(2n)",":nth-child(-n + 2)",":nth-last-child(1)",":last-of-type",":nth-of-type(even)",":is(Label, #play)",":not(.missing)",":where(.primary)",":state(hover)"})CHECK(Match(selector,node));
    for(auto selector:{"Label","#other",".container + Button","Button > Frame","[state=other]","[title^=hello s]",":enabled",":first-child",":only-child",":nth-child(odd)",":nth-child(-n+1)",":not(#play)"})CHECK(!Match(selector,node));
    CHECK(Match(":root",root));
    std::vector<Diagnostic> d;auto s=ParseSelectors("#play:is(Button,.primary):where(#ignored)",d);CHECK(d.empty()&&s[0].Weight.IDs==1&&s[0].Weight.Classes==1&&s[0].Weight.Types==0);
    for(auto invalid:{"",",Button","Button,","Button >","[x!=y]","Button::before",":has(Button)",":unknown",":not(",".123","#123",":nth-child(foo)",":nth-child(2n1)",":nth-child(2 n)"}){d.clear();(void)ParseSelectors(invalid,d);CHECK(!d.empty());}
    Node numeric;numeric.ID="123";CHECK(Match("#\\31 23",numeric));
    Limits limits;limits.Nesting=2;d.clear();(void)ParseSelectors(":is(:is(:is(Button)))",d,limits);CHECK(!d.empty());
    Node cyclic;cyclic.PreviousSibling=&cyclic;CHECK(!Match(".absent ~ Button",cyclic));
}
static void Values(){
    Environment e;e.Width=800;e.Height=600;e.PercentageBase=200;e.FontSize=20;e.RootFontSize=16;
    CHECK(Near(*ResolveLength(V("calc(50% - 2em + 8px)"),e),68));
    CHECK(Near(*ResolveLength(V("clamp(20px, 10vw, 100px)"),e),80));
    CHECK(Near(*ResolveLength(V("max(2rem, min(5vh, 10px))"),e),32));
    CHECK(Near(*ResolveLength(V("calc(10px * 2 / 4)"),e),5));
    for(auto invalid:{"calc(1px + 1)","calc(1px * 2px)","calc(1 / 0)","12unknown","calc(1px 2px)","clamp(1px, 2px)"})CHECK(!ResolveLength(V(invalid),e));
    CHECK(ResolveBoolean(V("true"))==true&&ResolveBoolean(V("false"))==false&&!ResolveBoolean(V("on")));
    CHECK(ResolveReference(V("url(\"asset://123\")"))=="asset://123");CHECK(ResolveReference(V("asset(abc-123)"))=="abc-123");CHECK(!ResolveReference(V("system(\"bad\")")));
    CHECK(!ResolveReference(V("url(a b)"))&&!ResolveReference(V("url (x)")));
    CHECK(!ResolveColor(V("rgb (1 2 3)"))&&!ResolveLength(V("calc (1px + 2px)")));
    auto color=ResolveColor(V("#0f08"));CHECK(color&&color->G==1&&Near(color->A,136./255));
    color=ResolveColor(V("rgb(255 0 128 / 50%)"));CHECK(color&&color->R==1&&Near(color->B,128./255)&&color->A==.5);
    CHECK(ResolveColor(V("rgba(255, 0, 0, .25)"))->A==.25);
    CHECK(ResolveColor(V("hsl(120deg 100% 50%)"))->G==1);
    CHECK(ResolveColor(V("hsl(.5turn 100% 50% / .5)"))->B==1);
    CHECK(ResolveColor(V("hwb(0 0% 0%)"))->R==1);
    CHECK(ResolveColor(V("color(srgb 1 0 .5 / .25)"))->A==.25);
    CHECK(!ResolveColor(V("rgb(1 2,3)"))&&!ResolveColor(V("rgb(1 2 3 .5)")));
    CHECK(ResolveColor(V("rebeccapurple")));CHECK(ResolveColor(V("transparent"))->A==0);
    CHECK(!ResolveColor(V("currentColor")));CHECK(ResolveColor(V("currentColor"),Color{1,0,0,1})->R==1);
    CHECK(!ResolveColor(V("#gggg"))&&!ResolveColor(V("rgb(1 2)")));
    std::map<std::string,Value> vars={{"--x",V("10px")},{"--y",V("calc(var(--x) * 2)")}};std::vector<Diagnostic> d;
    auto expanded=SubstituteVariables(V("var(--y)"),vars,d);CHECK(expanded&&Near(*ResolveLength(*expanded),20));
    expanded=SubstituteVariables(V("var(--missing, rgb(1, 2, 3))"),vars,d);CHECK(expanded&&ResolveColor(*expanded));
    vars["--x"]=V("var(--y)");CHECK(!SubstituteVariables(V("var(--x)"),vars,d));
    expanded=SubstituteVariables(V("var(--x, 12px)"),vars,d);CHECK(expanded&&*ResolveLength(*expanded)==12);
    vars={{"--a",V("var(--b, var(--a))")},{"--b",V("3px")},{"--c",V("var(--a, 4px)")}};
    CHECK(!SubstituteVariables(V("var(--a)"),vars,d));expanded=SubstituteVariables(V("var(--c)"),vars,d);CHECK(expanded&&*ResolveLength(*expanded)==4);
    Limits limits;limits.ExpansionBytes=2;CHECK(!SubstituteVariables(V("var(--none, 100px)"),{},d,limits));
}
static void Cascade(){
    auto sheet=Parse(R"css(
      @layer defaults, theme;
      @layer defaults { Button { color: red; gap: 1px !important; } }
      @layer theme { .primary { color: green; gap: 2px !important; } }
      :root { --size: 10px; --accent: #acf; }
      Button { width: var(--size); color: blue; }
      #play { color: #123; opacity: .4; opacity: .6; gap:99px !important; }
      Button:hover { opacity: .8 !important; }
      @viewport (min-width: 600px) and (orientation: landscape) { .primary { width: calc(var(--size) * 2); } }
      @media screen and (prefers-color-scheme: dark) { #play { color: white; } }
      @font-face { font-family: Demo; src: url("asset://font"); }
      @supports (magic: yes) { #play { color: black !important; } }
      @import "https://example.invalid/no-network.css";
    )css");
    CHECK(!sheet.HasErrors()&&sheet.Rules.size()==8&&sheet.AtRules.size()==8);
    Node root;root.Type="Frame";Node node;node.Type="Button";node.ID="play";node.Classes={"primary"};node.States={"hover"};node.Parent=&root;
    auto style=Compute(sheet,node);CHECK(style.Diagnostics.empty());CHECK(Property(style,"color")=="white"&&Property(style,"gap")=="1px"&&Property(style,"opacity")==".8");
    CHECK(*ResolveLength(style.Properties.at("width"))==20);
    Environment narrow;narrow.Width=400;style=Compute(sheet,node,narrow);CHECK(*ResolveLength(style.Properties.at("width"))==10);
    narrow.ColorScheme="light";style=Compute(sheet,node,narrow);CHECK(Property(style,"color")=="#123");
    auto inherited=Parse("Frame { --a: 2px; --b: var(--a); } Button { --a: 9px; size:var(--b); }");
    style=Compute(inherited,node);CHECK(Property(style,"size")=="2px");
    sheet=Parse("#play { value: inherit; other: initial; unknown-engine-property: asset(abc); }");style=Compute(sheet,node);
    CHECK(Property(style,"value")=="inherit"&&Property(style,"other")=="initial"&&Property(style,"unknown-engine-property")=="asset(abc)");
    auto custom=Parse("Frame{--a:3px} Button{--a:inherit;size:var(--a)}");style=Compute(custom,node);CHECK(Property(style,"size")=="3px");
    custom=Parse("Frame{--a:3px} Button{--a:initial;size:var(--a,4px)}");style=Compute(custom,node);CHECK(Property(style,"size")=="4px");
    custom=Parse("Button{--a:\"initial\";text:var(--a)}");style=Compute(custom,node);CHECK(ResolveString(style.Properties.at("text"))=="initial");
    CHECK(EvaluateCondition({"viewport",V("(min-width: 500px) and ((orientation: landscape) or (prefers-reduced-motion: reduce))"),{}},{}));
    CHECK(!EvaluateCondition({"viewport",V("(unknown-feature: true)"),{}},{}));
    auto invalid=Parse("Button { broken } Button {color: red;}");CHECK(invalid.HasErrors()&&Compute(invalid,node).Properties.empty());
    Limits limits;limits.Rules=1;CHECK(Parse("Button{} Image{}",limits).HasErrors());
    limits=Limits{};limits.Declarations=1;CHECK(Parse("Button{a:1;b:2}",limits).HasErrors());
    limits=Limits{};limits.ResolveWork=1;CHECK(!Compute(sheet,node,{},limits).Diagnostics.empty());
    node.Parent=&node;CHECK(!Compute(sheet,node).Diagnostics.empty());
}
static void RecoveryAndFuzz(){
    for(auto text:{"}","@", "Button {", "Button{ ) ] ; color:red; }", "Button{color:var(--x;}","@layer x { @media (x:y) {Button{}", "@font-face {src: url(x)", "Button { color: \"bad\n; a:1; }"})CHECK(Parse(text).HasErrors());
    std::mt19937 random(42);const std::string alphabet="a0-+.#@{}[]():;,/*\\\" ' \n";
    Limits limits;limits.Bytes=512;limits.Tokens=512;limits.Nesting=8;
    for(int sample=0;sample<1000;++sample){std::string source;for(std::size_t n=random()%256;n;--n)source+=alphabet[random()%alphabet.size()];auto parsed=Parse(source,limits);auto value=ParseValue(source,limits);(void)parsed;(void)value;}
}
static void Requirements(){
    auto sheet=Parse(R"(@require(accent, red, color, label("Accent")); @require(size, 12px, length, min(1), max(64)); @require(choice, center, enum, options(start, center, end)); @require(icon, none, texture); Button {color:require(accent);width:calc(require(-> size <-) * 2);text-align:require(choice)})");
    CHECK(!sheet.HasErrors()&&sheet.Requirements.size()==4);Node node;node.Type="Button";auto style=Compute(sheet,node);CHECK(Property(style,"color")=="red");CHECK(ResolveLength(style.Properties.at("width"))==24);
    auto bound=BindRequirements(sheet,{{"accent","blue"},{"size","24px"},{"choice","end"}});style=Compute(bound,node);CHECK(Property(style,"color")=="blue"&&ResolveLength(style.Properties.at("width"))==48&&Property(style,"text-align")=="end");
    bound=BindRequirements(sheet,{{"size","999px"},{"choice","invalid"}});CHECK(!bound.Diagnostics.empty());CHECK(ResolveLength(Compute(bound,node).Properties.at("width"))==24);
    CHECK(Parse("@require(large,\""+std::string(8193,'a')+"\",string);").HasErrors());
    for(auto bad:{"@require(a, 1, unknown);","@require(a, 1, float);@require(a,2,float);","@require(a, red, float);","@require(a, 2, int, min(3));","@require(a, x, enum, options(y));","@viewport(width>1px){@require(a,1,float);}","Button{color:require(missing)}","@require(icon,url(https://bad),texture);"})CHECK(Parse(bad).HasErrors());
}
int main(){try{Requirements();std::cerr<<"Lexical\n";Lexical();std::cerr<<"Selectors\n";Selectors();std::cerr<<"Values\n";Values();std::cerr<<"Cascade\n";Cascade();std::cerr<<"Fuzz\n";RecoveryAndFuzz();std::cout<<"CSS lexer, selectors, values, variables, cascade, conditions, recovery and fuzz regressions passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
