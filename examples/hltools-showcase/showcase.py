#!/usr/bin/env python3
"""Source for the self-contained, annotated hltools exhibit maps."""

from __future__ import annotations

import importlib.util
from pathlib import Path


HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
spec = importlib.util.spec_from_file_location("tutorial_geometry", HERE.parent / "hltools-tutorial" / "generate.py")
tutorial = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tutorial)

# Texture name, category, title, and two short lines. The same data drives the
# textures and their in-map placements; names fit GoldSrc's 15-byte limit.
SIGNS = {
    "S00_START": ("START HERE", "欢迎来到编译实验室", "先打开各编号地图", "选中实体查看属性"),
    "S00_LIGHT": ("01  LIGHT", "光照与发光材质", "点光源、聚光灯", "贴图光与阴影"),
    "S00_DETAIL": ("02–03  CSG", "细节与碰撞", "观察 A / B 对照", "编译后比较结果"),
    "S00_VIS": ("04–05  VIS", "工具面与可见性", "HINT、SKIP、CLIP", "读取门户与 PVS"),
    "S00_ENTITY": ("06–08  LABS", "实体与光照实验", "轴心、天空、最低光", "结合说明逐项操作"),
    "S00_LEAK": ("09  EXERCISE", "漏空排查", "故意缺墙的练习", "载入点线并修复"),
    "S01_TEX": ("01 / TEXTURE", "贴图光源", "选 info_texlights", "LAMP 定义颜色和亮度"),
    "S01_SHADOW": ("01 / SHADOW", "遮挡与反弹", "比较蓝墙背面", "调整 bounce 和 AO"),
    "S01_SPOT": ("01 / SPOT", "聚光灯", "选 light_spot", "调整内外锥角"),
    "S02_LEVEL": ("02 / 01", "细节层级", "A 两个都是 1", "B 橙色体为 2"),
    "S02_DOWN": ("02 / 02", "降低切割层级", "A chopdown 为 0", "B 橙色体为 1"),
    "S02_UP": ("02 / 03", "提高被切割层级", "A chopup 为 0", "B 蓝色体为 1"),
    "S02_COP": ("02 / 04", "共面优先级", "A 蓝色优先", "B 橙色优先"),
    "S02_ROWA": ("A / ROW", "A 排", "对照实验的一侧", "查看上方站点说明"),
    "S02_ROWB": ("B / ROW", "B 排", "对照实验的另一侧", "查看上方站点说明"),
    "S03_SIMPLE": ("03 / 01", "simple", "精简碰撞模式", "比较编译碰撞数据"),
    "S03_LEGACY": ("03 / 02", "legacy", "传统碰撞模式", "在游戏中检查接缝"),
    "S03_TRIM": ("03 / 03", "legacytrim", "连续外表面碰撞", "适合比较台阶接缝"),
    "S03_NORM": ("03 / 04", "normalized", "归一化碰撞模式", "观察结果并非必然不同"),
    "S03_PASS": ("03 / 05", "实体与可穿越", "A 保留碰撞", "B zhlt_noclip = 1"),
    "S03_HULL": ("03 / 06", "碰撞细节层级", "比较 A / B 的", "clipnode 细节设置"),
    "S04_HINT": ("04 / VIS", "HINT + SKIP", "门洞薄片提示分割", "载入 PRT 比较门户"),
    "S04_CLIP": ("04 / COLLISION", "CLIP", "看不见的碰撞体", "不要用于封闭世界"),
    "S04_NULL": ("04 / SURFACE", "NULL", "隐藏柱子底面", "只用于不应看到的面"),
    "S04_NOCLIP": ("04 / BRUSH", "NOCLIP", "底面改为 NULL", "整个画刷不生成碰撞"),
    "S05_A": ("05 / PVS A", "可见性链接起点", "选 info_portal", "改 oneway 后重新编译"),
    "S05_B": ("05 / PVS B", "可见性链接终点", "墙不会被打通", "使用脚本检查 PVS"),
    "S06_DOOR": ("06 / ORIGIN", "旋转门与轴心", "选 func_door_rotating", "ORIGIN 方块决定轴心"),
    "S06_TRIGGER": ("06 / TRIGGER", "AAATRIGGER", "触发体积连接旋转门", "触发行为由游戏执行"),
    "S06_GROUP": ("06 / GROUP", "func_group", "用于编辑时归组", "编译后合入世界"),
    "S06_DETAIL": ("06 / DETAIL", "func_detail", "细节画刷合入世界", "不能用来封闭地图"),
    "S06_WALL": ("06 / MODEL", "func_wall", "保留为独立画刷模型", "可设置遮光标志"),
    "S07_SKY": ("07 / SKY", "天空与太阳光", "顶部使用 SKY", "light_environment 控制方向"),
    "S07_SHADE": ("07 / SHADOW", "遮阳板", "对比遮阳板内外", "编译后观察投影"),
    "S07_SUN": ("07 / SETTINGS", "调整太阳", "修改 pitch、angles", "重新烘焙再比较"),
    "S08_MIN": ("08 / MINLIGHT", "材质最低光照", "info_minlights 指定", "MINLIT = 0.45"),
    "S08_REF": ("08 / CONTROL", "对照材质", "MINBASE 不设最低光", "比较暗角下两块柱子"),
    "S08_OPT": ("08 / COMPILER", "编译参数实体", "rad_bounce = 0", "命令行参数可覆盖"),
    "S09_LEAK": ("09 / REPAIR", "这张地图故意漏空", "载入编译生成的点线", "用世界画刷补上东墙"),
}


def panel(cx, y, z, texture, facing="south"):
    """A 256 x 128 board. Align its 512 x 256 texture, even at negative x."""
    x0, x1 = cx - 128, cx + 128
    y0, y1 = (y, y + 8) if facing == "south" else (y - 8, y)
    source = tutorial.box(x0, y0, z, x1, y1, z + 128, "TRIM")
    lines = source.splitlines()
    face = 2 if facing == "south" else 3
    axis = (
        f"[ 1 0 0 {-2 * x0} ]" if facing == "south"
        else f"[ -1 0 0 {2 * x1} ]"
    )
    coords = lines[face + 1].split(" TRIM [", 1)[0]
    lines[face + 1] = f"{coords} {texture} {axis} [ 0 0 -1 {2 * (z + 128)} ] 0 0.5 0.5"
    return "\n".join(lines) + "\n"


def floor_panel(cx, cy, texture, width=128, height=64):
    """Low floor marker facing a visitor walking toward positive Y."""
    x0, x1 = cx - width // 2, cx + width // 2
    y0, y1 = cy - height // 2, cy + height // 2
    scale = width / 512
    if height / 256 != scale:
        raise ValueError("floor marker must preserve the sign aspect ratio")
    lines = tutorial.box(x0, y0, 0, x1, y1, 2, "TRIM").splitlines()
    coords = lines[6].split(" TRIM [", 1)[0]
    lines[6] = (f"{coords} {texture} [ 1 0 0 {-x0 / scale:g} ] "
                f"[ 0 -1 0 {y1 / scale:g} ] 0 {scale:g} {scale:g}")
    return "\n".join(lines) + "\n"


def add_to_world(source, brushes):
    marker = '\n}\n{\n"classname" "info_player_start"'
    if source.count(marker) != 1:
        raise ValueError("expected one worldspawn / player boundary")
    return source.replace(marker, "\n" + "".join(brushes) + marker, 1)


def boards(source, placements):
    brushes = [panel(x, y, z, name) for name, x, y, z in placements]
    return add_to_world(source, brushes).replace('"angle" "0"', '"angle" "90"', 1)


def overview():
    brushwork = tutorial.room(896, 384, 320)
    # Low plinths keep the room navigable while giving the exhibit a visual rhythm.
    brushwork += [tutorial.box(x - 80, 128, 0, x + 80, 240, 24, "TRIM") for x in (-640, -320, 0, 320, 640)]
    brushwork.append(floor_panel(0, 32, "S00_START", 192, 96))
    entities = [tutorial.light(x, 80, 272, 450, "210 230 255") for x in (-640, -320, 0, 320, 640)]
    text = tutorial.map_text("00 hltools exhibit", brushwork, entities, (0, -240, 40))
    names = ("S00_START", "S00_LIGHT", "S00_DETAIL", "S00_VIS", "S00_ENTITY", "S00_LEAK")
    return boards(text, [(name, x, 360, 112) for name, x in zip(names, (-672, -416, -160, 96, 352, 608))])


def lighting():
    text = tutorial.lighting()
    text += tutorial.entity("light_spot", {
        "origin": "288 80 208", "pitch": "-90", "angle": "0",
        "_light": "150 190 255 450", "_cone": "18", "_cone2": "35",
    })
    return boards(text, [("S01_TEX", -352, 296, 104), ("S01_SHADOW", 0, 296, 104), ("S01_SPOT", 352, 296, 104)])


def details():
    text = add_to_world(tutorial.details(), [floor_panel(-1000, -144, "S02_ROWA"),
                                            floor_panel(-1000, 144, "S02_ROWB")])
    return boards(text, [(name, x, 296, 104) for name, x in zip(
        ("S02_LEVEL", "S02_DOWN", "S02_UP", "S02_COP"), (-800, -256, 256, 800))])


def collisions():
    names = ("S03_SIMPLE", "S03_LEGACY", "S03_TRIM", "S03_NORM", "S03_PASS", "S03_HULL")
    text = add_to_world(tutorial.collisions(), [floor_panel(288, -112, "S02_ROWA"),
                                               floor_panel(288, 112, "S02_ROWB")])
    return boards(text, [(name, x, 296, 104) for name, x in zip(names, (-800, -480, -160, 160, 480, 800))])


def tool_textures():
    brushes = tutorial.room(1088, 320)
    # Two offset doors create a useful HINT plane, followed by three stations.
    for x, low, high in ((-800, -224, -64), (-480, 64, 224)):
        brushes += [tutorial.box(x - 8, -304, 0, x + 8, low, 240, "WALL"),
                    tutorial.box(x - 8, high, 0, x + 8, 304, 240, "WALL"),
                    tutorial.box(x - 8, low, 176, x + 8, high, 240, "WALL")]
    brushes += [tutorial.box(-800, -224, 0, -796, -64, 176, "SKIP", {0: "HINT"}),
                tutorial.box(-264, -96, 0, -248, 96, 96, "CLIP"),
                tutorial.box(192, 64, 0, 320, 192, 96, "TRIM", {4: "NULL"}),
                tutorial.box(736, 64, 0, 864, 192, 96, "ORANGE", {4: "NOCLIP"})]
    lights = [tutorial.light(x, 0, 208, 600) for x in (-960, -640, -256, 256, 800)]
    text = tutorial.map_text("04 tools and hint", brushes, lights, (-1000, -160, 40))
    return boards(text, [(name, x, 296, 104) for name, x in zip(
        ("S04_HINT", "S04_CLIP", "S04_NULL", "S04_NOCLIP"), (-800, -256, 256, 800))])


def portal():
    return boards(tutorial.portals(), [("S05_A", -480, 296, 104), ("S05_B", 480, 296, 104)])


def brush_entities():
    brushes = tutorial.room(896, 384, 320)
    # The trigger is a game entity; its target is the named rotating door.
    door = tutorial.entity("func_door_rotating", {
        "targetname": "showcase_door", "distance": "90", "speed": "90", "wait": "3",
    }, [tutorial.box(-720, 80, 0, -592, 96, 128, "BLUE"),
        tutorial.box(-728, 80, 56, -712, 96, 72, "ORIGIN")])
    trigger = tutorial.entity("trigger_multiple", {
        "targetname": "showcase_trigger", "target": "showcase_door", "wait": "3",
    }, [tutorial.box(-440, 64, 0, -328, 160, 96, "AAATRIGGER")])
    group = tutorial.entity("func_group", {"targetname": "editor_group"}, [
        tutorial.box(-112, 80, 0, 0, 176, 80, "ORANGE"),
        tutorial.box(-96, 96, 80, -16, 160, 144, "TRIM"),
    ])
    detail = tutorial.detail("world_detail", [tutorial.box(224, 80, 0, 352, 176, 120, "BLUE")])
    wall = tutorial.entity("func_wall", {"targetname": "opaque_wall", "zhlt_lightflags": "2"}, [
        tutorial.box(560, 80, 0, 688, 176, 144, "ORANGE"),
    ])
    lights = [tutorial.light(x, -128, 224, 500) for x in (-672, -336, 0, 336, 672)]
    text = tutorial.map_text("06 brush entities", brushes, lights + [door, trigger, group, detail, wall], (-800, -240, 40))
    names = ("S06_DOOR", "S06_TRIGGER", "S06_GROUP", "S06_DETAIL", "S06_WALL")
    return boards(text, [(name, x, 360, 112) for name, x in zip(names, (-672, -336, 0, 336, 672))])


def sky():
    brushes = tutorial.room(704, 384, 320)
    brushes[1] = tutorial.box(-704, -384, 320, 704, 384, 336, "SKY")
    # A sheltered and an exposed floor area make sunlight direction observable.
    brushes += [tutorial.box(-288, -128, 208, -32, 128, 224, "TRIM"),
                tutorial.box(-288, -128, 0, -272, -112, 208, "TRIM"),
                tutorial.box(160, -48, 0, 224, 48, 160, "BLUE")]
    sun = tutorial.entity("light_environment", {
        "origin": "0 0 192", "pitch": "-60", "angles": "0 45 0", "_light": "255 224 180 250",
    })
    text = tutorial.map_text("07 sky and sunlight", brushes, [sun], (0, -240, 40))
    return boards(text, [("S07_SKY", -448, 360, 112), ("S07_SHADE", 0, 360, 112), ("S07_SUN", 448, 360, 112)])


def rad_settings():
    brushes = tutorial.room(704, 320)
    brushes += [tutorial.box(-320, 32, 0, -192, 160, 112, "MINLIT"),
                tutorial.box(192, 32, 0, 320, 160, 112, "MINBASE")]
    entities = [tutorial.light(0, -192, 208, 300),
                tutorial.entity("info_minlights", {"origin": "-416 208 40", "MINLIT": "0.45"}),
                tutorial.entity("info_compile_parameters", {"origin": "416 208 40", "rad_bounce": "0"})]
    text = tutorial.map_text("08 lighting settings", brushes, entities, (0, -240, 40))
    return boards(text, [("S08_MIN", -448, 296, 104), ("S08_OPT", 0, 296, 104), ("S08_REF", 448, 296, 104)])


def leak():
    return boards(tutorial.leak(), [("S09_LEAK", 0, 232, 104)])


MAPS = [
    ("00_gallery", "00 实验室导览", overview),
    ("01_lighting", "01 光照、聚光灯与贴图光", lighting),
    ("02_detail_csg", "02 细节层级与切割", details),
    ("03_collision", "03 碰撞模式与穿越", collisions),
    ("04_tool_textures", "04 工具纹理与门户", tool_textures),
    ("05_portal", "05 info_portal 可见性", portal),
    ("06_brush_entities", "06 画刷实体与旋转轴", brush_entities),
    ("07_sky", "07 天空与环境光", sky),
    ("08_rad_settings", "08 最低光与编译参数", rad_settings),
    ("09_leak", "09 漏空练习（故意漏空）", leak),
]
