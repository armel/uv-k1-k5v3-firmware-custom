"""Replicate CHIRP's real module-load and clone-start flow.

This is the path that produced "Internal driver error": clone.py calls
rclass.detect_from_serial(serial) inside a try/except before touching the
port, and CloneModeRadio.detect_from_serial asserts when a class has
detected models but no implementation of its own.
"""
import sys, types, importlib.util, logging, os
sys.path.insert(0, os.environ.get('CHIRP_SRC', os.path.expanduser('~/code/chirp')))
logging.disable(logging.CRITICAL)
wx = types.ModuleType("wx")
for n in ("OK", "CANCEL", "CANCEL_DEFAULT", "ICON_WARNING"):
    setattr(wx, n, 0)
wx.MessageBox = lambda *a, **k: 0
sys.modules["wx"] = wx

from chirp import directory, errors

MODULE = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
    os.path.dirname(os.path.abspath(__file__)), '..', '..',
    'chirp', 'brass.k1-k5v3.chirp.py')

# --- exactly what wxui/main.py load_module does ---
directory.enable_reregistrations()
modname = 'chirp.loaded.%s' % os.path.splitext(os.path.basename(MODULE))[0]
spec = importlib.util.spec_from_file_location(modname, MODULE)
mod = importlib.util.module_from_spec(spec)
mod._was_loaded = True
sys.modules[modname] = mod
spec.loader.exec_module(mod)
print("module loaded ok")

rclass = mod.UVK5_NR7Y_Fusion

# --- exactly what wxui/clone.py does before opening/using the port ---
class DeadPort:
    """Must never be touched: detection has to resolve before any I/O."""
    timeout = 0.25
    def write(self, *a): raise AssertionError("detection touched the port")
    def read(self, *a):  raise AssertionError("detection touched the port")

failed = None
try:
    rclass = rclass.detect_from_serial(DeadPort())
except NotImplementedError:
    pass                      # clone.py: "no detection needed" - the good path
except errors.RadioError as e:
    failed = "RadioError: %s" % e
except Exception as e:
    failed = "%s: %s  -> clone.py reports 'Internal driver error'" % (
        type(e).__name__, e)

if failed:
    print("FAIL:", failed)
    sys.exit(1)
print("detect_from_serial: clean (NotImplementedError, port untouched)")
print("PASS")
