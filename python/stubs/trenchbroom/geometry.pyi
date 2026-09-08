"""
Native geometry analysis operations.
"""
from __future__ import annotations
import typing
__all__: list[str] = ['analyze_selection', 'csg_selection']
def analyze_selection(grid: typing.SupportsFloat = 1.0, detail: str = 'summary', max_brushes: typing.SupportsInt = 100) -> dict:
    ...
def csg_selection(operation: str) -> dict:
    ...
