"""Reproducible named-message experiment tests; excludes obsolete PR release pins."""
from pathlib import Path
import subprocess,sys,os,json
p=Path(__file__).resolve().parent;app=p.parent
out=p/'out';out.mkdir(exist_ok=True)
results=[]
def run(name,command):
    r=subprocess.run(command,capture_output=True,text=True,encoding='utf-8',errors='replace',env=dict(os.environ,PYTHONIOENCODING='utf-8'))
    (out/(name+'.log')).write_text(r.stdout+r.stderr,encoding='utf-8')
    results.append(dict(test=name,passed=r.returncode==0))
    print(name, 'PASS' if r.returncode==0 else 'FAIL',flush=True)
    assert r.returncode==0,(r.stdout+r.stderr)[-3000:]
run('build',[sys.executable,str(app/'build.py')])
# Retain the production generator/mocks, with a replacement driver for the new
# settings. Do not run the legacy driver with obsolete frequency expectations.
generator=(p/'functional.py').read_text()
generator=generator[:generator.index('a=subprocess.run(')]
generator=generator.replace("driver=(p/'functional.c').read_text()","driver=(p/'settings-target.c').read_text()")
exec(compile(generator,str(p/'functional.py'),'exec'),{'__file__':str(p/'functional.py')})
exe=out/'settings-target.exe'
run('settings-compile',[os.environ.get('HOST_CC','gcc'),'-std=c11','-O1','-Wall','-Wextra','-ftrapv',str(out/'functional.c'),'-o',str(exe)])
run('settings-target',[str(exe)])
for name in ['repeat_selected','names','preview','navigation16','bank','waveforms','audio','arm_abi','render']:
    run(name,[sys.executable,str(p/(name+'.py'))])
node=os.environ.get('NODE','node')
run('editor-core',[node,'--preserve-symlinks','--preserve-symlinks-main',str(p/'editor-core.cjs')])
if '--browser' in sys.argv:
    run('editor-browser',[node,'--preserve-symlinks','--preserve-symlinks-main',str(p/'editor-browser.cjs')])
run('custom-file',[sys.executable,str(p/'custom_file.py'),str(p/'out-editor/named-custom.app')])
run('release',[sys.executable,str(p/'release.py')])
(out/'experimental-results.json').write_text(json.dumps(results,indent=2))
