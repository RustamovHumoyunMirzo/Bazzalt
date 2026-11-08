"""BlockWorkspace -- native PySide6 Scratch/Blockly-style visual programming widget.

    self.Workspace = BlockWorkspace(self)
    self.Workspace.RegisterBlocks([PatrolBlock, MoveToBlock])
"""
from .valuetypes import ValueType, ValueTypeRegistry
from .theme import BlockTheme, SCRATCH_CATEGORY_COLORS
from .icons import IconProvider, BUILTIN_ICONS
from .fields import (InputDef, LabelInput, FieldInput, CustomInput, StringInput, IntInput, FloatInput, BoolInput,
                     DropdownInput, AssetInput, EntityInput, ColorInput, VectorInput, ValueInput, BooleanInput,
                     StatementInput, NumberSocket, StringSocket, FieldStyle, FieldEditContext, ShowLineEditor,
                     ShowChoiceMenu)
from .definition import BlockType, BlockDefinition, BlockShape, SectionDef, BlockAction, NEXT
from .registry import BlockRegistry
from .commands import BlockCommandStack, BlockCommand, StateCommand, CallbackCommand, CompositeCommand
from .model import BlockModel, BlockDocument
from .connections import BlockConnectionManager, ConnectionResult
from .selection import BlockSelectionManager
from .serializer import BlockSerializer, ImportReport
from .codegen import (BlockCodeGenerator, CodeTarget, CodeContext, XmlBehaviorTarget, JsonTarget,
                      PseudoCodeTarget)
from .validation import ValidationIssue, ValidateDocument
from .graphics import BlockGraphicsItem
from .canvas import BlockCanvas
from .toolbox import BlockToolbox
from .workspace import BlockWorkspace
from . import stdlib

__all__ = [n for n in dir() if not n.startswith("_")]
