from pathlib import Path
import os,subprocess,sys,json,re,shutil
p=Path(__file__).resolve().parent
cc=os.environ.get("ARM_CC") or shutil.which("arm-none-eabi-gcc") or "/opt/toolchain/bin/arm-none-eabi-gcc"
obj=str(Path(cc).with_name(Path(cc).name.replace("gcc","objcopy")))
vma=os.environ.get("APP_VMA","0x20000280")
quiet="--quiet" in sys.argv
# Never leave a previous build installable after a failed compilation.
for name in ["CWQSOAuto.app","cwqsoauto.elf","cwqsoauto.bin","cwqsoauto.map"]:
 (p/name).unlink(missing_ok=True)
def run(args,quiet_output=False): subprocess.run(args,cwd=p,check=True,stdout=subprocess.DEVNULL if quiet_output else None)
def step(number,label):
 if quiet:
  print(f"\r  🔨 {'CW QSO Auto':<13} [{number}/4] {label:<8}",end="",flush=True)
step(1,"assets")
run([sys.executable,"gen_assets.py","cwqsoauto_assets.bin","cwqsoauto_assets.h"])
flags="-mcpu=cortex-m0plus -mthumb -Os -fno-jump-tables -std=gnu11 -ffreestanding -fno-builtin -fno-common -fomit-frame-pointer -ffunction-sections -fdata-sections -Wall -Wextra -Werror -fstack-usage".split()
assert "--measure" not in sys.argv, "Use the strict production linker"
step(2,"compile")
run([cc,*flags,"-nostdlib","-nostartfiles","-T","app.ld","-Wl,--defsym,APP_VMA="+vma,"-Wl,--gc-sections","-Wl,-Map=cwqsoauto.map","-Wl,--build-id=none","-Wl,--no-warn-rwx-segments","cwqsoauto_app.c","-lgcc","-o","cwqsoauto.elf"])
step(3,"objcopy")
run([obj,"-O","binary","cwqsoauto.elf","cwqsoauto.bin"])
m=(p/"cwqsoauto.map").read_text(); start=int(re.search(r"0x([0-9a-f]+)\s+__app_bss_start",m).group(1),16);end=int(re.search(r"0x([0-9a-f]+)\s+__app_bss_end",m).group(1),16)
size=(p/"cwqsoauto.bin").stat().st_size
if not quiet:
 print(json.dumps(dict(overlay=size,bss=end-start,assets=(p/"cwqsoauto_assets.bin").stat().st_size,free=4096-size)))

if size <= 4096:
 step(4,"pack")
 run([sys.executable,"../pack_app.py","cwqsoauto.bin","CWQSOAuto.app","--name","CW QSO Auto","--ver","0.4","--api-min","3","--vma",vma,"--shortcut","none","--assets","cwqsoauto_assets.bin"],quiet_output=quiet)
 if quiet:
  print(f"\r  ✅ {'CW QSO Auto':<13} {size:4d} B  ({size * 100 // 4096}% of 4 KiB)  ->  CWQSOAuto.app")
else:
 raise SystemExit("Overlay exceeds 4096 bytes; no APP produced")
