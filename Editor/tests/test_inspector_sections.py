import os
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
import tempfile
import unittest
from pathlib import Path
from unittest.mock import Mock
from PySide6.QtWidgets import QApplication, QComboBox, QDialog, QLabel, QPlainTextEdit, QPushButton, QTreeWidget, QWidget
from Editor.gui.panels.properties import ComponentSection
from Editor.gui.widgets.fields import FloatInput
from Editor.gui.version_control import VersionControlDialog
from Editor.gui.inspector_sections import FieldGroup
from Editor.scripting import ScriptCompiler, ScriptValidationError
from Editor.version_control import Change, Snapshot


class InspectorSectionsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.App=QApplication.instance() or QApplication([])

    def test_nested_fields_preserve_editing_visibility_and_clipboard(self):
        section=ComponentSection("Camera")
        editor=FloatInput(value=3)
        section.AddField("Focus Distance",editor,"Post Processing/Depth of Field")
        group=section.FieldSection("Post Processing/Depth of Field")
        self.assertIs(group,section.FieldSection("Post Processing/Depth of Field"))
        self.assertIs(section._fields["Focus Distance"],editor)
        group.Toggle.setChecked(False)
        self.assertTrue(group.Body.isHidden())
        section._CopyFields()
        self.assertIn('"Focus Distance":3',self.App.clipboard().text())
        group.Toggle.setChecked(True)
        section.SetFieldVisible("Focus Distance",False)
        self.assertTrue(editor.isHidden());self.assertTrue(group._captions["Focus Distance"].isHidden())
        section.SetFieldVisible("Focus Distance",True)
        section.SetFieldTitle("Focus Distance","Distance")
        self.assertEqual(group._captions["Focus Distance"].text(),"Distance")
        self.assertEqual(FieldGroup("Audio Source","Spatial Blend"),"Spatial Audio")
        self.assertEqual(FieldGroup("Rigidbody","LinearDamping"),"Dynamics")
        section.deleteLater()

    def test_cpp_and_lua_section_declarations_and_overrides(self):
        with tempfile.TemporaryDirectory() as folder:
            cpp=Path(folder)/"Grouped.cpp"
            cpp.write_text('COMPONENT(Grouped) { SECTION("Movement/Speed") PROPERTY(float, Speed, 4) PROPERTY(float, Other, 1, Section="Other") SECTION("") PROPERTY(int, Count, 2) };')
            lua=Path(folder)/"Grouped.lua"
            lua.write_text('local G=COMPONENT("Grouped")\nSECTION("Movement/Speed")\nG.Speed=PROPERTY("float",4)\nG.Other=PROPERTY("float",1,{Section="Other"})\nSECTION("")\nG.Count=PROPERTY("int",2)\nreturn G')
            for path in (cpp,lua):
                props=ScriptCompiler.Inspect(path).properties
                self.assertEqual([p.rules.get("Section","") for p in props],["Movement/Speed","Other",""])
            for title in ("/Bad","Bad//Child","/".join(["a"]*9)):
                cpp.write_text('COMPONENT(Grouped) { SECTION("'+title+'") PROPERTY(int, Count, 2) };')
                with self.assertRaises(ScriptValidationError):ScriptCompiler.Inspect(cpp)

    def test_git_refresh_preserves_items_and_diff_when_unchanged(self):
        dialog=VersionControlDialog.__new__(VersionControlDialog);QDialog.__init__(dialog)
        dialog.State=None;dialog.Tr=lambda key,**kw:key;dialog.InitButton=QPushButton(dialog)
        dialog.Location=QLabel(dialog);dialog.Changes=QTreeWidget(dialog);dialog.History=QTreeWidget(dialog)
        dialog.Branches=QComboBox(dialog);dialog.Diff=QPlainTextEdit(dialog);dialog.DeferredPreview=None
        state=Snapshot("root","main","",0,0,(Change("file.cpp",".","M"),),("main",),(),False,False)
        dialog.ShowState(state);item=dialog.Changes.topLevelItem(0);item.setSelected(True)
        dialog.ShowDiff(dialog.Diff,{"text":"diff text","truncated":False})
        cursor=dialog.Diff.textCursor();cursor.setPosition(3);dialog.Diff.setTextCursor(cursor)
        dialog.ShowState(state)
        dialog.ShowDiff(dialog.Diff,{"text":"diff text","truncated":False})
        self.assertIs(dialog.Changes.topLevelItem(0),item)
        self.assertTrue(item.isSelected());self.assertEqual(dialog.Diff.textCursor().position(),3)
        self.assertEqual(dialog.DeferredPreview,"diff")
        dialog.Future=None;dialog.ReloadFailed=False;dialog.Body=QWidget(dialog);dialog.Status=QPlainTextEdit(dialog)
        dialog.ActionButtons=[QPushButton(dialog)];dialog.Repository=object();dialog.Executor=Mock()
        dialog.Submit(lambda:None,lambda _:None)
        self.assertTrue(dialog.ActionButtons[0].isEnabled())
        dialog.deleteLater()
