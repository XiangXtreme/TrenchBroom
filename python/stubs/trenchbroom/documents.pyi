"""
Document lifecycle operations.
"""
from __future__ import annotations
import trenchbroom
import typing
__all__: list[str] = ['activate', 'close', 'current', 'export', 'list', 'open', 'save', 'save_as', 'snapshot']
def activate(document: trenchbroom.Document) -> trenchbroom.Document:
    ...
def close(document: trenchbroom.Document, discard_changes: bool = False) -> None:
    ...
def current() -> trenchbroom.Document:
    ...
def export(path: str, strip_tb_properties: bool = True) -> trenchbroom.Document:
    ...
def list() -> list[trenchbroom.Document]:
    ...
def open(path: str) -> trenchbroom.Document:
    """
    Open an absolute map path and verify that the active document matches it. Action mode.
    """
def save(path: typing.Any = None) -> trenchbroom.Document:
    ...
def save_as(arg0: str) -> trenchbroom.Document:
    ...
def snapshot() -> dict:
    ...
