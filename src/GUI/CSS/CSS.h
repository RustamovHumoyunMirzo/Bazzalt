#pragma once
// Private engine stylesheet library. No renderer, filesystem, network or editor dependencies.
#include <cstddef>
#include <cstdint>
#include <compare>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace Bazzalt::Runtime::CSS {
struct Location {std::size_t Offset=0,Line=1,Column=1;};
enum class Severity { Warning, Error };
struct Diagnostic {Severity Level;Location Source;std::string Message;};
struct Limits {
    std::size_t Bytes=1024*1024,Tokens=200000,Rules=10000,Declarations=50000;
    std::size_t Nesting=64,Selectors=4096,ExpansionBytes=65536,AncestorDepth=128,ResolveWork=1000000;
};
enum class TokenKind { Ident,Number,Dimension,Percentage,String,Hash,AtKeyword,Space,Symbol,End };
struct Token {TokenKind Kind;std::string Text,Unit;double Number=0;Location Source;bool IdentifierHash=true;};
struct Value {
    std::vector<Token> Tokens;
    [[nodiscard]] std::string ToString() const;
};
struct Declaration {std::string Property;Value Data;bool Important=false;Location Source;std::size_t Order=0;};
struct Specificity {
    std::size_t IDs=0,Classes=0,Types=0;
    auto operator<=>(const Specificity&) const=default;
    Specificity& operator+=(Specificity other){IDs+=other.IDs;Classes+=other.Classes;Types+=other.Types;return *this;}
};
enum class Combinator { None,Descendant,Child,AdjacentSibling,GeneralSibling };
struct Selector;
struct Test {
    enum class Kind { Type,Universal,ID,Class,Attribute,Pseudo };
    Kind Type=Kind::Universal;
    std::string Name,Operator,Argument;
    bool CaseInsensitive=false;
    std::vector<Selector> Alternatives;
};
struct Compound {std::vector<Test> Tests;Combinator Relation=Combinator::None;};
struct Selector {std::vector<Compound> Parts;Specificity Weight;Location Source;};
struct Condition {std::string Directive;Value Expression;Location Source;};
struct Rule {std::vector<Selector> Selectors;std::vector<Declaration> Declarations;std::vector<Condition> Conditions;std::string Layer;Location Source;};
struct AtRule {std::string Name;Value Prelude,Body;bool HasBlock=false;Location Source;};
struct Stylesheet {
    std::vector<Rule> Rules;
    std::vector<AtRule> AtRules;
    std::vector<std::string> Layers;
    std::vector<Diagnostic> Diagnostics;
    [[nodiscard]] bool HasErrors() const;
};
struct ParseResult {Value Data;std::vector<Diagnostic> Diagnostics;[[nodiscard]] bool HasErrors() const;};
[[nodiscard]] std::vector<Token> Tokenize(std::string_view source,std::vector<Diagnostic>& diagnostics,const Limits& limits={});
[[nodiscard]] Stylesheet Parse(std::string_view source,const Limits& limits={});
[[nodiscard]] ParseResult ParseValue(std::string_view source,const Limits& limits={});
[[nodiscard]] std::vector<Selector> ParseSelectors(std::string_view source,std::vector<Diagnostic>& diagnostics,const Limits& limits={});

// Immutable caller-owned hierarchy snapshot. No entity or renderer lifetime escapes into the parser.
struct Node {
    std::string Type,ID;
    std::set<std::string> Classes,States;
    std::map<std::string,std::string> Attributes;
    const Node* Parent=nullptr;
    const Node* PreviousSibling=nullptr;
    std::size_t ChildIndex=1,ChildCount=1,TypeIndex=1,TypeCount=1;
};
struct Environment {
    double Width=1280,Height=720,FontSize=16,RootFontSize=16,PercentageBase=0;
    std::string ColorScheme="dark";
    bool ReducedMotion=false;
};
struct Color {double R=0,G=0,B=0,A=1;};
[[nodiscard]] bool Matches(const Selector& selector,const Node& node,const Limits& limits={});
[[nodiscard]] bool EvaluateCondition(const Condition& condition,const Environment& environment);
struct ComputedStyle {
    std::map<std::string,Value> Properties,CustomProperties;
    std::vector<Diagnostic> Diagnostics;
};
// Cascade only: typed application to GUI components is a separate integration layer.
// Ordinary "inherit" / "initial" / "unset" keywords are intentionally retained.
[[nodiscard]] ComputedStyle Compute(const Stylesheet& sheet,const Node& node,const Environment& environment={},const Limits& limits={});
[[nodiscard]] std::optional<Value> SubstituteVariables(const Value& value,const std::map<std::string,Value>& variables,std::vector<Diagnostic>& diagnostics,const Limits& limits={});
[[nodiscard]] std::optional<double> ResolveLength(const Value& value,const Environment& environment={});
[[nodiscard]] std::optional<Color> ResolveColor(const Value& value,std::optional<Color> currentColor={});
[[nodiscard]] std::optional<std::string> ResolveString(const Value& value);
[[nodiscard]] std::optional<bool> ResolveBoolean(const Value& value);
// Asset URL/reference syntax is parsed as data, never fetched or opened.
[[nodiscard]] std::optional<std::string> ResolveReference(const Value& value);
}
