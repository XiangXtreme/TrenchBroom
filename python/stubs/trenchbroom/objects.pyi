"""
Explicit object editing operations.
"""
from __future__ import annotations
import typing
__all__: list[str] = ['delete', 'duplicate', 'rotate', 'scale', 'translate']
def delete(targets: typing.Any) -> bool:
    ...
def duplicate(targets: typing.Any, *, select: bool = False) -> list:
    """
    Duplicate explicit targets and return their clones. select defaults to false.
    """
def rotate(targets: typing.Any, axis: typing.Any, angle_degrees: typing.SupportsFloat, *, center: typing.Any = None) -> bool:
    """
    Rotate explicit targets around an axis in degrees. center defaults to their bounds center.
    """
def scale(targets: typing.Any, factors: typing.Any, *, center: typing.Any = None) -> bool:
    """
    Scale explicit targets by a scalar or Vec3. center defaults to their bounds center.
    """
def translate(targets: typing.Any, offset: typing.Any) -> bool:
    """
    Translate explicit Entity or Brush targets by a map-unit Vec3 without changing selection.
    """
