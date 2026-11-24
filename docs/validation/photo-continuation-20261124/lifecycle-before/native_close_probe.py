#!/usr/bin/env python3
"""Native XTEST entry; host alone calls public removed through its owning IPtr."""
from pathlib import Path
import argparse, json, os, signal, subprocess, time, traceback
from types import SimpleNamespace
from verify_native_photo_editor import NativeRun, VOLUME, file_record, gesture_summary, utc_now
OUT=Path(__file__).resolve().parent
p=argparse.ArgumentParser();p.add_argument('--mode',choices=['control','reentrant','wheel'],required=True);p.add_argument('--output-dir',type=Path,required=True);p.add_argument('--editorhost',type=Path,default=OUT/'editorhost-accepting');p.add_argument('--preload',type=Path);opts=p.parse_args()
opts.output_dir.mkdir(parents=True,exist_ok=False)
if opts.mode=='control':os.environ.pop('RHODES_REMOVE_DURING_BEGIN',None)
else:os.environ['RHODES_REMOVE_DURING_BEGIN']='1'
if opts.preload:os.environ['LD_PRELOAD']=str(opts.preload)
args=SimpleNamespace(editorhost=opts.editorhost,bundle=OUT/'RhodesResearch.vst3',output_dir=opts.output_dir,startup_timeout=15)
inputs=[args.editorhost,OUT/'editorhost-accepting.cpp',OUT/'runloop.cpp',args.bundle/'Contents/x86_64-linux/RhodesResearch.so',OUT/'verify_native_photo_editor.py',Path(__file__)]
if opts.preload:inputs.extend([opts.preload,OUT/'crash_trace.cpp'])
report={'mode':opts.mode,'started_utc':utc_now(),'source_files':[file_record(p) for p in inputs]}
r=None
try:
    r=NativeRun(args,report);r.launch();r.pointer(VOLUME)
    report['input_seconds']=r.elapsed()
    r.xtest.fake_input(r.connection,r.X.ButtonPress,detail=4 if opts.mode=='wheel' else 1)
    r.connection.sync()
    deadline=time.monotonic()+2
    while time.monotonic()<deadline and r.host.poll() is None:
        if r.events and opts.mode=='control':break
        time.sleep(.025)
    report['post_input_returncode']=r.host.poll()
    if r.host.poll() is None:
        if opts.mode=='control':r.pointer((VOLUME[0],VOLUME[1]-18))
        report['post_input_window_tree']=r.tree()
        r.outside();time.sleep(.15)
        report['close_message_seconds']=r.elapsed()
        msg=r.protocol.event.ClientMessage(window=r.top,client_type=r.connection.intern_atom('WM_PROTOCOLS'),data=(32,[r.connection.intern_atom('WM_DELETE_WINDOW'),r.X.CurrentTime,0,0,0]))
        r.top.send_event(msg,event_mask=r.X.NoEventMask);r.connection.sync()
        try:r.host.wait(timeout=5)
        except subprocess.TimeoutExpired:report['close_timeout']=True
    else:
        report['crash_before_ordinary_close']=True
    if r.reader:r.reader.join(timeout=1)
except Exception as e:
    report['exception']=str(e);report['traceback']=traceback.format_exc()
finally:
    if r is not None:
        try:r.cleanup()
        except Exception as e:report['cleanup_exception']=str(e)
    report['finished_utc']=utc_now()
    report['inputs_unchanged']=all(file_record(Path(v['path']))['sha256']==v['sha256'] for v in report['source_files'])
    log=(opts.output_dir/'editorhost.log').read_text() if (opts.output_dir/'editorhost.log').exists() else ''
    report['trace']=[line for line in log.splitlines() if not line.startswith('PROBE native fd')]
    marker='PROBE beginEdit returning kResultOk'
    report['native_callback_returned_after_begin']=marker in log and 'PROBE native fd callback returned' in log.split(marker,1)[1]
    report['public_removed_returned']='PROBE after public removed result=0' in log
    report['gesture']=gesture_summary(report.get('host_callbacks',[]),0)
    report['host_signal']=signal.Signals(-report['host_returncode']).name if report.get('host_returncode',0)<0 else None
    (opts.output_dir/'outcome.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:report[k] for k in ['mode','host_returncode','host_signal','public_removed_returned','native_callback_returned_after_begin','gesture','inputs_unchanged'] if k in report},indent=2))
    for line in report['trace']:print(line)
