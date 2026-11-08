import os,tempfile,unittest
os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
from pathlib import Path
from PySide6.QtWidgets import QApplication
from Editor import behavior_tree as B
from Editor.gui.panels.behavior_tree import BehaviorTreePanel
from Editor.resources import ResourceManager
from Editor.localization import LocalizationManager
from Editor.theme import ThemeManager,Theme

class BehaviorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.App=QApplication.instance() or QApplication([])
    def Panel(self):
        resources=ResourceManager();return BehaviorTreePanel(None,LocalizationManager(resources),resources,ThemeManager(self.App))
    def test_invalid_xml(self):
        for source in ('<!DOCTYPE a><BehaviorTree/>','<BehaviorTree version="1"><Tree><Unknown/></Tree></BehaviorTree>',B.NewTree().replace('seconds="1"','seconds="nan"'),B.NewTree().replace('<Succeed/>','<Invert/>')):
            with self.assertRaises(ValueError):B.Parse(source)
    def test_catalog_roundtrip(self):
        panel=self.Panel()
        self.assertGreaterEqual(len(B.NODES),35)
        for name,(_,fields,cardinality) in B.NODES.items():
            with self.subTest(name=name):
                panel.Workspace.Clear();block=panel.Workspace.CreateBlock(name)
                if cardinality[0]:
                    kind='Option' if name=='Utility' else 'Succeed';child=panel.Workspace.CreateBlock(kind);panel.Workspace.Connect(block,'children',child)
                    if kind=='Option':panel.Workspace.Connect(child,'children',panel.Workspace.CreateBlock('Succeed'))
                root=B.FromWorkspace(panel.Workspace);xml=B.Source(root);B.Parse(xml)
                panel.Workspace.Serializer.ImportWorkspaceXml(B.ToWorkspace(root),Clear=True,Undoable=False)
                self.assertEqual(B.FromWorkspace(panel.Workspace).find('Tree')[0].tag,name)
        panel.deleteLater()
    def test_editor_history_unlock_and_conflict(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[2]/"build") as folder:
            path=Path(folder)/'npc.bbtree';B.Save(path,B.NewTree());panel=self.Panel();self.assertTrue(panel.Open(path))
            wait=next(b for b in panel.Workspace.Blocks() if b.Type=='Wait')
            panel.Workspace.SetFieldValue(wait,'seconds',2);self.assertTrue(panel.History.canUndo())
            panel.History.undo();self.assertEqual(panel.Workspace.FindBlock(wait.Id).Values['seconds'],1)
            panel.History.redo();self.assertEqual(panel.Workspace.FindBlock(wait.Id).Values['seconds'],2)
            self.assertTrue(panel.Flush());panel.Unlock();self.assertFalse(panel.Source.isReadOnly());self.assertTrue(panel.Workspace.Canvas.ReadOnly)
            self.assertEqual(panel.Workspace.Actions['Undo'].shortcuts(),[])
            panel.History.undo();self.assertTrue(panel.Source.isReadOnly());panel.History.redo();self.assertTrue(panel.Flush())
            panel.Source.setPlainText(panel.Source.toPlainText().replace('seconds="2.0"','seconds="4.0"'))
            path.write_text(B.NewTree());self.assertFalse(panel.Flush());panel.Reload();self.assertTrue(panel.Source.isReadOnly())
            panel.Timer.stop();panel.deleteLater()
    def test_theme(self):
        panel=self.Panel();panel.Themes.SetTheme(Theme.light());self.assertEqual(panel.Workspace.Theme.Background,Theme.light().background)
        panel.deleteLater()
    def test_every_block_uses_localized_names_fields_and_tooltips(self):
        panel=self.Panel()
        for name in B.NODES:
            definition=panel.Workspace.Registry.Get(name)
            self.assertEqual(definition.DisplayName,panel.Locale.Translate('behavior.block.'+name))
            self.assertFalse(definition.Tooltip.startswith('behavior.'))
            for field in definition.Inputs[1:]:self.assertFalse(field.Label.startswith('behavior.'))
        class Translation:
            def Translate(self,key):return 'Translated '+key
        B.LocalizeBlocks(panel.Workspace,Translation())
        definition=panel.Workspace.Registry.Get('MoveTo')
        self.assertEqual(definition.Inputs[0].Text,'Translated behavior.block.MoveTo')
        self.assertEqual(definition.Inputs[1].Label,'Translated behavior.field.key')
        self.assertEqual(definition.TypeId,'MoveTo')
        self.assertEqual(panel.Workspace.CategoryLabel('Physics'),'Translated behavior.category.Physics')
        panel.deleteLater()
    def test_authoring_metadata_survives_xml(self):
        panel=self.Panel();block=panel.Workspace.CreateBlock('Succeed')
        panel.Workspace.Document.SetComment(block,'NPC: stop & wait\nnext step',True)
        panel.Workspace.Document.SetCollapsed(block,True)
        root=B.Parse(B.Source(B.FromWorkspace(panel.Workspace)))
        panel.Workspace.Serializer.ImportWorkspaceXml(B.ToWorkspace(root),Clear=True,Undoable=False)
        restored=panel.Workspace.Blocks()[0]
        self.assertEqual(restored.Comment,'NPC: stop & wait\nnext step');self.assertTrue(restored.Collapsed)
        panel.deleteLater()
    def test_root_vector_fields_are_independent_of_layout(self):
        panel=self.Panel();panel.Workspace.CreateBlock('SetVelocity',120,200,dict(x=1,y=2,z=3))
        root=B.FromWorkspace(panel.Workspace);node=root.find('Tree')[0]
        self.assertEqual(float(node.get('x')),1);self.assertEqual(float(node.get('editor-x')),120)
        panel.Workspace.Serializer.ImportWorkspaceXml(B.ToWorkspace(root),Clear=True,Undoable=False)
        block=panel.Workspace.Blocks()[0];self.assertEqual(block.X,120);self.assertEqual(block.Values['y'],2)
        panel.deleteLater()

if __name__=='__main__':unittest.main()
