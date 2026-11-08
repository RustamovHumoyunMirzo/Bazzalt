"""Built-in standard blocks (events, control flow, logic, math, text, debug)."""
from __future__ import annotations

from .definition import BlockType, BlockShape, SectionDef
from .fields import (StringInput, FloatInput, DropdownInput, BooleanInput, ValueInput, StatementInput,
                     LabelInput, NumberSocket, StringSocket, FloatInput)


class WhenStartedBlock(BlockType):
    Type = "core.event.WhenStarted"
    DisplayName = "When started"
    Category = "Events"
    Shape = BlockShape.Entry
    Icon = "flag"
    Inputs = [LabelInput("when started")]
    Generators = {"xml": lambda ctx: None, "pseudo": lambda ctx: None}


class ForeverBlock(BlockType):
    Type = "core.control.Forever"
    DisplayName = "Forever"
    Category = "Control"
    Icon = "repeat"
    XmlTag = "repeat"
    HasNext = False
    Inputs = [StatementInput("Body", Label="repeat forever")]
    Keywords = ["loop", "repeat"]
    Generators = {"xml": lambda ctx: ctx.Target.Wrap("repeat", ctx.Statements("Body"))}


class RepeatBlock(BlockType):
    Type = "core.control.Repeat"
    DisplayName = "Repeat"
    Category = "Control"
    Icon = "repeat"
    Inputs = [NumberSocket("Times", 10, Label="repeat"), StatementInput("Body", Label="times")]
    Keywords = ["loop", "for"]

    @staticmethod
    def _gen(ctx):
        X = ctx.Target
        child = ctx.Value("Times")
        el = X.Element("repeat")
        if child is not None:
            el.append(X.Wrap("count", child))
        else:
            el.set("count", str(int(ctx.Field("Times", 10))) if float(ctx.Field("Times", 10)).is_integer() else str(ctx.Field("Times")))
        for s in ctx.Statements("Body"):
            el.append(s)
        return el
    Generators = {"xml": _gen.__func__}


class WhileBlock(BlockType):
    Type = "core.control.While"
    DisplayName = "While"
    Category = "Control"
    Inputs = [BooleanInput("Condition", Label="while", Required=True), StatementInput("Body", Label="do")]
    Keywords = ["loop"]


class IfBlock(BlockType):
    """IF / any number of ELSE IF / optional ELSE -- demonstrates the generic section API."""
    Type = "core.control.If"
    DisplayName = "If"
    Category = "Control"
    Inputs = [BooleanInput("Condition", Label="if", Required=True), StatementInput("Then", Label="then")]
    Sections = [
        SectionDef("Elif", [BooleanInput("Condition", Label="else if", Required=True), StatementInput("Do", Label="then")],
                   MenuLabel="Else If"),
        SectionDef("Else", [StatementInput("Do", Label="else")], MenuLabel="Else", Max=1),
    ]
    Keywords = ["condition", "branch", "else", "elif"]

    @staticmethod
    def _gen(ctx):
        X = ctx.Target
        el = X.Element("if")
        el.append(X.Wrap("condition", ctx.Value("Condition")))
        el.append(X.Wrap("then", ctx.Statements("Then")))
        for _, keys in ctx.Sections("Elif"):
            e = X.Element("elif")
            e.append(X.Wrap("condition", ctx.Value(keys["Condition"])))
            e.append(X.Wrap("then", ctx.Statements(keys["Do"])))
            el.append(e)
        for _, keys in ctx.Sections("Else"):
            el.append(X.Wrap("else", ctx.Statements(keys["Do"])))
        return el
    Generators = {"xml": _gen.__func__}


class WaitBlock(BlockType):
    Type = "core.control.Wait"
    DisplayName = "Wait"
    Category = "Control"
    Icon = "clock"
    Inputs = [NumberSocket("Seconds", 1.0, Label="wait"), LabelInput("seconds")]


class StopBlock(BlockType):
    Type = "core.control.Stop"
    DisplayName = "Stop"
    Category = "Control"
    HasNext = False
    Inputs = [DropdownInput("Target", [("all", "all"), ("this script", "this")], Label="stop")]


class CompareBlock(BlockType):
    Type = "core.logic.Compare"
    DisplayName = "Compare"
    Category = "Logic"
    Shape = BlockShape.Boolean
    Inputs = [ValueInput("A", ("Any",), Shadow=FloatInput("A", 0)),
              DropdownInput("Op", [("=", "eq"), ("≠", "ne"), ("<", "lt"), (">", "gt"), ("≤", "le"), ("≥", "ge")]),
              ValueInput("B", ("Any",), Shadow=FloatInput("B", 0))]


class AndBlock(BlockType):
    Type = "core.logic.And"
    DisplayName = "And"
    Category = "Logic"
    Shape = BlockShape.Boolean
    Inputs = [BooleanInput("A"), LabelInput("and"), BooleanInput("B")]


class OrBlock(BlockType):
    Type = "core.logic.Or"
    DisplayName = "Or"
    Category = "Logic"
    Shape = BlockShape.Boolean
    Inputs = [BooleanInput("A"), LabelInput("or"), BooleanInput("B")]


class NotBlock(BlockType):
    Type = "core.logic.Not"
    DisplayName = "Not"
    Category = "Logic"
    Shape = BlockShape.Boolean
    Inputs = [BooleanInput("Value", Label="not")]


class BooleanLiteralBlock(BlockType):
    Type = "core.logic.Boolean"
    DisplayName = "True / False"
    Category = "Logic"
    Shape = BlockShape.Boolean
    Inputs = [DropdownInput("Value", [("true", "true"), ("false", "false")])]


class NumberBlock(BlockType):
    Type = "core.math.Number"
    DisplayName = "Number"
    Category = "Math"
    OutputType = "Number"
    Inputs = [FloatInput("Value", 0)]


class ArithmeticBlock(BlockType):
    Type = "core.math.Arithmetic"
    DisplayName = "Arithmetic"
    Category = "Math"
    OutputType = "Number"
    Inputs = [NumberSocket("A", 0), DropdownInput("Op", [("+", "add"), ("−", "sub"), ("×", "mul"), ("÷", "div")]),
              NumberSocket("B", 0)]


class RandomBlock(BlockType):
    Type = "core.math.Random"
    DisplayName = "Random"
    Category = "Math"
    OutputType = "Number"
    Inputs = [NumberSocket("Min", 1, Label="random from"), NumberSocket("Max", 10, Label="to")]


class TextBlock(BlockType):
    Type = "core.text.Text"
    DisplayName = "Text"
    Category = "Text"
    OutputType = "String"
    Inputs = [StringInput("Text", "hello")]


class JoinBlock(BlockType):
    Type = "core.text.Join"
    DisplayName = "Join"
    Category = "Text"
    OutputType = "String"
    Inputs = [StringSocket("A", "hello ", Label="join"), StringSocket("B", "world")]


class LogBlock(BlockType):
    Type = "core.debug.Log"
    DisplayName = "Log"
    Category = "Debug"
    Inputs = [StringSocket("Message", "hello", Label="log")]


STANDARD_BLOCKS = [WhenStartedBlock, ForeverBlock, RepeatBlock, WhileBlock, IfBlock, WaitBlock, StopBlock,
                   CompareBlock, AndBlock, OrBlock, NotBlock, BooleanLiteralBlock,
                   NumberBlock, ArithmeticBlock, RandomBlock, TextBlock, JoinBlock, LogBlock]
STANDARD_CATEGORY_ORDER = ["Events", "Control", "Logic", "Math", "Text", "Debug"]
