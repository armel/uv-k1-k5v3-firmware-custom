"""Paths/toolchain shared by the standalone application tests."""
from pathlib import Path
import os,shutil
app=Path(__file__).resolve().parents[1]
p=app/'test/out-repeat';p.mkdir(exist_ok=True)
cc=Path(os.environ.get('ARM_CC') or shutil.which('arm-none-eabi-gcc') or '/opt/toolchain/bin/arm-none-eabi-gcc')
