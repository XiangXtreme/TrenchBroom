"""
Native action discovery and execution.
"""
from __future__ import annotations
__all__: list[str] = ['execute', 'list']
def execute(action_id: str) -> None:
    ...
def list() -> list[str]:
    ...
