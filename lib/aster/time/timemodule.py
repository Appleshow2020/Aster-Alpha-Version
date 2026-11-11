"""Aster time module."""

from __future__ import annotations

from . import _time as _backend


__doc__ = "Time access and conversions for Aster."

__all__ = list(_backend.__all__)


# ---------------------------------------------------------
# Module Initialization
# ---------------------------------------------------------

for _name in __all__:
    globals()[_name] = getattr(
        _backend,
        _name
    )

del _name