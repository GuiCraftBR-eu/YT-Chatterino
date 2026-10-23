"""Repeatable Tk resize workload, without accounts or network requests."""
from pathlib import Path
import argparse, cProfile, importlib.machinery, importlib.util, io, json, pstats, statistics, sys, time
import tkinter as tk
from unittest.mock import patch

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('repo', type=Path, help='Repository folder containing src/')
parser.add_argument('--output-dir', type=Path, default=Path.cwd(), help='Directory for JSON and profile reports')
args=parser.parse_args()
repo=args.repo.resolve()
sys.path.insert(0,str(repo/'src'))
loader=importlib.machinery.SourceFileLoader('resize_core',str(repo/'src/ytchatterino.pyw'))
spec=importlib.util.spec_from_loader(loader.name,loader)
core=importlib.util.module_from_spec(spec);sys.modules[loader.name]=core;loader.exec_module(core)
root=tk.Tk()
cfg={'features':{'alerts':{'enabled':False}},'appearance':{'font_size':11},'message_limit':2000}
patches=[patch.object(core,'load_config',return_value=cfg),patch.object(core,'save_config'),
         patch.object(core.App,'_read_twitch_auth',return_value=None),patch.object(core.FeatureController,'start'),
         patch.object(core.ChatSource,'ensure_worker')]
for p in patches:p.start()
app=core.App(root);root.title('YT-Chatterino resize benchmark')
source=app.source_for({'platform':'twitch','id':'benchmark'})
source.header_info={'title':'Long stream title with several words to wrap when resizing the application window',
                    'category':'Just Chatting','live':True,'viewers':10000}
workspace=app.new_workspace([source]);pane=workspace.panes[0]
root.geometry('800x660+80+80');root.update()
photo=tk.PhotoImage(master=root,width=28,height=28);photo.put('#9146ff',to=(0,0,28,28))
urls=('benchmark:image',);pane.emote_photos[urls]=photo
body='A message with enough text to wrap across lines in a narrow window Kappa and more conversation after the emote.'
start=body.index('Kappa')
pane._batch=True;pane._batch_bottom=True;pane.text.configure(state='normal')
for i in range(2000):
    pane.add({'name':'User'+str(i%50),'time':'12:00 am','text':body,'special':None,'badges':[],
              '_seq':i+1,'id':str(i),'native_emotes':[{'start':start,'end':start+5,'urls':urls}]},source)
pane._batch=False;pane._end(True);root.update()
counts={'header':0,'text':0,'root':0}
pane.header.bind('<Configure>',lambda e:counts.__setitem__('header',counts['header']+1),add='+')
pane.text.bind('<Configure>',lambda e:counts.__setitem__('text',counts['text']+1),add='+')
root.bind('<Configure>',lambda e:counts.__setitem__('root',counts['root']+1) if e.widget is root else None,add='+')
durations=[];profile=cProfile.Profile();profile.enable()
for cycle in range(3):
    for width in list(range(420,1041,20))+list(range(1020,419,-20)):
        before=time.perf_counter();root.geometry(f'{width}x660');root.update()
        durations.append((time.perf_counter()-before)*1000)
profile.disable()
settle_start=time.perf_counter()
for view in app.all_panes():
    if hasattr(view,'finish_resize'):view.finish_resize()
root.update()
settle_ms=(time.perf_counter()-settle_start)*1000
report={'repo':repo.name,'messages':2000,'steps':len(durations),'total_ms':round(sum(durations),1),
    'median_ms':round(statistics.median(durations),2),'p95_ms':round(sorted(durations)[int(len(durations)*.95)],2),
    'max_ms':round(max(durations),2),'settle_ms':round(settle_ms,2),
    'total_with_settle_ms':round(sum(durations)+settle_ms,1),'events':counts}
stream=io.StringIO();pstats.Stats(profile,stream=stream).strip_dirs().sort_stats('cumulative').print_stats(16)
args.output_dir.mkdir(parents=True,exist_ok=True)
out=args.output_dir/(repo.name+'-resize.json');out.write_text(json.dumps(report,indent=2),encoding='utf-8')
out.with_suffix('.profile.txt').write_text(stream.getvalue(),encoding='utf-8')
print(json.dumps(report));print(stream.getvalue())
app.quit()
for p in reversed(patches):p.stop()
