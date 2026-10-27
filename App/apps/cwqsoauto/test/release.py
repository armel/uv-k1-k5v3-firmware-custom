"""Independent format/CRC/identity checks and official packer comparison."""
from support import app,p
from pathlib import Path
import json,hashlib,base64,re,struct,zlib,subprocess,sys
repo=app.parents[2];spec=json.loads((app/'release.json').read_text())
ref=(repo/'archive/apps/v6.3.0/CWQSOAuto.app').read_bytes()
assert hashlib.sha256(ref).hexdigest()==spec['app_sha256']
assert hashlib.sha256((app/'cwqsoauto_app.c').read_bytes()).hexdigest()==spec['source_sha256']
assert (app/'CWQSOAuto.app').read_bytes()==ref
html=(app/'editor.html').read_text(encoding='utf-8')
embedded=base64.b64decode(re.search(r'id="bank16-reference"[^>]*>(.*?)</script>',html,re.S).group(1))
assert embedded==ref
fmt='<4sHBBIIHH16s16sIIHH';header=struct.unpack(fmt,ref[:64]);size=header[4];start=64+size
assert header[:5]==(b'FAP1',1,1,3,4092) and size<=4096 and header[12]==2282
assert len(ref)==6438 and zlib.crc32(ref[64:start])==header[5] and zlib.crc32(ref[start:])&65535==header[13]
assert hashlib.sha256(ref[64:start]).hexdigest()==spec['code_sha256']
assert hashlib.sha256(ref[start:]).hexdigest()==spec['assets_sha256']
example=(app/'test/out-editor/example.app').read_bytes();x=struct.unpack(fmt,example[:64])
assert x[:13]==header[:13] and example[64:start]==ref[64:start]
assert zlib.crc32(example[start:])&65535==x[13]
allowed={62,63}
for i in range(16):allowed.update(range(start+512+i*68+4,start+512+i*68+68))
assert all(i in allowed for i,(a,b) in enumerate(zip(ref,example)) if a!=b)
code=p/'custom-code.bin';assets=p/'custom-assets.bin';packed=p/'repacked.app'
code.write_bytes(example[64:start]);assets.write_bytes(example[start:])
subprocess.run([sys.executable,str(repo/'App/apps/pack_app.py'),str(code),str(packed),'--name','CW QSO Auto','--ver','0.4','--api-min','3','--vma','0x20000280','--shortcut','none','--assets',str(assets)],check=True)
assert packed.read_bytes()==example
result=dict(approved_source_and_binary=True,rebuild_identical=True,embedded_identical=True,assets_identical=True,custom_code_preserved=True,official_packer_identical=True,passed=True)
(p/'release.json').write_text(json.dumps(result,indent=2));print(result)
