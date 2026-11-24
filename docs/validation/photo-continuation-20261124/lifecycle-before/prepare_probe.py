#!/usr/bin/env python3
from pathlib import Path
import difflib, hashlib, json, shutil, subprocess
OUT=Path(__file__).resolve().parent
BASE=OUT.parent/'w2-022'
SDK=OUT.parent/'vst3sdk'
def rec(p):
    p=Path(p); b=p.read_bytes(); return {'path':str(p),'sha256':hashlib.sha256(b).hexdigest(),'bytes':len(b)}
original=(BASE/'editorhost-accepting.cpp').read_text()
s=original.replace('#include <cstdio>','#include <cstdio>\n#include <cstdlib>')
s=s.replace('\tvoid closePlugView ();','\tvoid closePlugView ();\n\tvoid removeDuringBegin ();')
s=s.replace('\tbool resizeViewRecursionGard {false};','\tbool resizeViewRecursionGard {false};\n\tbool probeRemoved {false};')
s=s.replace('class ComponentHandler : public IComponentHandler','static WindowController* gAttachedWindowController = nullptr;\n\nclass ComponentHandler : public IComponentHandler')
s=s.replace('\t\tstd::cout << "beginEdit called (" << id << ")" << std::endl;\n\t\treturn kResultOk;', '''\t\tstd::cout << "beginEdit called (" << id << ")" << std::endl;
\t\tif (std::getenv ("RHODES_REMOVE_DURING_BEGIN") && gAttachedWindowController)
\t\t\tgAttachedWindowController->removeDuringBegin ();
\t\tstd::cout << "PROBE beginEdit returning kResultOk" << std::endl;
\t\treturn kResultOk;''')
s=s.replace('WindowController::~WindowController () noexcept\n{\n}', '''WindowController::~WindowController () noexcept
{
\tstd::cout << "PROBE WindowController destructor" << std::endl;
\tif (gAttachedWindowController == this) gAttachedWindowController = nullptr;
}''')
s=s.replace('\t\tIPlatform::instance ().kill (-1, "Attaching PlugView failed");\n\t}\n}', '''\t\tIPlatform::instance ().kill (-1, "Attaching PlugView failed");
\t}
\tgAttachedWindowController = this;
\tstd::cout << "PROBE attached host-owned view=" << plugView.get () << std::endl;
}

void WindowController::removeDuringBegin ()
{
\tif (!plugView || probeRemoved) return;
\tprobeRemoved = true;
\tstd::cout << "PROBE before public removed view=" << plugView.get () << std::endl;
\tauto result = plugView->removed ();
\tstd::cout << "PROBE after public removed result=" << result
\t          << " retained host view=" << plugView.get () << std::endl;
}''')
old='''\t\tplugView->setFrame (nullptr);
\t\tif (plugView->removed () != kResultTrue)
\t\t{
\t\t\tIPlatform::instance ().kill (-1, "Removing PlugView failed");
\t\t}
\t\tplugView = nullptr;'''
new='''\t\tstd::cout << "PROBE ordinary close enter retained view=" << plugView.get ()
\t\t          << " already_removed=" << probeRemoved << std::endl;
\t\tplugView->setFrame (nullptr);
\t\tif (!probeRemoved && plugView->removed () != kResultTrue)
\t\t{
\t\t\tIPlatform::instance ().kill (-1, "Removing PlugView failed");
\t\t}
\t\tstd::cout << "PROBE ordinary close removal complete" << std::endl;
\t\tif (gAttachedWindowController == this) gAttachedWindowController = nullptr;
\t\tplugView = nullptr;
\t\tstd::cout << "PROBE ordinary close view released" << std::endl;'''
assert s.count(old)==1;s=s.replace(old,new)
(OUT/'editorhost-accepting.cpp').write_text(s)
(OUT/'host-removal.patch').write_text(''.join(difflib.unified_diff(original.splitlines(True),s.splitlines(True),fromfile='editorhost-accepting.cpp',tofile='editorhost-removal.cpp')))
rpath=SDK/'public.sdk/samples/vst-hosting/editorhost/source/platform/linux/runloop.cpp'
r=rpath.read_text();needle='\t\t\t\te.second (e.first);';assert r.count(needle)==1
changed=r.replace(needle,'''\t\t\t{
\t\t\t\tauto dispatchFD = e.first;
\t\t\t\tstd::cout << "PROBE native fd callback enter fd=" << dispatchFD << std::endl;
\t\t\t\te.second (dispatchFD);
\t\t\t\tstd::cout << "PROBE native fd callback returned fd=" << dispatchFD << std::endl;
\t\t\t}''')
(OUT/'runloop.cpp').write_text(changed)
(OUT/'runloop-trace.patch').write_text(''.join(difflib.unified_diff(r.splitlines(True),changed.splitlines(True),fromfile='runloop.cpp',tofile='runloop-trace.cpp')))
recipe=json.loads((BASE/'commands.json').read_text())
commands=[]
for idx in (1,4):
    cmd=[a.replace(str(BASE),str(OUT)) for a in recipe['compile'][idx]]
    if idx==4:cmd[cmd.index('-c')+1]=str(OUT/'runloop.cpp')
    commands.append(cmd)
for idx in (0,2,3,5,6):
    obj=Path(recipe['compile'][idx][recipe['compile'][idx].index('-o')+1]);shutil.copy2(obj,OUT/obj.name)
link=[a.replace(str(BASE),str(OUT)) for a in recipe['link']]
(OUT/'commands.json').write_text(json.dumps({'compile':commands,'link':link,'cwd':str(OUT)},indent=2)+'\n')
inputs=[BASE/'editorhost-accepting.cpp',BASE/'commands.json',rpath,*[Path(a) for a in recipe['link'] if a.endswith('.a')]]
before=[rec(p) for p in inputs]
with (OUT/'host-build.log').open('w') as log:
    for cmd in [*commands,link]:subprocess.run(cmd,cwd=OUT,check=True,stdout=log,stderr=subprocess.STDOUT)
assert before==[rec(p) for p in inputs]
for filename in ['with-xvfb.py','snapshot02-provenance.json']:
    shutil.copy2(BASE/filename,OUT/filename)
shutil.copy2(BASE/'snapshot02/Fender-rhoades/tools/verify_native_photo_editor.py',OUT/'verify_native_photo_editor.py')
if not (OUT/'RhodesResearch.vst3').exists():shutil.copytree(BASE/'snapshot02/RhodesResearch.vst3',OUT/'RhodesResearch.vst3')
shutil.copy2(BASE/'snapshot02/Fender-rhoades/plugins/vst3/photo_editor.cpp',OUT/'photo_editor-frozen.cpp')
shutil.copy2(SDK/'LICENSE.txt',OUT/'SDK-LICENSE.txt')
fonts=(OUT.parent/'w1-015/fonts.conf').read_text().replace(str(OUT.parent/'w1-015/font-cache'),str(OUT/'font-cache'))
(OUT/'fonts.conf').write_text(fonts)
(OUT/'font-cache').mkdir(exist_ok=True)
(OUT/'xdg-cache').mkdir(exist_ok=True)
outputs=[rec(p) for p in [OUT/'editorhost-accepting',OUT/'editorhost-accepting.cpp',OUT/'runloop.cpp',OUT/'host-removal.patch',OUT/'runloop-trace.patch',OUT/'RhodesResearch.vst3/Contents/x86_64-linux/RhodesResearch.so',OUT/'photo_editor-frozen.cpp']]
(OUT/'provenance.json').write_text(json.dumps({'original_inputs_unchanged':True,'input_files':before,'output_files':outputs,'scope':'Only copied host source/runloop rebuilt. Host owns view throughout synchronous removed. Copied frozen Direct bundle; no SDK or production mutation.'},indent=2)+'\n')
print(json.dumps(outputs,indent=2))
