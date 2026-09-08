"""
Face collection operations.
"""
from __future__ import annotations
import collections.abc
import trenchbroom
__all__: list[str] = ['list', 'selected', 'set_material']
def list() -> list[trenchbroom.Face]:
    ...
def selected() -> list[trenchbroom.Face]:
    ...
def set_material(faces: collections.abc.Iterable, material: str) -> int:
    ...
