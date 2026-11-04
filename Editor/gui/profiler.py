"""Bounded, opt-in diagnostics; CPU samples are copied only when recording stops."""
from collections import deque
import os
from PySide6.QtCore import Qt, QTimer, QRectF, Signal
from PySide6.QtGui import QPainter, QColor
from PySide6.QtWidgets import (QDialog, QVBoxLayout, QHBoxLayout, QPushButton,
    QTreeWidget, QTreeWidgetItem, QLabel, QFileDialog, QTabWidget, QWidget,
    QScrollArea, QComboBox)


def ResidentBytes():
    """Current process resident memory, not an allocation/leak count."""
    try:
        if os.name == 'nt':
            import ctypes
            from ctypes import wintypes
            class Counters(ctypes.Structure):
                _fields_ = [('cb', wintypes.DWORD), ('faults', wintypes.DWORD)] + [(name, ctypes.c_size_t) for name in
                    ('peak', 'working', 'pagedPeak', 'paged', 'nonpagedPeak', 'nonpaged', 'pagefile', 'pagefilePeak')]
            counters = Counters(); counters.cb = ctypes.sizeof(counters)
            kernel = ctypes.WinDLL('kernel32'); kernel.GetCurrentProcess.restype = wintypes.HANDLE
            query = ctypes.WinDLL('psapi').GetProcessMemoryInfo
            query.argtypes = [wintypes.HANDLE, ctypes.POINTER(Counters), wintypes.DWORD]
            if query(kernel.GetCurrentProcess(), ctypes.byref(counters), counters.cb): return counters.working
        elif os.path.exists('/proc/self/statm'):
            with open('/proc/self/statm') as stream: return int(stream.read().split()[1]) * os.sysconf('SC_PAGE_SIZE')
    except (OSError, ValueError, IndexError): pass
    return None


class ProfileGraph(QWidget):
    Selected = Signal(int)
    def __init__(self, parent=None):
        super().__init__(parent); self.Values=[]; self.Index=-1; self.setMinimumHeight(130)
    def SetValues(self, values):
        self.Values=list(values); self.Index=-1; self.update()
    def paintEvent(self, event):
        p=QPainter(self); p.fillRect(self.rect(),self.palette().base()); values=self.Values
        if not values: return
        peak=max(max(values),.001); width=self.width()/len(values)
        p.setPen(Qt.PenStyle.NoPen)
        for i,value in enumerate(values):
            p.setBrush(self.palette().highlight() if i==self.Index else self.palette().link())
            height=max(1,value/peak*(self.height()-24)); p.drawRect(QRectF(i*width,self.height()-height,width-1 if width>2 else width,height))
        p.setPen(self.palette().text().color());p.drawText(6,16,f'{peak:.2f}')
    def mousePressEvent(self,event):
        if self.Values:
            self.Index=min(len(self.Values)-1,max(0,int(event.position().x()*len(self.Values)/max(1,self.width()))));self.update();self.Selected.emit(self.Index)


class Timeline(QWidget):
    def __init__(self,parent=None):
        super().__init__(parent);self.Samples=[];self.Bars=[];self.Hits=[];self.Lanes=[];self.Zoom=1;self.setMouseTracking(True)
    def SetSamples(self,samples):
        self.Samples=sorted(samples,key=lambda s:(s.get('thread',0),s.get('start_us',0),-s['duration_us']))
        self.Bars=[];self.Lanes=[];top=24;index=0
        # Calculate nesting once, rather than sorting and assigning lanes on every repaint.
        while index<len(self.Samples):
            thread=self.Samples[index].get('thread',0);ends=[];lane=[]
            while index<len(self.Samples) and self.Samples[index].get('thread',0)==thread:
                sample=self.Samples[index];at=sample.get('start_us',0);level=next((i for i,e in enumerate(ends) if e<=at),len(ends))
                if level==len(ends):ends.append(0)
                ends[level]=at+sample['duration_us'];lane.append((sample,top+level*20));index+=1
            self.Lanes.append((thread,top));self.Bars.extend(lane);top+=max(1,len(ends))*20+24
        self.setMinimumSize(1100*self.Zoom,max(160,top));self.update()
    def wheelEvent(self,event):
        if event.modifiers() & Qt.KeyboardModifier.ControlModifier:
            self.Zoom=max(1,min(16,self.Zoom+(1 if event.angleDelta().y()>0 else -1)));self.setMinimumWidth(1100*self.Zoom);self.update();event.accept()
        else:super().wheelEvent(event)
    def paintEvent(self,event):
        p=QPainter(self);p.fillRect(event.rect(),self.palette().base());self.Hits=[]
        if not self.Samples:return
        start=min(s.get('start_us',0) for s in self.Samples);end=max(s.get('start_us',0)+s['duration_us'] for s in self.Samples);span=max(1,end-start)
        # At most one tiny bar per lane/pixel. Full samples remain in export and aggregation.
        pixels=set()
        for sample,y in self.Bars:
            x=110+(sample.get('start_us',0)-start)/span*(self.width()-120);width=max(1,sample['duration_us']/span*(self.width()-120));rect=QRectF(x,y,width,18)
            if not rect.intersects(QRectF(event.rect())):continue
            if width<2:
                key=(y,int(x))
                if key in pixels:continue
                pixels.add(key)
            color=self.palette().highlight().color();p.fillRect(rect,color)
            if width>30:
                p.setPen(self.palette().highlightedText().color());p.save();p.setClipRect(rect);p.drawText(rect.adjusted(3,0,-2,0),Qt.AlignmentFlag.AlignVCenter,sample['name']);p.restore()
            self.Hits.append((rect,sample))
        p.setPen(self.palette().text().color())
        for thread,y in self.Lanes:p.drawText(4,y+14,str(thread)[-12:])
    def mouseMoveEvent(self,event):
        for rect,sample in reversed(self.Hits):
            if rect.contains(event.position()):self.setToolTip(f"{sample['name']}\n{sample['duration_us']/1000:.3f} ms");return
        self.setToolTip('')


class ProfilerDialog(QDialog):
    def __init__(self, editor):
        super().__init__(editor)
        self.setWindowFlag(Qt.WindowType.WindowMinimizeButtonHint,False)
        self.Runtime=editor.Runtime;self.Tr=editor.Localization.Translate;self.Capturing=False;self.Samples=[];self.Frames=[];self.Snapshots=deque(maxlen=240)
        self.setWindowTitle(self.Tr('profiler.title'));self.resize(1000,650)
        layout=QVBoxLayout(self);bar=QHBoxLayout()
        self.Start=QPushButton(self.Tr('profiler.start'));self.Stop=QPushButton(self.Tr('profiler.stop'));self.Export=QPushButton(self.Tr('profiler.export'));self.Stop.setEnabled(False)
        for button in (self.Start,self.Stop,self.Export):bar.addWidget(button)
        self.All=QPushButton(self.Tr('profiler.all'));bar.addWidget(self.All);bar.addStretch();layout.addLayout(bar)
        self.Tabs=QTabWidget();layout.addWidget(self.Tabs)
        overview=QWidget();box=QVBoxLayout(overview);self.Graph=ProfileGraph();box.addWidget(QLabel(self.Tr('profiler.frames')));box.addWidget(self.Graph)
        self.Table=QTreeWidget();self.Table.setHeaderLabels([self.Tr('profiler.scope'),self.Tr('profiler.calls'),self.Tr('profiler.total'),self.Tr('profiler.maximum')]);box.addWidget(self.Table);self.Tabs.addTab(overview,self.Tr('profiler.cpu'))
        self.Timeline=Timeline();scroll=QScrollArea();scroll.setWidgetResizable(True);scroll.setWidget(self.Timeline);self.Tabs.addTab(scroll,self.Tr('profiler.timeline'))
        system=QWidget();box=QVBoxLayout(system);self.Metric=QComboBox()
        for key in ('gpu_frame_ms','backend_frame_ms','resident_mib','primitive_cache_bytes','renderables','render_primitives','objects','components','gui_batches'):
            self.Metric.addItem(self.Tr('profiler.metric.'+key),key)
        box.addWidget(self.Metric);self.SystemGraph=ProfileGraph();box.addWidget(self.SystemGraph);self.MetricStatus=QLabel();box.addWidget(self.MetricStatus)
        self.Counters=QTreeWidget();self.Counters.setHeaderLabels([self.Tr('profiler.counter'),self.Tr('profiler.value')]);box.addWidget(self.Counters);self.MemoryNote=QLabel(self.Tr('profiler.memory_note'));self.MemoryNote.setWordWrap(True);box.addWidget(self.MemoryNote);self.Tabs.addTab(system,self.Tr('profiler.system'))
        self.Status=QLabel();self.Status.setWordWrap(True);layout.addWidget(self.Status)
        self.Timer=QTimer(self);self.Timer.setInterval(250);self.Timer.timeout.connect(self.Poll)
        self.Start.clicked.connect(self.Begin);self.Stop.clicked.connect(self.End);self.Export.clicked.connect(self.Save);self.All.clicked.connect(lambda:self.ShowSamples(self.Samples));self.Graph.Selected.connect(self.SelectFrame);self.Metric.currentIndexChanged.connect(self.RefreshMetric)

    def Begin(self):
        try:
            self.Runtime.ProfileCommand('begin_profile_capture');self.Capturing=True;self.Snapshots.clear();self.Samples=[];self.Frames=[];self.Graph.SetValues([]);self.ShowSamples([]);self.Counters.clear();self.RefreshMetric();self.Start.setEnabled(False);self.Stop.setEnabled(True);self.Status.setText(self.Tr('profiler.capturing'));self.Timer.start()
        except Exception as error:self.Status.setText(str(error))

    def Poll(self):
        try:
            stats=dict(getattr(self.Runtime,'Statistics',lambda:{})());resident=ResidentBytes()
            if resident is not None:stats['resident_mib']=resident/(1024*1024)
            self.Snapshots.append(stats);self.Counters.clear()
            for key,value in sorted(stats.items()):QTreeWidgetItem(self.Counters,[self.CounterLabel(key),self.Tr('profiler.unavailable') if value<0 else f'{value:,.3f}'])
            self.RefreshMetric()
        except Exception as error:self.Status.setText(str(error))

    def CounterLabel(self,key):
        translated=self.Tr('profiler.metric.'+key)
        return key.replace('_',' ').capitalize() if translated=='profiler.metric.'+key else translated

    def RefreshMetric(self):
        key=self.Metric.currentData();values=[s[key] for s in self.Snapshots if s.get(key,-1)>=0];self.SystemGraph.SetValues(values);self.MetricStatus.setText('' if values else self.Tr('profiler.unavailable'))

    def ShowSamples(self,samples):
        groups={}
        for sample in samples:
            stats=groups.setdefault(sample['name'],[0,0.,0.]);duration=sample['duration_us']/1000;stats[0]+=1;stats[1]+=duration;stats[2]=max(stats[2],duration)
        self.Table.clear()
        for name,(count,total,maximum) in sorted(groups.items(),key=lambda item:-item[1][1]):QTreeWidgetItem(self.Table,[name,str(count),f'{total:.3f}',f'{maximum:.3f}'])
        self.Timeline.SetSamples(samples)

    def SelectFrame(self,index):
        frame=self.Frames[index];start=frame.get('start_us',0);end=start+frame['duration_us']
        self.ShowSamples([s for s in self.Samples if s.get('start_us',0)<end and s.get('start_us',0)+s['duration_us']>start])

    def End(self):
        self.Timer.stop()
        try:
            self.Runtime.ProfileCommand('end_profile_capture');self.Capturing=False;self.Start.setEnabled(True);self.Stop.setEnabled(False)
            self.Samples=list(self.Runtime.ProfileCommand('profile_samples'));self.Frames=sorted([s for s in self.Samples if s['name'] in ('Editor.Frame','Engine.Update')],key=lambda s:s.get('start_us',0))
            self.Graph.SetValues([s['duration_us']/1000 for s in self.Frames]);self.ShowSamples(self.Samples);self.Status.setText(self.Tr('profiler.finished'))
        except Exception as error:self.Status.setText(str(error))

    def Save(self):
        if self.Capturing:self.End()
        path,_=QFileDialog.getSaveFileName(self,self.Tr('profiler.export'),'bazzalt-trace.json','JSON (*.json)')
        if path:
            try:self.Runtime.ProfileCommand('export_profile_trace',path)
            except Exception as error:self.Status.setText(str(error))

    def done(self,result):
        if self.Capturing:self.End()
        self.Timer.stop();super().done(result)
