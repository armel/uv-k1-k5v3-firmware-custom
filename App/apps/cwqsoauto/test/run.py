"""Run existing radio/UI suites plus offline editor tests. Build first."""
from pathlib import Path
import subprocess,sys,os,json
p=Path(__file__).resolve().parent
results=[]
commands=[(name,[sys.executable,str(p/(name+'.py'))]) for name in ['configuration','startup','waveforms','bank','settings','menu_hint','navigation16','preview','render','repeat','arm_abi','audio']]
node=os.environ.get('NODE','node')
commands.append(('editor-core',[node,'--preserve-symlinks','--preserve-symlinks-main',str(p/'editor-core.cjs')]))
if '--browser' in sys.argv:commands.append(('editor-browser',[node,'--preserve-symlinks','--preserve-symlinks-main',str(p/'editor-browser.cjs')]))
custom_files=[str(p/'out-editor/example.app')]
if '--browser' in sys.argv:custom_files.append(str(p/'out-editor/downloads/CWQSOAuto-personnalise.app'))
commands.extend([('custom_file',[sys.executable,str(p/'custom_file.py'),*custom_files]),('release',[sys.executable,str(p/'release.py')])])
for name,command in commands:
 run=subprocess.run(command,capture_output=True,text=True,encoding='utf-8',env=dict(os.environ,PYTHONIOENCODING='utf-8'))
 (p/'out'/('suite-'+name+'.log')).write_text(run.stdout+run.stderr,encoding='utf-8')
 results.append(dict(test=name,passed=run.returncode==0,summary=run.stdout[-1000:]))
 print(name,run.returncode,run.stdout[-350:],flush=True)
 if run.returncode:print(run.stderr[-1500:],flush=True)
(p/'out/validation-results.json').write_text(json.dumps(results,indent=2))
assert all(r['passed'] for r in results),'See test/out/suite-*.log'
