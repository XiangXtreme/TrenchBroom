"""
Current document viewport state and synchronous 3D camera control.
"""
from __future__ import annotations
import typing
__all__: list[str] = ['focus_selection', 'set_camera', 'set_options', 'state']
def focus_selection() -> dict:
    ...
def set_camera(position: typing.Any, target: typing.Any, up: typing.Any = (0, 0, 1)) -> dict:
    ...
def set_options(options: dict) -> dict:
    """
    Set a partial options dict idempotently in action mode. Discover keys and current values in viewport.state()['options']. face_render_mode: textured|flat|skip; entity_link_mode: all|transitive|direct|none; all other values are bool. Unknown keys or invalid values reject the whole patch before changes. Example: tb.viewport.set_options({'show_edges': False, 'entity_link_mode': 'none'}). These are shared native preferences, except the document's show_grid; no map undo.
    """
def state() -> dict:
    """
    Read camera state and an options dict. Options are native view preferences shared across documents, except show_grid which belongs to this document. Save options to restore them later with set_options; 2D rendering may force edges.
    """
