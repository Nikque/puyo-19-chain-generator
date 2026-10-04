"""Independent cell-grid verifier of the generator's output (Python 3, stdlib).

Usage: python tests/verify_output.py generator.log [--allow-extra]

It consumes the generator's stdout, not its internal C++ state, and checks the
definition of a valid result directly, one cell at a time. The C++ search skips
most of these checks because they follow from the way it builds a board; this
program exists to catch a hole in that reasoning.

A result is valid when
  1. no column has a gap and nothing sits in the 14th row;
  2. firing it clears exactly `chains` waves, each one group of exactly four,
     and the first wave is the printed trigger;
  3. after the chain the cell where pairs appear (column 3, row 12) is empty;
  4. at least one "last pair" is printed, and for every printed pair:
     a. it is the two topmost puyos of one column, or the topmost puyos of two
        neighbouring columns;
     b. without the pair nothing clears;
     c. without the pair the cell where pairs appear is empty;
     d. the pair can travel to its place (rule below);
  5. the printed pairs are exactly all pairs that satisfy 4a-4d.
The board itself may cover the cell where pairs appear: the last pair is
allowed to choke as long as the chain frees that cell again (rule 3).
"""
import argparse
import re

OFFSETS = [(0, 1), (1, 0), (0, -1), (-1, 0)]


def wave(board):
    """One clearing wave. Returns (groups of four or more, board after gravity)."""
    visited, erase = set(), set()
    groups = []
    for cell, color in board.items():
        if cell[1] >= 12 or cell in visited:
            continue
        group, queue = set(), [cell]
        visited.add(cell)
        while queue:
            x, y = queue.pop()
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


def heights(board):
    return [sum(1 for (x, _) in board if x == column) for column in range(6)]


MAX_HOP = 2  # lower columns the lifted axis puyo can cross before it must land


def column_reachable(h, target):
    """Can the falling pair travel from column 3 to `target`?

    11 or lower: always passable. 13: never. 12: only while the axis puyo is
    lifted to row 13 (wall crossing). It is lifted by a quick turn at the
    spawn (both neighbours 12+), by a floor kick on a column exactly 11 high,
    or by standing on a 12-high column; once lifted it may hop over up to
    MAX_HOP lower columns. The floor kick may be made on the other side of the
    spawn column; the columns crossed on the way back count as hopped.
    """
    if h[2] >= 12:
        return False
    step = -1 if target < 2 else 1
    fly = -1  # lower columns that may still be crossed in the air; -1 = not lifted
    if (h[1] >= 12 and h[3] >= 12) or h[2] == 11:
        fly = MAX_HOP
    else:
        x = 2 - step
        for hopped in range(1, MAX_HOP + 1):
            if not 0 <= x < 6:
                break
            if h[x] == 11:
                fly = MAX_HOP - hopped
            if h[x] >= 11:
                break
            x -= step
    x = 2
    while x != target:
        x += step
        if h[x] >= 13:
            return False
        if h[x] == 12:
            if fly < 0:
                return False
            fly = MAX_HOP
        elif h[x] == 11:
            fly = MAX_HOP
        elif fly >= 0:
            fly -= 1
    return True


def pair_candidates(board):
    """Every pair of topmost puyos: (kind, frozenset of two cells)."""
    h = heights(board)
    for x in range(6):
        if h[x] >= 2:
            yield "V", frozenset({(x, h[x] - 1), (x, h[x] - 2)})
    for x in range(5):
        if h[x] and h[x + 1]:
            yield "H", frozenset({(x, h[x] - 1), (x + 1, h[x + 1] - 1)})


def pair_is_valid(board, kind, cells):
    before = {cell: color for cell, color in board.items() if cell not in cells}
    if wave(before)[0]:
        return False
    if (2, 11) in before:
        return False
    h = heights(before)
    columns = sorted(x for x, _ in cells)
    if kind == "V":
        return h[columns[0]] <= 11 and column_reachable(h, columns[0])
    far = columns[1] if columns[0] >= 2 else columns[0]
    return h[columns[0]] <= 12 and h[columns[1]] <= 12 and column_reachable(h, far)


CELL = re.compile(r"\((\d+),(\d+)\)")
OPTION = re.compile(r"([VH])\((\d+),(\d+)\)\((\d+),(\d+)\)")


def verify(text, allow_extra=False):
    counts = {}
    sections = text.split("field (top row first; columns left to right):\n")
    assert len(sections) > 1, "no solutions in log"
    for index, chunk in enumerate(sections[1:], 1):
        chain_headers = re.findall(r"chains=(\d+)", sections[index - 1])
        target = int(chain_headers[-1])
        lines = chunk.splitlines()
        rows = [line.split() for line in lines[:13]]
        assert all(len(row) == 6 for row in rows)
        board = {(x, 12 - y): c for y, row in enumerate(rows)
                 for x, c in enumerate(row) if c != "."}
        assert all(c in "RGBYP" for c in board.values())
        assert 4 * target <= len(board) <= 78
        if not allow_extra:
            assert len(board) == 4 * target
        # 1. compact columns (row 14 cannot be printed at all)
        h = heights(board)
        assert all((x, y) in board for x in range(6) for y in range(h[x])), "hole in a column"

        trigger_line = next(line for line in lines if line.startswith("trigger cells"))
        trigger = {(int(x) - 1, int(y) - 1) for x, y in CELL.findall(trigger_line)}
        assert len(trigger) == 4
        option_line = next(line for line in lines if line.startswith("last pair options"))
        printed = {(kind, frozenset({(int(a) - 1, int(b) - 1), (int(c) - 1, int(d) - 1)}))
                   for kind, a, b, c, d in OPTION.findall(option_line.split(":", 1)[1])}

        # 4 and 5: the printed last pairs are exactly the valid ones
        valid = {(kind, cells) for kind, cells in pair_candidates(board) if pair_is_valid(board, kind, cells)}
        assert printed, "no last pair"
        assert printed <= set(pair_candidates(board)), "a printed pair is not made of topmost puyos"
        assert printed <= valid, "a printed last pair is not valid"
        assert valid <= printed, "a valid last pair was not found"

        # 2 and 3: the chain
        built, waves = dict(board), 0
        while True:
            groups, after = wave(built)
            if not groups:
                break
            assert len(groups) == 1 and len(groups[0]) == 4, "a wave is not one group of four"
            if waves == 0:
                assert groups[0] == trigger, "first wave is not the trigger"
            built = after
            waves += 1
        assert waves == target
        assert len(built) == len(board) - 4 * target
        assert (2, 11) not in built, "choked after the chain"
        key = f"{target}-chain/{len(board)}-cell"
        counts[key] = counts.get(key, 0) + 1
    return counts


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("log")
    parser.add_argument("--allow-extra", action="store_true")
    args = parser.parse_args()
    with open(args.log, encoding="utf-8") as source:
        print("Verified:", verify(source.read(), args.allow_extra))
