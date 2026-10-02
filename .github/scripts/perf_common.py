"""What the nightly performance scripts share (#677): loading a sibling script
and writing seconds.

perf-history.py, perf-budgets.py and perf-issues.py are scripts with a dash in
their names, so a sibling loads them by path; and each writes a time, which
below a millisecond is in microseconds (a read takes 0.1 µs, #705).
"""

from __future__ import annotations

import importlib.util
import sys
from pathlib import Path
from types import ModuleType
from typing import Callable


def load_script(name: str, filename: str) -> ModuleType:
    """The script filename beside this module, as the module name, loaded once."""
    if name in sys.modules:
        return sys.modules[name]
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def format_seconds(value: float | None,
                   coarse: Callable[[float], str] = lambda v: f"{v:.4f} s") -> str:
    """A time in seconds as text: microseconds below a millisecond, coarse above."""
    if value is None:
        return "–"
    if abs(value) < 0.001:
        return f"{value * 1e6:.1f} µs"
    return coarse(value)
