"""
Map validation operations.
"""
from __future__ import annotations
import typing
__all__: list[str] = ['check']
def check(include_hidden: bool = False, limit: typing.SupportsInt = 500) -> dict:
    ...
