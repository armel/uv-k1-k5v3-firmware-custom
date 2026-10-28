"""Current V7 validation entry; historical drivers remain for reference."""
from pathlib import Path
import subprocess,sys
raise SystemExit(subprocess.call([sys.executable,str(Path(__file__).with_name('experimental.py')),*sys.argv[1:]]))
