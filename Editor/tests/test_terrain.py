import os
os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
import tempfile
import unittest
from pathlib import Path
from PySide6.QtWidgets import QApplication,QLabel
from PySide6.QtWidgets import QWidget
from PySide6.QtCore import QObject,Signal,QPoint
from PySide6.QtGui import QAction
from types import SimpleNamespace
from Editor.gui.widgets.collections import ListInput,TabInput
from Editor.scripting import ScriptCompiler
from Editor.lua_declarations import ReadDeclarations
from Editor.gui.terrain import TerrainEditor

class TerrainAuthoringTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.App=QApplication.instance() or QApplication([])
    def test_collection_mutations_are_bounded_and_copy_values(self):
        field=ListInput([{'Name':'A'},{'Name':'B'}],factory=lambda value,changed:QLabel(value['Name']),default=lambda:{'Name':'New'},minimum=1,maximum=3)
        changes=[];field.ValueChanged.connect(changes.append);field.Move(1,-1);self.assertEqual(field.GetValue()[0]['Name'],'B')
        field.Add();field.Add();self.assertEqual(len(field.GetValue()),3);field.Remove(0);field.Remove(0);field.Remove(0);self.assertEqual(len(field.GetValue()),1)
        copy=field.GetValue();copy[0]['Name']='Changed';self.assertNotEqual(field.GetValue()[0]['Name'],'Changed');self.assertTrue(changes);field.deleteLater()
    def test_numeric_script_lists(self):
        with tempfile.TemporaryDirectory() as folder:
            source=Path(folder)/'Tool.cpp';source.write_text('COMPONENT(Tool) { public: PROPERTY(Bazzalt::FloatList, Heights, {}) };',encoding='utf-8')
            descriptor=ScriptCompiler.Inspect(source);wrapper=ScriptCompiler._Wrapper(descriptor);self.assertIn('ReadPropertyList(value,self->Heights)',wrapper);self.assertIn('WritePropertyList(self->Heights)',wrapper)
        declared=ReadDeclarations('local Tool = COMPONENT("Tool")\nTool.Values = PROPERTY("IntList", {1,2,3})')
        self.assertEqual(declared[1][0][2],'1,2,3')

    def test_tab_container(self):
        tabs=TabInput();tabs.AddSection('Brush',QLabel('Radius'));tabs.AddSection('Paint',QLabel('Layer'));self.assertEqual(tabs.count(),2);tabs.deleteLater()
    def test_overlay_is_a_scene_child_and_position_is_clamped(self):
        class Runtime(QObject):
            SceneChanged=Signal();ProjectChanging=Signal()
            def Entities(self):return [{'uuid':'terrain','name':'Ground'}]
            def EntityDetails(self,identity):return {'components':['Terrain']}
            def SetToolGuides(self,lines):pass
            def IsEditorLocked(self,identity):return False
        class Locale(QObject):
            LocaleChanged=Signal()
            def Translate(self,key):return key
        class Themes(QObject):ThemeChanged=Signal()
        window=QWidget();surface=QWidget(window);surface.resize(640,480);surface._attached=False;surface._captured=False
        action=QAction(window);action.setCheckable(True);window.Scene=SimpleNamespace(Surface=surface,TerrainAction=action)
        window._settings={};saves=[];window._settings_saver=lambda value:saves.append(dict(value));window.Localization=Locale();window.ThemeManager=Themes()
        controller=QObject(window);controller.Window=window;controller.Runtime=Runtime();controller.SelectedEntity='terrain'
        editor=TerrainEditor(controller);self.assertFalse(editor.Active);self.assertIsNone(editor.Target);self.assertEqual(surface.findChildren(QWidget),[])
        action.setChecked(True);self.assertTrue(editor.Active);self.assertFalse(editor.Overlay.isWindow());self.assertIs(editor.Overlay.parentWidget(),surface);self.assertEqual(editor.Target.currentData(),'terrain')
        editor.Place(QPoint(10000,10000));self.assertLessEqual(editor.Island.geometry().bottom(),surface.height());editor.SavePosition();self.assertTrue(saves)
        action.setChecked(False);self.assertTrue(editor.Overlay.isHidden());window.deleteLater()
    def test_native_and_lua_tab_metadata(self):
        with tempfile.TemporaryDirectory() as folder:
            source=Path(folder)/'Tool.cpp';source.write_text('COMPONENT(Tool) { public: TAB("Brush") PROPERTY(float, Radius, 1.0f) TAB("") PROPERTY(bool, Active, true) };',encoding='utf-8')
            descriptor=ScriptCompiler.Inspect(source)
            self.assertEqual(descriptor.properties[0].rules.get('Tab'),'Brush');self.assertFalse(descriptor.properties[1].rules.get('Tab'))
        declarations=ReadDeclarations('local Tool = COMPONENT("Tool")\nTAB("Brush")\nTool.Radius = PROPERTY("float", 1)\nTAB("")\nTool.Active = PROPERTY("bool", true)')
        self.assertEqual(declarations[1][0][4].get('Tab'),'Brush');self.assertFalse(declarations[1][1][4].get('Tab'))

if __name__=='__main__':unittest.main()
