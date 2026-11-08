"""Example custom blocks: a small game-AI vocabulary showing the BlockType API (inputs, custom fields,
custom value types, context actions, validation and custom code generators)."""
from __future__ import annotations
from PySide6.QtCore import Qt, QRectF
from PySide6.QtGui import QColor, QPen

from .definition import BlockType, BlockShape, BlockAction, SectionDef
from .fields import (AssetInput, EntityInput, FloatInput, DropdownInput, StatementInput, StringInput, ColorInput,
                     VectorInput, BoolInput, LabelInput, ValueInput, FieldInput, NumberSocket)
from .theme import Qc


class PercentInput(FloatInput):
    """Custom field: a float 0..100 painted as a progress bar (shows how to build custom fields)."""
    Pill = False

    def __init__(self, Name, DefaultValue=50.0, Label=None, **kw):
        super().__init__(Name, DefaultValue, Label, Min=0.0, Max=100.0, **kw)

    def Measure(self, value, style):
        return (84, style.FieldHeight)

    def Paint(self, p, r, value, style):
        p.setPen(Qt.NoPen)
        p.setBrush(Qc(style.Theme.FieldFill))
        p.drawRoundedRect(r, 5, 5)
        fill = QRectF(r.x() + 2, r.y() + 2, (r.width() - 4) * float(value) / 100.0, r.height() - 4)
        p.setBrush(Qc("#59C059"))
        p.drawRoundedRect(fill, 4, 4)
        p.setPen(Qc(style.Theme.FieldText))
        p.setFont(style.Font)
        p.drawText(r, int(Qt.AlignCenter), f"{float(value):.0f}%")


class PatrolBlock(BlockType):
    Type = "game.ai.Patrol"
    DisplayName = "Patrol"
    Category = "Movement"
    Color = "#4C97FF"
    Icon = "footsteps"
    Inputs = [AssetInput("Route", AssetType="PatrolRoute", Label="patrol"), FloatInput("Speed", DefaultValue=3.0, Label="speed")]
    Keywords = ["route", "walk"]

    def Validate(self, Block):
        return [("Warning", "No patrol route selected")] if not Block.Values.get("Route") else []

    def GetContextActions(self, Block):
        return [BlockAction("Reset Speed", lambda b, ws: ws.SetFieldValue(b, "Speed", 3.0),
                            Enabled=Block.Values.get("Speed") != 3.0)]


class MoveToBlock(BlockType):
    Type = "game.ai.MoveTo"
    DisplayName = "Move To"
    Category = "Movement"
    Color = "#4C97FF"
    Icon = "arrow"
    Inputs = [EntityInput("Target", EntityType="Actor", DefaultValue="", Label="move to"),
              FloatInput("Speed", DefaultValue=3.0, Label="speed")]


class AttackBlock(BlockType):
    Type = "game.ai.Attack"
    DisplayName = "Attack"
    Category = "Combat"
    Icon = "sword"
    Inputs = [EntityInput("Target", EntityType="Actor", DefaultValue="", Label="attack")]


class CanSeeBlock(BlockType):
    Type = "game.ai.CanSee"
    DisplayName = "Can See"
    Category = "Sensing"
    Icon = "eye"
    Shape = BlockShape.Boolean
    Inputs = [EntityInput("Target", EntityType="Actor", DefaultValue="", Label="can see")]


class DistanceToBlock(BlockType):
    Type = "game.ai.DistanceTo"
    DisplayName = "Distance To"
    Category = "Sensing"
    OutputType = "Number"
    Inputs = [EntityInput("Target", EntityType="Actor", DefaultValue="", Label="distance to")]


class SetAlertBlock(BlockType):
    Type = "game.ai.SetAlert"
    DisplayName = "Set Alert"
    Category = "Combat"
    Inputs = [PercentInput("Level", 60.0, Label="set alert level")]


class SwitchBlock(BlockType):
    """Second sections example: SWITCH with any number of CASE sections and a DEFAULT."""
    Type = "game.ai.Switch"
    DisplayName = "Switch State"
    Category = "Control"
    Inputs = [DropdownInput("State", ["idle", "alert", "combat"], Label="switch state")]
    Sections = [
        SectionDef("Case", [DropdownInput("Match", ["idle", "alert", "combat"], Label="case"), StatementInput("Do")],
                   MenuLabel="Case"),
        SectionDef("Default", [StatementInput("Do", Label="default")], MenuLabel="Default", Max=1),
    ]


GAME_BLOCKS = [PatrolBlock, MoveToBlock, AttackBlock, CanSeeBlock, DistanceToBlock, SetAlertBlock, SwitchBlock]
GAME_ASSETS = {"PatrolRoute": ["Route01", "Route02", "Route03"]}
GAME_ENTITIES = {"Actor": ["Player", "Guard", "Enemy", "NPC"]}
