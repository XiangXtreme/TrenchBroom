"""
Entity collection operations.
"""
from __future__ import annotations
import collections.abc
import trenchbroom
import typing
__all__: list[str] = ['create', 'create_from_schema', 'create_from_schema_batch', 'definitions', 'delete', 'find', 'link_chain_inspect', 'list', 'schema', 'selected', 'tie_brushes', 'untie_brushes', 'update', 'update_many']
def create(classname: str, properties: dict = {}, origin: typing.Any = None, select: bool = False) -> trenchbroom.Entity:
    """
    Create a point entity in map units. select defaults to false and preserves selection.
    """
def create_from_schema(classname: str, properties: dict = {}, origin: typing.Any = None, select: bool = False) -> trenchbroom.Entity:
    """
    Create a game-definition-validated point entity. select defaults to false.
    """
def create_from_schema_batch(entities: collections.abc.Iterable, select: bool = False) -> list[trenchbroom.Entity]:
    """
    Create entities after validating their game definitions. select defaults to false.
    """
def definitions(type: str = '', query: str = '', limit: typing.SupportsInt = 200) -> list:
    ...
def delete(entity: trenchbroom.Entity) -> None:
    ...
def find(classname: str | None = None, property: str | None = None, value: str | None = None) -> list[trenchbroom.Entity]:
    ...
def link_chain_inspect(start: typing.Any = None, classname: str = '', name_key: str = 'targetname', next_key: str = 'target', detail: str = 'summary', include_all_nodes: bool = False) -> dict:
    ...
def list() -> list[trenchbroom.Entity]:
    ...
def schema(classname: str) -> dict:
    ...
def selected(include_brushes: bool = False) -> list[trenchbroom.Entity]:
    ...
def tie_brushes(classname: str, brushes: typing.Any = None) -> trenchbroom.Entity:
    ...
def untie_brushes(objects: typing.Any = None) -> list[trenchbroom.Brush]:
    ...
def update(entity: trenchbroom.Entity, properties: dict = {}, remove_keys: collections.abc.Sequence[str] = []) -> None:
    ...
def update_many(entities: collections.abc.Iterable, properties: dict = {}, remove_keys: collections.abc.Sequence[str] = []) -> None:
    ...
