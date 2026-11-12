#!/usr/bin/env python3
"""Report world-leaf visibility between two points in a GoldSrc BSP v30."""

import argparse
from pathlib import Path
import struct


def records(data, pattern):
    size = struct.calcsize(pattern)
    if len(data) % size:
        raise ValueError("BSP lump has a partial record")
    return list(struct.iter_unpack(pattern, data))


def inspect(path, first, second):
    data = path.read_bytes()
    if len(data) < 124 or struct.unpack_from("<i", data)[0] != 30:
        raise ValueError("expected a GoldSrc BSP v30 file")
    lumps = []
    for i in range(15):
        offset, length = struct.unpack_from("<ii", data, 4 + i * 8)
        if offset < 0 or length < 0 or offset + length > len(data):
            raise ValueError(f"invalid BSP lump {i}")
        lumps.append(data[offset:offset + length])
    planes = records(lumps[1], "<4fi")
    nodes = records(lumps[5], "<ihh6hHH")
    leaves = records(lumps[10], "<ii6hHH4B")
    models = records(lumps[14], "<9f7i")
    if not models:
        raise ValueError("missing world model")
    headnode, visleafs = models[0][9], models[0][13]
    if visleafs <= 0 or visleafs >= len(leaves):
        raise ValueError("invalid world visibility leaf count")

    def locate(point):
        index = headnode
        for _ in range(len(nodes) + 1):
            if index < 0:
                leaf = -index - 1
                if leaf == 0 or leaf >= len(leaves) or leaves[leaf][0] == -2:
                    raise ValueError(f"point {point} is in solid or outside the world")
                return leaf
            if index >= len(nodes):
                raise ValueError("invalid BSP node index")
            node = nodes[index]
            if node[0] < 0 or node[0] >= len(planes):
                raise ValueError("invalid BSP plane index")
            plane = planes[node[0]]
            distance = sum(plane[i] * point[i] for i in range(3)) - plane[3]
            index = node[1] if distance >= 0 else node[2]
        raise ValueError("cyclic BSP nodes")

    def visible(source, target):
        offset = leaves[source][1]
        if offset < 0:
            raise ValueError("source leaf has no VIS data; recompile with VIS enabled")
        vis = lumps[4]
        decoded = bytearray()
        length = (visleafs + 7) // 8
        while len(decoded) < length:
            if offset >= len(vis):
                raise ValueError("truncated VIS data")
            value = vis[offset]
            offset += 1
            if value:
                decoded.append(value)
            else:
                if offset >= len(vis) or vis[offset] == 0:
                    raise ValueError("invalid VIS run")
                decoded.extend(bytes(vis[offset]))
                offset += 1
        if target > visleafs:
            raise ValueError("target leaf is outside world VIS data")
        bit = target - 1
        return bool(decoded[bit // 8] & (1 << (bit % 8)))

    a, b = locate(first), locate(second)
    return a, b, visible(a, b), visible(b, a)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bsp", type=Path)
    parser.add_argument("--from", dest="first", type=float, nargs=3, required=True, metavar=("X", "Y", "Z"))
    parser.add_argument("--to", dest="second", type=float, nargs=3, required=True, metavar=("X", "Y", "Z"))
    args = parser.parse_args()
    try:
        a, b, forward, backward = inspect(args.bsp, args.first, args.second)
    except (ValueError, OSError, struct.error) as error:
        parser.exit(1, f"PVS inspection failed: {error}\n")
    print(f"A: leaf {a} {args.first}; B: leaf {b} {args.second}")
    print(f"A -> B: {'visible' if forward else 'not visible'}")
    print(f"B -> A: {'visible' if backward else 'not visible'}")


if __name__ == "__main__":
    main()
