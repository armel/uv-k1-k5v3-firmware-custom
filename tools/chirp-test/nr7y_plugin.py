"""pytest plugin that registers the NR7Y driver before test collection.

CHIRP's driver tests import only the bundled drivers, and chirp/drivers/ is
often not writable, so load ours the same way the GUI's "Load Module" does.
"""
import os
import sys
import types
import importlib.util

# The driver imports wx for its message-box callbacks; the tests never fire
# them, so a stub is enough and keeps wxPython out of the test dependencies.
if 'wx' not in sys.modules:
    wx = types.ModuleType("wx")
    for _n in ("OK", "CANCEL", "CANCEL_DEFAULT", "ICON_WARNING"):
        setattr(wx, _n, 0)
    wx.MessageBox = lambda *a, **k: 0
    sys.modules["wx"] = wx

from chirp import directory

MODULE = os.environ.get(
    'NR7Y_MODULE',
    os.path.join(os.path.dirname(os.path.abspath(__file__)),
                 '..', '..', 'chirp', 'brass.k1-k5v3.chirp.py'))

directory.enable_reregistrations()
spec = importlib.util.spec_from_file_location("chirp.loaded.nr7y", MODULE)
mod = importlib.util.module_from_spec(spec)
mod._was_loaded = True
sys.modules["chirp.loaded.nr7y"] = mod
spec.loader.exec_module(mod)
