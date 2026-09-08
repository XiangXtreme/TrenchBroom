import json
import os
import tempfile
import time
import uuid
from pathlib import Path

import trenchbroom as tb


PANEL_TITLE = "Blender Brush Sync"
SCHEMA = "tb.blenderBrushSync.v1"
SYNC_DIR = Path(
    os.environ.get("TB_BLENDER_SYNC_DIR")
    or Path(os.environ.get("TEMP") or tempfile.gettempdir()) / "trenchbroom-blender-sync"
)
REQUEST_PATH = SYNC_DIR / "request.json"
RESPONSE_PATH = SYNC_DIR / "response.json"
PENDING_REQUEST_PATH = SYNC_DIR / "pending-request.json"


def _as_vec3(value):
    return [float(value[0]), float(value[1]), float(value[2])]


def _as_uv(value):
    return [float(value[0]), float(value[1])]


def _point_key(value):
    return tuple(round(float(value[index]), 4) for index in range(3))


def _points_key(values):
    return tuple(sorted(_point_key(value) for value in values))


def _solve_axis(points, coords, basis):
    point0, point1, point2 = (points[index] for index in basis)
    edge1 = [point1[i] - point0[i] for i in range(3)]
    edge2 = [point2[i] - point0[i] for i in range(3)]
    a = sum(value * value for value in edge1)
    b = sum(edge1[i] * edge2[i] for i in range(3))
    c = sum(value * value for value in edge2)
    determinant = a * c - b * b
    if abs(determinant) < 1e-12:
        return None
    d1 = coords[basis[1]] - coords[basis[0]]
    d2 = coords[basis[2]] - coords[basis[0]]
    s = (d1 * c - d2 * b) / determinant
    t = (d2 * a - d1 * b) / determinant
    return [s * edge1[i] + t * edge2[i] for i in range(3)]


def _face_uv_error(points, uvs):
    if len(points) != len(uvs) or len(points) < 3:
        return None
    basis = None
    for i in range(1, len(points) - 1):
        for j in range(i + 1, len(points)):
            edge1 = [points[i][axis] - points[0][axis] for axis in range(3)]
            edge2 = [points[j][axis] - points[0][axis] for axis in range(3)]
            cross = [
                edge1[1] * edge2[2] - edge1[2] * edge2[1],
                edge1[2] * edge2[0] - edge1[0] * edge2[2],
                edge1[0] * edge2[1] - edge1[1] * edge2[0],
            ]
            if sum(value * value for value in cross) > 1e-12:
                basis = (0, i, j)
                break
        if basis:
            break
    if not basis:
        return None
    u_axis = _solve_axis(points, [uv[0] for uv in uvs], basis)
    v_axis = _solve_axis(points, [uv[1] for uv in uvs], basis)
    if u_axis is None or v_axis is None:
        return None
    offset = [
        uvs[0][0] - sum(points[0][i] * u_axis[i] for i in range(3)),
        uvs[0][1] - sum(points[0][i] * v_axis[i] for i in range(3)),
    ]
    return max(
        max(
            abs(sum(point[i] * u_axis[i] for i in range(3)) + offset[0] - uv[0]),
            abs(sum(point[i] * v_axis[i] for i in range(3)) + offset[1] - uv[1]),
        )
        for point, uv in zip(points, uvs)
    )


def _write_json(path, payload):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temp_path = path.with_suffix(path.suffix + ".tmp")
    with temp_path.open("w", encoding="utf-8") as f:
        json.dump(payload, f, indent=2)
    os.replace(temp_path, path)


def _existing_wad_path(path, doc):
    if not path:
        return None
    wad_path = Path(path)
    candidates = [wad_path]
    if not wad_path.is_absolute() and doc.path:
        candidates.append(Path(doc.path).parent / wad_path)
    for candidate in candidates:
        if candidate.exists() and candidate.suffix.lower() == ".wad":
            return str(candidate)
    return str(wad_path) if wad_path.suffix.lower() == ".wad" else None


def _wad_paths(doc):
    result = []
    seen = set()

    def add(path):
        resolved = _existing_wad_path(path, doc)
        if resolved and resolved.lower() not in seen:
            seen.add(resolved.lower())
            result.append(resolved)

    for entity in doc.entities:
        if entity.classname == "worldspawn":
            wad_value = entity.get("wad", "")
            for path in wad_value.split(";"):
                add(path.strip())
            break

    for collection in doc.material_collections:
        add(collection.path)
    return result


class BlenderBrushSync:
    def __init__(self):
        self.panel = tb.create_plugin_panel(PANEL_TITLE)
        self.session_id = None
        self.cache = {}
        self.pending_response_mtime = None
        self.timer_token = None
        self.build_ui()
        self.restore_pending_session()
        self.timer_token = tb.set_interval(self.poll_response, 1000)

    def build_ui(self):
        self.panel.clear()
        self.panel.add_label("Brush UV/material exchange with an open Blender project.")
        self.panel.add_button_callback("Send Selection", self.send_selection)
        self.panel.add_button_callback("Apply Pending", self.apply_pending)
        self.panel.add_button_callback("Apply + Split", self.apply_pending_with_split)
        self.panel.add_label_named("status", "Ready.")

    def log(self, message):
        print(f"[{PANEL_TITLE}] {message}")
        self.panel.set_label_text("status", message)

    def selected_brushes(self):
        selection = tb.documents.current().selection
        brushes = list(selection.brushes)
        if brushes:
            return brushes

        for entity in selection.all_entities:
            brushes.extend(entity.brushes)
        return brushes

    def selected_faces(self):
        return list(tb.documents.current().selection.brush_faces)

    def export_faces(self, faces, brush_index):
        brush_id = f"brush{brush_index}"
        vertices = []
        vertex_ids = {}
        exported_faces = []
        face_cache = {}

        def intern_vertex(point):
            key = tuple(_as_vec3(point))
            if key not in vertex_ids:
                vertex_ids[key] = len(vertices)
                vertices.append(list(key))
            return vertex_ids[key]

        for face_index, face in enumerate(faces):
            face_id = f"face{face_index}"
            face_vertices = list(face.vertices)
            local_to_brush = [intern_vertex(point) for point in face_vertices]
            loops = []
            for loop in face.uv_loops:
                local_vertex = int(loop["vertex"])
                loops.append(
                    {
                        "vertex": local_to_brush[local_vertex],
                        "uv": _as_uv(loop["uv"]),
                    }
                )

            exported_faces.append(
                {
                    "id": face_id,
                    "vertices": local_to_brush,
                    "material": face.texture_name,
                    "loops": loops,
                }
            )
            face_cache[face_id] = {
                "handle": face,
                "vertices": local_to_brush,
                "points": [_as_vec3(point) for point in face_vertices],
                "vertex_to_local": {
                    brush_vertex: local_vertex
                    for local_vertex, brush_vertex in enumerate(local_to_brush)
                },
            }

        self.cache[brush_id] = {"faces": face_cache}
        return {"id": brush_id, "vertices": vertices, "faces": exported_faces}

    def export_brush(self, brush, brush_index):
        return self.export_faces(list(brush.faces()), brush_index)

    def rebuild_cache(self, payload):
        candidates = []
        for entity in tb.documents.current().entities:
            for brush in entity.brushes:
                for face in brush.faces():
                    points = [_as_vec3(point) for point in face.vertices]
                    candidates.append(
                        {"face": face, "points": points, "key": _points_key(points)}
                    )

        restored_cache = {}
        used_candidates = set()
        for brush_payload in payload.get("brushes", []):
            brush_id = str(brush_payload.get("id", ""))
            brush_vertices = [
                _as_vec3(point) for point in brush_payload.get("vertices", [])
            ]
            face_cache = {}
            for face_payload in brush_payload.get("faces", []):
                face_id = str(face_payload.get("id", ""))
                vertex_ids = list(map(int, face_payload.get("vertices", [])))
                if any(index < 0 or index >= len(brush_vertices) for index in vertex_ids):
                    return False, f"Invalid cached topology: {brush_id}/{face_id}"
                requested_points = [brush_vertices[index] for index in vertex_ids]
                matches = [
                    index
                    for index, candidate in enumerate(candidates)
                    if index not in used_candidates
                    and candidate["key"] == _points_key(requested_points)
                ]
                if len(matches) != 1:
                    return (
                        False,
                        f"Could not uniquely recover {brush_id}/{face_id} "
                        f"({len(matches)} matches)",
                    )

                candidate_index = matches[0]
                used_candidates.add(candidate_index)
                candidate = candidates[candidate_index]
                vertex_to_local = {}
                for brush_vertex, point in zip(vertex_ids, requested_points):
                    local_matches = [
                        index
                        for index, current_point in enumerate(candidate["points"])
                        if _point_key(current_point) == _point_key(point)
                    ]
                    if len(local_matches) != 1:
                        return False, f"Could not recover vertex: {brush_id}/{face_id}"
                    vertex_to_local[brush_vertex] = local_matches[0]

                face_cache[face_id] = {
                    "handle": candidate["face"],
                    "vertices": vertex_ids,
                    "points": candidate["points"],
                    "vertex_to_local": vertex_to_local,
                }
            restored_cache[brush_id] = {"faces": face_cache}

        self.cache = restored_cache
        return True, None

    def restore_pending_session(self):
        if not PENDING_REQUEST_PATH.exists():
            return
        try:
            with PENDING_REQUEST_PATH.open("r", encoding="utf-8-sig") as f:
                payload = json.load(f)
            if payload.get("schema") != SCHEMA:
                self.log("Pending Blender sync uses an unsupported schema.")
                return
            recovered, error = self.rebuild_cache(payload)
            if not recovered:
                self.log(error)
                return
            self.session_id = payload.get("sessionId")
            if RESPONSE_PATH.exists():
                self.log("Recovered pending Blender response. Apply when ready.")
            else:
                self.log("Recovered Blender sync session; waiting for Blender.")
        except Exception as e:
            self.log(f"Could not recover pending Blender sync: {e}")

    def send_selection(self):
        try:
            selected_faces = self.selected_faces()
            brushes = [] if selected_faces else self.selected_brushes()
            if not selected_faces and not brushes:
                self.log("No selected brushes or faces.")
                return

            self.session_id = uuid.uuid4().hex
            self.cache = {}
            payload = {
                "schema": SCHEMA,
                "sessionId": self.session_id,
                "createdAt": time.time(),
                "wadPaths": _wad_paths(tb.documents.current()),
                "selectionMode": "faces" if selected_faces else "brushes",
                "brushes": (
                    [
                        self.export_faces([face], face_index)
                        for face_index, face in enumerate(selected_faces)
                    ]
                    if selected_faces
                    else [
                        self.export_brush(brush, brush_index)
                        for brush_index, brush in enumerate(brushes)
                    ]
                ),
            }
            if RESPONSE_PATH.exists():
                RESPONSE_PATH.unlink()
            _write_json(PENDING_REQUEST_PATH, payload)
            _write_json(REQUEST_PATH, payload)
            face_count = sum(len(brush["faces"]) for brush in payload["brushes"])
            self.pending_response_mtime = None
            subject = "selected" if selected_faces else "from brushes"
            self.log(f"Sent {face_count} faces ({subject}).")
        except Exception as e:
            self.log(f"Error: {e}")
            raise

    def read_response(self):
        if not RESPONSE_PATH.exists():
            return None
        with RESPONSE_PATH.open("r", encoding="utf-8-sig") as f:
            response = json.load(f)
        if response.get("schema") != SCHEMA:
            raise ValueError("Response schema mismatch.")
        if response.get("sessionId") != self.session_id:
            raise ValueError("Response belongs to a different sync session.")
        return response

    def poll_response(self):
        if not self.session_id or not RESPONSE_PATH.exists():
            return
        mtime = RESPONSE_PATH.stat().st_mtime
        if mtime != self.pending_response_mtime:
            self.pending_response_mtime = mtime
            self.log("Blender response pending. Click Apply Pending.")

    def apply_pending(self):
        self.apply_response(split_non_affine=False)

    def apply_pending_with_split(self):
        self.apply_response(split_non_affine=True)

    def apply_response(self, split_non_affine):
        try:
            if not self.session_id or not self.cache:
                self.log("Send selection before applying.")
                return
            response = self.read_response()
            if response is None:
                self.log("No pending Blender response.")
                return

            warnings = list(response.get("warnings", []))
            pending_updates = []
            non_affine = []
            skipped = 0
            doc = tb.documents.current()

            for face_payload in response.get("faces", []):
                brush_id = face_payload.get("brushId")
                face_id = face_payload.get("faceId")
                face_cache = self.cache.get(brush_id, {}).get("faces", {}).get(face_id)
                if face_cache is None:
                    skipped += 1
                    warnings.append(f"Unknown face: {brush_id}/{face_id}")
                    continue

                loops = face_payload.get("loops", [])
                expected_vertices = set(face_cache["vertices"])
                loop_vertices = {int(loop.get("vertex", -1)) for loop in loops}
                if len(loops) != len(face_cache["vertices"]) or loop_vertices != expected_vertices:
                    skipped += 1
                    warnings.append(f"Topology changed: {brush_id}/{face_id}")
                    continue

                face = face_cache["handle"]
                if [_as_vec3(point) for point in face.vertices] != face_cache["points"]:
                    skipped += 1
                    warnings.append(f"Brush changed in TrenchBroom: {brush_id}/{face_id}")
                    continue

                local_loops = []
                for loop in loops:
                    brush_vertex = int(loop["vertex"])
                    local_loops.append(
                        {
                            "vertex": face_cache["vertex_to_local"][brush_vertex],
                            "uv": _as_uv(loop["uv"]),
                        }
                    )
                pending_updates.append(
                    {
                        "face": face,
                        "material": face_payload.get("material"),
                        "loops": local_loops,
                    }
                )

                error = _face_uv_error(
                    face_cache["points"],
                    [
                        next(
                            loop["uv"]
                            for loop in local_loops
                            if loop["vertex"] == vertex
                        )
                        for vertex in range(len(face_cache["points"]))
                    ],
                )
                if error is None or error > 0.001:
                    non_affine.append((brush_id, face_id, error))

            apply_uvs = (
                doc.set_face_uvs_with_split if split_non_affine else doc.set_face_uvs
            )
            applied = bool(pending_updates) and apply_uvs(pending_updates)
            if pending_updates and not applied:
                skipped += len(pending_updates)
                warnings.append("UV batch could not be applied; no faces were changed")

            if non_affine:
                details = ", ".join(
                    f"{brush_id}/{face_id} ({error:.2f}px)"
                    if error is not None
                    else f"{brush_id}/{face_id}"
                    for brush_id, face_id, error in non_affine
                )
                warnings.append(f"Non-affine faces: {details}")
            if applied:
                RESPONSE_PATH.unlink(missing_ok=True)
                REQUEST_PATH.unlink(missing_ok=True)
                PENDING_REQUEST_PATH.unlink(missing_ok=True)
            suffix = f", {len(warnings)} warnings" if warnings else ""
            if not applied and non_affine and not split_non_affine:
                self.log(f"Strict apply rejected {details}. Use Apply + Split.")
            elif applied and split_non_affine and non_affine:
                self.log(
                    f"Applied {len(pending_updates)} faces; "
                    f"split {len(non_affine)} non-affine faces."
                )
            else:
                self.log(
                    f"Applied {len(pending_updates) if applied else 0} UV/material faces, "
                    f"skipped {skipped}{suffix}."
                )
        except Exception as e:
            self.log(f"Error: {e}")
            raise


_sync = BlenderBrushSync()
