import os
os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock
from PySide6.QtWidgets import QApplication
from Editor.property_rules import CheckRules,Evaluate,State,Validate,RuleError
from Editor.scripting import ScriptCompiler,ScriptValidationError,ScriptAttachments
from Editor.gui.controller import EditorController
from Editor.gui.panels.properties import ComponentSection

class PropertyRulesTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.App=QApplication.instance() or QApplication([])

    def test_safe_logic_and_filters(self):
        props={'Enabled':True,'Limit':10}
        rules=CheckRules({'Min':0,'Max':100,'Validate':'not self.Enabled or value <= self.Limit','Message':'Too fast'})
        self.assertEqual(Validate(rules,props,8),8)
        with self.assertRaisesRegex(RuleError,'Too fast'):Validate(rules,props,11)
        self.assertEqual(Validate(rules,{'Enabled':False,'Limit':10},11),11)
        self.assertEqual(State({'VisibleIf':'self.Enabled == false'},props),(False,True))
        self.assertTrue(Evaluate('StartsWith(value, "item_") and Len(value) > 5',{},'item_one'))
        self.assertEqual(Validate({'Trim':True,'Filter':'Identifier','MaxLength':20},{},'  item_one  '),'item_one')
        for invalid in ('bad name','123abc'):
            with self.assertRaises(RuleError):Validate({'Filter':'Identifier'},{},invalid)
        with self.assertRaises(RuleError):Validate({'Pattern':'asset_*'},{},'other')
        with self.assertRaises(RuleError):Validate({'Min':0},{},[-1,2,3])
        for expression in ('__import__("os")','self.__class__','value[0]','value ** 10000','self.Enabled()'):
            with self.assertRaises(RuleError):CheckRules({'Validate':expression})
        with self.assertRaises(RuleError):Evaluate('1 / 0',{})
        with self.assertRaises(RuleError):CheckRules({'Min':10,'Max':1})

    def test_declarations_and_cpp_metadata(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'Rules.lua'
            path.write_text('local W=COMPONENT("Rules")\nW.Speed=PROPERTY("float",4,{Min=0,Max=20,Step=0.5,EnabledIf="self.Enabled",Validate="value <= self.Limit"})\nreturn W',encoding='utf-8')
            prop=ScriptCompiler.Inspect(path).properties[0]
            self.assertEqual(prop.rules['Step'],.5);self.assertEqual(prop.rules['Max'],20)
            for options in ('{Min=20,Max=1}','{Min=0,Min=1}','{Validate="os.execute(value)"}','{Unknown=true}'):
                path.write_text('local W=COMPONENT("Rules")\nW.Speed=PROPERTY("float",4,'+options+')\nreturn W',encoding='utf-8')
                with self.assertRaises(ScriptValidationError):ScriptCompiler.Inspect(path)
            path=Path(folder)/'Rules.cpp';path.write_text('COMPONENT(Rules) { PROPERTY(float, Speed, 4, Min=0, Max=20, ReadOnly=true) };',encoding='utf-8')
            self.assertEqual(ScriptCompiler.Inspect(path).properties[0].rules,{'Min':0,'Max':20,'ReadOnly':True})

    def test_live_inspector_conditions_and_rejected_edit_preserve_model(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'Rules.lua';path.write_text('''local W=COMPONENT("Rules")
W.Enabled=PROPERTY("bool",true)
W.Speed=PROPERTY("float",4,{Min=0,Max=20,Step=0.5,VisibleIf="self.Enabled",Validate="value <= 10",Message="Too fast"})
W.Name=PROPERTY("string","valid",{Trim=true,Filter="Identifier",MaxLength=20})
W.Fixed=PROPERTY("int",1,{ReadOnly=true})
return W''',encoding='utf-8')
            attachments=ScriptAttachments(folder);attachments.Attach('entity',ScriptCompiler.Inspect(path));script=attachments.For('entity')[0]
            controller=EditorController.__new__(EditorController)
            controller.ScriptAttachments=attachments;controller._SyncLuaScripts=Mock(return_value=True);controller.SetDirty=Mock();controller._script_rule_rows=[]
            section=ComponentSection('Rules')
            for name,value in script['properties'].items():
                editor=controller._ScriptEditor(script,name,value);section.AddField(name,editor)
                controller._script_rule_rows.append((script,name,section,editor,editor._inspector_rules))
            controller._RefreshScriptRules();speed=section._fields['Speed']
            self.assertEqual(speed.singleStep(),.5);self.assertEqual(speed.maximum(),20)
            self.assertFalse(section._fields['Fixed'].isEnabled())
            speed.SetValue(12);self.assertEqual(script['properties']['Speed'],4);self.assertEqual(speed.GetValue(),4);self.assertEqual(speed.toolTip(),'Too fast')
            speed.SetValue(8);self.assertEqual(script['properties']['Speed'],8)
            section._fields['Enabled'].SetValue(False);self.assertTrue(speed.isHidden());self.assertTrue(section._captions['Speed'].isHidden())
            self.assertFalse(controller._SetScriptProperty(script,'Speed',6));self.assertEqual(script['properties']['Speed'],8)
            section._fields['Enabled'].SetValue(True);self.assertFalse(speed.isHidden())
            self.assertFalse(controller._SetScriptProperty(script,'Name','bad name'));self.assertEqual(script['properties']['Name'],'valid')
            self.assertTrue(controller._SetScriptProperty(script,'Name','  fine_name  '));self.assertEqual(script['properties']['Name'],'fine_name')
            QApplication.clipboard().setText('{"Name":"bad name","Fixed":8}')
            section._PasteFields();self.assertEqual(script['properties']['Name'],'fine_name');self.assertEqual(script['properties']['Fixed'],1)
            QApplication.clipboard().setText('{"Name":"pasted"}')
            section._PasteFields();self.assertEqual(script['properties']['Name'],'pasted')
            section.deleteLater()
