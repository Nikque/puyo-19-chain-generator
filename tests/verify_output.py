"""Independent cell-grid simulator and emitted-route verifier (Python 3, stdlib).

Usage: python tests/verify_output.py generator.log [--allow-extra]
Consumes the generator's stdout, not its internal C++ state.
"""
import argparse
import re
from collections import deque

OFFSETS = [(0, 1), (1, 0), (0, -1), (-1, 0)]


def wave(board):
    visited, erase = set(), set()
    groups = []
    for cell, color in board.items():
        if cell[1] >= 12 or cell in visited:
            continue
        group, queue = set(), deque([cell])
        visited.add(cell)
        while queue:
            x, y = queue.popleft()
            group.add((x, y))
            for dx, dy in OFFSETS:
                q = x + dx, y + dy
                if q[1] < 12 and board.get(q) == color and q not in visited:
                    visited.add(q)
                    queue.append(q)
        if len(group) >= 4:
            groups.append(group)
            erase.update(group)
    after = {}
    for x in range(6):
        colors = [v for (cx, y), v in sorted(board.items(), key=lambda kv: kv[0][1])
                  if cx == x and (cx, y) not in erase]
        after.update({(x, y): c for y, c in enumerate(colors)})
    return groups, after


def fits(board, pose):
    x, y, r = pose
    dx, dy = OFFSETS[r]
    child = x + dx, y + dy
    return (0 <= x < 6 and 0 <= y < 13 and 0 <= child[0] < 6 and
            0 <= child[1] < 14 and (x, y) not in board and child not in board)


def rotate(board, pose, direction):
    x, y, r = pose
    nr = (r + direction) % 4
    q = x, y, nr
    if fits(board, q):
        return q, False
    dx, dy = OFFSETS[nr]
    if dx and fits(board, (x - dx, y, nr)):
        return (x - dx, y, nr), False
    if dy == -1 and fits(board, (x, y + 1, nr)):
        return (x, y + 1, nr), False
    if r in (0, 2) and not fits(board, (x, y, 1)) and not fits(board, (x, y, 3)):
        q = x, y + OFFSETS[r][1], (r + 2) % 4
        if fits(board, q):
            return q, True
    raise AssertionError(f"illegal rotation at {pose}")


def replay(board, colors, targets, controls):
    pose = 2, 11, 0
    assert fits(board, pose), "blocked spawn"
    i = 0
    while i < len(controls):
        c = controls[i]
        x, y, r = pose
        if c in "LRD":
            pose = x + (c == "R") - (c == "L"), y - (c == "D"), r
            assert fits(board, pose), "illegal translation or row-13 wall crossing"
        else:
            assert c in "AB"
            pose, quick = rotate(board, pose, 1 if c == "A" else -1)
            if quick:
                assert controls[i:i + 2] == c * 2, "quick turn needs two presses"
                i += 1
        i += 1
    x, y, r = pose
    assert not fits(board, (x, y - 1, r)), "pair not grounded"
    dx, dy = OFFSETS[r]
    positions = [(x, y), (x + dx, y + dy)]
    # Independent gravity after locking: bottom piece first for a vertical pair.
    for k in sorted(range(2), key=lambda k: positions[k][1]):
        cx, cy = positions[k]
        while cy > 0 and (cx, cy - 1) not in board:
            cy -= 1
        assert (cx, cy) == targets[k], "emitted axis/child coordinates don't match route"
        assert cy < 13, "persistent row-14 placement"
        board[cx, cy] = colors[k]


PAIR = re.compile(r"^(\d+): [VH] ([RGBYP]{2}) at \((\d+),(\d+)\) and \((\d+),(\d+)\) axis/child; controls=([LRDAB]*)(?:; setup_clear=(\d+))?$", re.M)


def verify(text, allow_extra=False):
    counts = {}
    sections = text.split("field (top row first; columns left to right):\n")
    assert len(sections) > 1, "no solutions in log"
    for index, chunk in enumerate(sections[1:], 1):
        chain_headers = re.findall(r"chains=(\d+)", sections[index - 1])
        target = int(chain_headers[-1]) if chain_headers else 19
        rows = [line.split() for line in chunk.splitlines()[:13]]
        assert all(len(row) == 6 for row in rows)
        expected = {(x, 12 - y): c for y, row in enumerate(rows)
                    for x, c in enumerate(row) if c != "."}
        assert 4 * target <= len(expected) <= 78
        if not allow_extra:
            assert len(expected) == 4 * target
        pairs = PAIR.findall(chunk)
        assert len(pairs) * 2 == len(expected) + (5 if len(expected) % 2 else 0)
        built, opening, setups = {}, set(), 0
        for index, pair in enumerate(pairs):
            n, colors, x, y, xx, yy, controls, setup = pair
            assert int(n) == index + 1
            if index < 3:
                opening.update(colors)
            replay(built, colors, [(int(x) - 1, int(y) - 1), (int(xx) - 1, int(yy) - 1)], controls)
            if setup:
                assert index == 2 and int(setup) == 5
                groups, built = wave(built)
                assert len(groups) == 1 and len(groups[0]) == 5
                assert len(built) == 1 and not wave(built)[0]
                setups += 1
            elif index + 1 < len(pairs):
                assert (2, 11) not in built, "death before ignition"
                assert not wave(built)[0], "chain fires before final pair"
        assert len(opening) <= 3
        assert setups == len(expected) % 2
        assert built == expected
        waves = 0
        while True:
            groups, after = wave(built)
            if not groups:
                break
            assert len(groups) == 1 and len(groups[0]) == 4
            built = after
            waves += 1
        assert waves == target
        assert len(built) == len(expected) - 4 * target
        assert (2, 11) not in built, "death after the final chain"
        key = f"{target}-chain/{len(expected)}-cell"
        counts[key] = counts.get(key, 0) + 1
    return counts


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("log")
    parser.add_argument("--allow-extra", action="store_true")
    args = parser.parse_args()
    with open(args.log, encoding="utf-8") as source:
        print("Verified:", verify(source.read(), args.allow_extra))
