"""Low-level time primitives for Aster."""

from __future__ import annotations

import math
import threading

from datetime import (
    datetime,
    timezone as _datetime_timezone,
    timedelta
)
from calendar import timegm as _calendar_timegm
from collections import namedtuple


# ---------------------------------------------------------
# Time Structure
# ---------------------------------------------------------

struct_time = namedtuple(
    "struct_time",
    (
        "tm_year tm_mon tm_mday "
        "tm_hour tm_min tm_sec "
        "tm_wday tm_yday tm_isdst"
    )
)


# ---------------------------------------------------------
# Internal Utilities
# ---------------------------------------------------------

def _validate_seconds(value, *, name="seconds"):
    try:
        seconds = float(value)
    except (TypeError, ValueError, OverflowError) as exc:
        raise TypeError(
            f"{name} must be a real number"
        ) from exc

    if math.isnan(seconds):
        raise ValueError(
            f"{name} must not be NaN"
        )

    if math.isinf(seconds):
        raise OverflowError(
            f"{name} must be finite"
        )

    return seconds


def _to_struct(dt, isdst=-1):
    return struct_time(
        dt.year,
        dt.month,
        dt.day,
        dt.hour,
        dt.minute,
        dt.second,
        dt.weekday(),
        dt.timetuple().tm_yday,
        isdst,
    )


# ---------------------------------------------------------
# Clock Functions
# ---------------------------------------------------------

def time():
    """Return the current Unix timestamp."""
    return datetime.now(
        _datetime_timezone.utc
    ).timestamp()


def time_ns():
    """Return the current Unix timestamp in nanoseconds."""
    return int(time() * 1_000_000_000)


def sleep(seconds):
    """Suspend execution for the given number of seconds."""
    seconds = _validate_seconds(seconds)

    if seconds < 0:
        raise ValueError(
            "sleep length must be non-negative"
        )

    if seconds == 0:
        return None

    threading.Event().wait(seconds)

    return None


# ---------------------------------------------------------
# Time Conversion
# ---------------------------------------------------------

def gmtime(seconds=None):
    """Convert a timestamp to UTC struct_time."""

    if seconds is None:
        dt = datetime.now(_datetime_timezone.utc)
    else:
        dt = datetime.fromtimestamp(
            _validate_seconds(seconds),
            _datetime_timezone.utc
        )

    return _to_struct(dt, 0)


def localtime(seconds=None):
    """Convert a timestamp to local struct_time."""

    if seconds is None:
        dt = datetime.now().astimezone()
    else:
        dt = datetime.fromtimestamp(
            _validate_seconds(seconds)
        ).astimezone()

    return _to_struct(dt, -1)


def mktime(t):
    """Convert local struct_time to timestamp."""

    if len(t) < 6:
        raise TypeError(
            "mktime() argument must be a 9-item sequence"
        )

    year, month, day, hour, minute, second = map(
        int, t[:6]
    )

    dt = datetime(
        year, month, day,
        hour, minute, second
    ).astimezone()

    return dt.timestamp()


def timegm(t):
    """Convert UTC struct_time to timestamp."""

    if len(t) < 6:
        raise TypeError(
            "timegm() argument must be a 9-item sequence"
        )

    return float(
        _calendar_timegm(tuple(t[:9]))
    )


# ---------------------------------------------------------
# Time Formatting
# ---------------------------------------------------------

def asctime(t=None):
    """Convert struct_time to a readable string."""

    if t is None:
        t = localtime()

    if len(t) < 6:
        raise TypeError(
            "asctime() argument must be a 9-item sequence"
        )

    weekdays = (
        "Mon", "Tue", "Wed", "Thu",
        "Fri", "Sat", "Sun"
    )

    months = (
        "Jan", "Feb", "Mar", "Apr",
        "May", "Jun", "Jul", "Aug",
        "Sep", "Oct", "Nov", "Dec"
    )

    year, month, day, hour, minute, second = map(
        int, t[:6]
    )

    weekday = (
        int(t[6])
        if len(t) > 6
        else datetime(year, month, day).weekday()
    )

    if not 0 <= weekday <= 6:
        raise ValueError("invalid weekday")

    if not 1 <= month <= 12:
        raise ValueError("invalid month")

    return (
        f"{weekdays[weekday]} "
        f"{months[month - 1]} "
        f"{day:2d} "
        f"{hour:02d}:{minute:02d}:{second:02d} "
        f"{year:04d}"
    )


def ctime(seconds=None):
    """Convert timestamp to local time string."""
    return asctime(localtime(seconds))


def strftime(format, t=None):
    """Format a time tuple."""

    if t is None:
        t = localtime()

    if len(t) < 6:
        raise TypeError(
            "strftime() argument must be a 9-item sequence"
        )

    year, month, day, hour, minute, second = map(
        int, t[:6]
    )

    dt = datetime(
        year, month, day,
        hour, minute, second
    )

    return dt.strftime(format)


def strptime(
    date_string,
    format="%a %b %d %H:%M:%S %Y"
):
    """Parse a date string into struct_time."""

    dt = datetime.strptime(
        date_string,
        format
    )

    return _to_struct(dt)


# ---------------------------------------------------------
# Timezone Information
# ---------------------------------------------------------

def _timezone_info():
    now = datetime.now().astimezone()

    offset = now.utcoffset() or timedelta(0)
    name = now.tzname() or "UTC"

    return int(offset.total_seconds()), name


_local_offset, _local_tzname = _timezone_info()

timezone = -_local_offset
altzone = timezone
daylight = 0

tzname = (
    _local_tzname,
    _local_tzname
)


# ---------------------------------------------------------
# Public Exports
# ---------------------------------------------------------

__all__ = [
    "time",
    "time_ns",
    "sleep",

    "gmtime",
    "localtime",
    "mktime",
    "timegm",

    "asctime",
    "ctime",
    "strftime",
    "strptime",

    "struct_time",

    "timezone",
    "altzone",
    "daylight",
    "tzname",
]