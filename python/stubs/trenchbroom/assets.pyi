"""
GoldSrc asset placement operations.
"""
from __future__ import annotations
import trenchbroom
import typing
__all__: list[str] = ['place_model', 'place_sound', 'place_sprite', 'search']
def place_model(path: str, origin: typing.Any = None, classname: str = 'cycler_sprite', property: str = 'model', select: bool = False) -> trenchbroom.Entity:
    ...
def place_sound(path: str, origin: typing.Any = None, classname: str = 'ambient_generic', property: str = 'message', select: bool = False) -> trenchbroom.Entity:
    ...
def place_sprite(path: str, origin: typing.Any = None, classname: str = 'cycler_sprite', property: str = 'model', select: bool = False) -> trenchbroom.Entity:
    ...
def search(query: str = '', type: typing.Any = None, limit: typing.SupportsInt = 50) -> list:
    ...
