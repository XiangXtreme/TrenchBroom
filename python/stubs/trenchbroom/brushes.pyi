"""
Brush collection and creation operations.
"""
from __future__ import annotations
import collections.abc
import trenchbroom
import typing
__all__: list[str] = ['create', 'create_box', 'create_boxes', 'create_prism', 'create_prisms', 'list', 'selected']
def create(points: collections.abc.Iterable, material: typing.Any = None, select: bool = False) -> trenchbroom.Brush:
    """
    Create a convex brush from map-unit points. select defaults to false.
    """
def create_box(min: typing.Any, max: typing.Any, material: typing.Any = None, select: bool = False) -> trenchbroom.Brush:
    ...
def create_boxes(boxes: collections.abc.Iterable, material: typing.Any = None, select: bool = False) -> list[trenchbroom.Brush]:
    """
    Create boxes in map units. Each item is {'min': (x,y,z), 'max': (x,y,z), 'material': 'name'}; material is optional and overrides the batch material. Example: tb.brushes.create_boxes([{'min': (0,0,0), 'max': (64,64,16)}], material='stone', select=False). Returns the created brush handles.
    """
def create_prism(points2d: collections.abc.Iterable, min_z: typing.SupportsFloat, max_z: typing.SupportsFloat, material: typing.Any = None, select: bool = False) -> trenchbroom.Brush:
    """
    Extrude a convex XY polygon from min_z to max_z, all in map units. points2d is a sequence of (x, y) pairs, e.g. [(0,0), (64,0), (32,64)].
    """
def create_prisms(polygons: collections.abc.Iterable, material: typing.Any = None, select: bool = False) -> list[trenchbroom.Brush]:
    """
    Each item is {'points2d': [(x,y), ...], 'min_z': z0, 'max_z': z1, 'material': 'name'} describing a convex XY prism in map units. The per-item material is optional and overrides the batch material.
    """
def list() -> list[trenchbroom.Brush]:
    ...
def selected() -> list[trenchbroom.Brush]:
    ...
