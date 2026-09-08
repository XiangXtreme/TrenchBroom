"""
Native undo and redo operations.
"""
from __future__ import annotations
import typing
__all__: list[str] = ['redo', 'status', 'undo']
def redo(document: typing.Any = None) -> bool:
    ...
def status(document: typing.Any = None) -> dict:
    ...
def undo(document: typing.Any = None) -> bool:
    ...
