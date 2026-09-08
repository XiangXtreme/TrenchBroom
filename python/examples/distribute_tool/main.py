import math
import random

import trenchbroom as tb


def to_tuple(value):
    if isinstance(value, tb.Vec3):
        return (value.x, value.y, value.z)
    return (float(value[0]), float(value[1]), float(value[2]))


def vec3_sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def vec3_add(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def vec3_mul(v, s):
    return (v[0] * s, v[1] * s, v[2] * s)


def vec3_len(v):
    return math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2])


def vec3_normalize(v):
    length = vec3_len(v)
    if length == 0:
        return (0.0, 0.0, 1.0)
    return (v[0] / length, v[1] / length, v[2] / length)


def catmull_rom_spline(points, steps=10):
    points = [to_tuple(point) for point in points]
    if len(points) < 2:
        return points

    is_closed = vec3_len(vec3_sub(points[0], points[-1])) < 0.001
    if is_closed:
        control = [points[-2]] + list(points) + [points[1]]
    else:
        control = [points[0]] + list(points) + [points[-1]]

    smoothed = []
    for i in range(len(points) - 1):
        p0 = control[i]
        p1 = control[i + 1]
        p2 = control[i + 2]
        p3 = control[i + 3]
        for step in range(steps):
            t = step / float(steps)
            t2 = t * t
            t3 = t2 * t
            smoothed.append(
                (
                    0.5
                    * (
                        (2 * p1[0])
                        + (-p0[0] + p2[0]) * t
                        + (2 * p0[0] - 5 * p1[0] + 4 * p2[0] - p3[0]) * t2
                        + (-p0[0] + 3 * p1[0] - 3 * p2[0] + p3[0]) * t3
                    ),
                    0.5
                    * (
                        (2 * p1[1])
                        + (-p0[1] + p2[1]) * t
                        + (2 * p0[1] - 5 * p1[1] + 4 * p2[1] - p3[1]) * t2
                        + (-p0[1] + 3 * p1[1] - 3 * p2[1] + p3[1]) * t3
                    ),
                    0.5
                    * (
                        (2 * p1[2])
                        + (-p0[2] + p2[2]) * t
                        + (2 * p0[2] - 5 * p1[2] + 4 * p2[2] - p3[2]) * t2
                        + (-p0[2] + 3 * p1[2] - 3 * p2[2] + p3[2]) * t3
                    ),
                )
            )

    smoothed.append(points[-1])
    return smoothed


def binomial_coeff(n, k):
    if k < 0 or k > n:
        return 0
    return math.factorial(n) // (math.factorial(k) * math.factorial(n - k))


def bezier_curve(points, steps_per_segment=10):
    points = [to_tuple(point) for point in points]
    n = len(points) - 1
    if n < 1:
        return points

    total_steps = n * steps_per_segment
    result = []
    for i in range(total_steps + 1):
        t = i / float(total_steps)
        x, y, z = 0.0, 0.0, 0.0
        for k, point in enumerate(points):
            term = binomial_coeff(n, k) * math.pow(1 - t, n - k) * math.pow(t, k)
            x += point[0] * term
            y += point[1] * term
            z += point[2] * term
        result.append((x, y, z))
    return result


class DistributeTool:
    def __init__(self):
        self.panel = tb.create_plugin_panel("Distribute Tool")
        self.raw_points = []
        self.path_points = []
        self.path_length = 0.0
        self.segment_lengths = []
        self.message = "Record path points or choose a path entity chain."
        self.selected_chain_index = 0
        self.detected_chains = []
        self.build_ui()
        self.refresh_status()

    def build_ui(self):
        self.panel.clear()
        self.panel.add_label_named("status", self.message)

        self.detected_chains = self.detect_entity_chains()
        chain_items = ["Choose path source", "Manual: record current selection"]
        chain_items.extend(
            [
                f"Chain {index + 1}: {len(chain)} nodes"
                for index, chain in enumerate(self.detected_chains)
            ]
        )
        if not self.detected_chains:
            chain_items.append("(No path chains found)")
        chain_items.append("Refresh list")
        self.panel.add_combo_box(
            "chain_selector",
            "Path Source",
            chain_items,
            self.on_chain_selected,
            self.selected_chain_index,
        )

        self.panel.add_int_field("count", "Count", 5, 2, 100)
        self.panel.add_checkbox("align_to_path", "Align to path", True)
        self.panel.add_checkbox("smooth_path", "Smooth path", False)
        self.panel.add_checkbox("use_bezier", "Use Bezier curve", False)
        self.panel.add_float_field("offset_start", "Start Offset (%)", 0.0, 0.0, 100.0, 1, 5.0)
        self.panel.add_float_field("offset_end", "End Offset (%)", 0.0, 0.0, 100.0, 1, 5.0)

        self.panel.add_label("Base offset")
        self.panel.add_float_field("base_off_x", "X Offset", 0.0, -4096.0, 4096.0, 1, 16.0)
        self.panel.add_float_field("base_off_y", "Y Offset", 0.0, -4096.0, 4096.0, 1, 16.0)
        self.panel.add_float_field("base_off_z", "Z Offset", 0.0, -4096.0, 4096.0, 1, 16.0)

        self.panel.add_label("Randomization")
        self.panel.add_float_field("rand_pos", "Position Jitter", 0.0, 0.0, 128.0, 1, 1.0)
        self.panel.add_float_field("rand_rot_z", "Random Z Rotation", 0.0, 0.0, 360.0, 1, 15.0)
        self.panel.add_float_field("rand_scale", "Random Scale (%)", 0.0, 0.0, 100.0, 1, 10.0)

        self.panel.add_button_callback("Record Path From Selection", self.record_path)
        self.panel.add_button_callback("Distribute Selected Objects", self.distribute)

    def current_document(self):
        return tb.documents.current()

    def entity_origin(self, entity):
        origin = entity.get("origin")
        if not origin:
            return None
        try:
            values = [float(value) for value in origin.split()]
        except ValueError:
            return None
        if len(values) < 3:
            return None
        return (values[0], values[1], values[2])

    def detect_entity_chains(self):
        doc = self.current_document()
        path_entities = [
            entity
            for entity in doc.entities
            if entity.classname.lower().startswith("path_")
        ]
        if not path_entities:
            return []

        by_targetname = {}
        for entity in path_entities:
            targetname = entity.get("targetname")
            if targetname:
                by_targetname[targetname] = entity

        next_map = {}
        has_parent = set()
        for entity in path_entities:
            target = entity.get("target")
            if target and target in by_targetname:
                next_entity = by_targetname[target]
                next_map[entity] = next_entity
                has_parent.add(next_entity)

        starts = [entity for entity in path_entities if entity not in has_parent]
        if not starts:
            starts = path_entities[:1]

        chains = []
        visited_global = set()
        for start in starts:
            if start in visited_global:
                continue
            chain = []
            current = start
            visited_local = set()
            while current is not None:
                chain.append(current)
                visited_local.add(current)
                visited_global.add(current)
                current = next_map.get(current)
                if current in visited_local:
                    chain.append(current)
                    break
            if len(chain) >= 2:
                chains.append(chain)
        return chains

    def chain_index_from_combo_value(self, value):
        if isinstance(value, int):
            return value
        try:
            return int(value)
        except (TypeError, ValueError):
            pass
        text = str(value)
        if text.startswith("Chain "):
            try:
                return int(text.split()[1].rstrip(":")) + 1
            except (IndexError, ValueError):
                return 0
        if text.startswith("Manual"):
            return 1
        if text.startswith("Refresh"):
            item_count = 2 + (len(self.detected_chains) if self.detected_chains else 1) + 1
            return item_count - 1
        return 0

    def on_chain_selected(self, value):
        index = self.chain_index_from_combo_value(value)
        self.selected_chain_index = index
        if index == 0:
            return

        refresh_index = 2 + (len(self.detected_chains) if self.detected_chains else 1)
        if index == refresh_index:
            self.selected_chain_index = 0
            self.message = "Refreshed path entity list."
            self.build_ui()
            return

        if index == 1:
            self.record_path()
            self.build_ui()
            return

        chain_index = index - 2
        if 0 <= chain_index < len(self.detected_chains):
            self.use_chain(chain_index)
            self.build_ui()

    def use_chain(self, index):
        points = [
            origin
            for origin in (self.entity_origin(entity) for entity in self.detected_chains[index])
            if origin is not None
        ]
        if len(points) < 2:
            self.refresh_status("Selected chain has fewer than 2 valid origins.")
            return
        self.raw_points = points
        self.path_points = list(points)
        self.calculate_path_metrics()
        self.refresh_status(f"Loaded chain {index + 1} with {len(points)} points.")

    def refresh_status(self, message=None):
        if message is not None:
            self.message = message
        points = f"Recorded points: {len(self.raw_points)}"
        if self.path_length > 0:
            points += f", length: {self.path_length:.1f}"
        self.panel.set_label_text("status", f"{self.message}\n{points}")

    def calculate_path_metrics(self):
        self.path_length = 0.0
        self.segment_lengths = []
        if len(self.path_points) < 2:
            return
        for index in range(len(self.path_points) - 1):
            distance = vec3_len(vec3_sub(self.path_points[index + 1], self.path_points[index]))
            self.segment_lengths.append(distance)
            self.path_length += distance

    def sort_path_entities(self, entities):
        by_targetname = {}
        for entity in entities:
            targetname = entity.get("targetname")
            if targetname:
                by_targetname[targetname] = entity

        next_map = {}
        has_parent = set()
        for entity in entities:
            target = entity.get("target")
            if target and target in by_targetname:
                next_entity = by_targetname[target]
                next_map[entity] = next_entity
                has_parent.add(next_entity)

        starts = [entity for entity in entities if entity not in has_parent]
        if not starts and entities:
            starts = [entities[0]]
        if not starts:
            return []

        result = []
        current = starts[0]
        visited = set()
        while current:
            result.append(current)
            visited.add(current)
            current = next_map.get(current)
            if current in visited:
                break
        return result

    def record_path(self):
        doc = self.current_document()
        vertices = [to_tuple(vertex) for vertex in doc.vertex_tool_vertices()]
        if len(vertices) >= 2:
            self.raw_points = vertices
            self.path_points = list(vertices)
            self.calculate_path_metrics()
            self.refresh_status(f"Recorded {len(vertices)} vertex-tool points.")
            return

        path_entities = [
            entity
            for entity in doc.selection.entities
            if entity.classname.lower().startswith("path_")
        ]
        if len(path_entities) >= 2:
            points = [
                origin
                for origin in (
                    self.entity_origin(entity) for entity in self.sort_path_entities(path_entities)
                )
                if origin is not None
            ]
            if len(points) >= 2:
                self.raw_points = points
                self.path_points = list(points)
                self.calculate_path_metrics()
                self.refresh_status(f"Recorded {len(points)} path entity points.")
                return

        self.refresh_status("Select at least 2 vertex handles or path entities.")

    def path_point_at_distance(self, distance):
        if len(self.path_points) < 2:
            return None, None
        if distance <= 0:
            p0 = self.path_points[0]
            p1 = self.path_points[1]
            return p0, vec3_normalize(vec3_sub(p1, p0))
        if distance >= self.path_length:
            p0 = self.path_points[-2]
            p1 = self.path_points[-1]
            return p1, vec3_normalize(vec3_sub(p1, p0))

        remaining = distance
        for index, segment_length in enumerate(self.segment_lengths):
            if remaining <= segment_length:
                p0 = self.path_points[index]
                p1 = self.path_points[index + 1]
                local_t = 0.0 if segment_length <= 0.0001 else remaining / segment_length
                position = vec3_add(p0, vec3_mul(vec3_sub(p1, p0), local_t))
                tangent = vec3_normalize(vec3_sub(p1, p0))
                return position, tangent
            remaining -= segment_length

        p0 = self.path_points[-2]
        p1 = self.path_points[-1]
        return p1, vec3_normalize(vec3_sub(p1, p0))

    def selected_center(self, selection):
        vertices_by_brush = selection.brush_vertices()
        vertices = [to_tuple(vertex) for brush_vertices in vertices_by_brush for vertex in brush_vertices]
        if not vertices:
            return None
        return (
            sum(vertex[0] for vertex in vertices) / len(vertices),
            sum(vertex[1] for vertex in vertices) / len(vertices),
            sum(vertex[2] for vertex in vertices) / len(vertices),
        )

    def distribute(self):
        if len(self.raw_points) < 2:
            self.refresh_status("Record a path first.")
            return

        if self.panel.get_checkbox("smooth_path"):
            if self.panel.get_checkbox("use_bezier"):
                self.path_points = bezier_curve(self.raw_points, steps_per_segment=10)
            else:
                self.path_points = catmull_rom_spline(self.raw_points, steps=10)
        else:
            self.path_points = list(self.raw_points)
        self.calculate_path_metrics()

        doc = self.current_document()
        selection = doc.selection
        if not selection.all_entities:
            self.refresh_status("Select objects to distribute.")
            return

        current_center = self.selected_center(selection)
        if current_center is None:
            self.refresh_status("Selected objects have no brush vertices.")
            return

        count = int(self.panel.get_int_field("count"))
        align_to_path = self.panel.get_checkbox("align_to_path")
        start_distance = self.path_length * (self.panel.get_float_field("offset_start") / 100.0)
        end_distance = self.path_length * (1.0 - self.panel.get_float_field("offset_end") / 100.0)
        if start_distance > end_distance:
            start_distance = end_distance = (start_distance + end_distance) / 2.0

        effective_length = end_distance - start_distance
        step = effective_length / (count - 1) if count > 1 else 0.0
        base_offset = (
            self.panel.get_float_field("base_off_x"),
            self.panel.get_float_field("base_off_y"),
            self.panel.get_float_field("base_off_z"),
        )
        random_position = self.panel.get_float_field("rand_pos")
        random_rotation_z = self.panel.get_float_field("rand_rot_z")
        random_scale = self.panel.get_float_field("rand_scale") / 100.0
        current_yaw = 0.0

        with doc.transaction("Python API: Distribute Along Path"):
            for index in range(count):
                distance = (
                    start_distance + effective_length * 0.5
                    if count == 1
                    else start_distance + step * index
                )
                target_position, tangent = self.path_point_at_distance(distance)
                if target_position is None:
                    continue

                selection.duplicate()

                jitter = (
                    random.uniform(-random_position, random_position),
                    random.uniform(-random_position, random_position),
                    random.uniform(-random_position, random_position),
                )
                target_yaw = math.degrees(math.atan2(tangent[1], tangent[0])) if align_to_path else 0.0
                final_yaw = target_yaw + random.uniform(-random_rotation_z, random_rotation_z)
                final_center = vec3_add(vec3_add(target_position, jitter), base_offset)

                move = vec3_sub(final_center, current_center)
                selection.translate(move)
                selection.rotate(
                    (0.0, 0.0, 1.0), final_yaw - current_yaw, center=final_center
                )

                scale = 1.0 + random.uniform(-random_scale, random_scale)
                if abs(scale - 1.0) > 0.001:
                    selection.scale(scale, center=final_center)

                current_center = final_center
                current_yaw = final_yaw

        self.refresh_status(f"Generated {count} instances.")


_tool = DistributeTool()
