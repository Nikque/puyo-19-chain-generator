"""Independently replay every saved GUI construction, including partial runs."""
import pathlib
import sys
import re
from verify_output import verify, wave

def decode_url(url):
    if url.startswith("https://www.pndsng.com/puyo/index.html?"):
        code = url.split("?", 1)[1]
        tokens = re.findall(r"([a-f])(\d*)", code)
        assert "".join(a+b for a,b in tokens) == code
        cells = ["abecdf".index(a) for a,b in tokens for _ in range(int(b or 1))]
    else:
        prefixes = ("https://ishikawapuyo.net/simu/pe.html?", "https://www.puyop.com/s/")
        prefix = next(p for p in prefixes if url.startswith(p))
        alphabet = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.-"
        cells = [v for c in url[len(prefix):] for v in divmod(alphabet.index(c), 8)]
        cells = [0]*(78-len(cells)) + cells
    assert len(cells) == 78 and all(0 <= c <= 5 for c in cells)
    return cells

def archive_cells(path):
    columns = [list(map(int, line.split()))[1:] for line in path.read_text(encoding="utf-8").splitlines()[2:8]]
    return [columns[x][y] if y < len(columns[x]) else 0 for y in range(12, -1, -1) for x in range(6)]

root = pathlib.Path(sys.argv[1])
total = 0
formats = set()
for log in root.rglob("generator.log"):
    text = log.read_text(encoding="utf-8")
    archives = list(log.parent.glob("board-*.puyo"))
    if not archives:
        assert "field (top row first" not in text, log
        continue
    counts = verify(text, True)
    n = sum(counts.values())
    urls = (log.parent / "19chain_urls.txt").read_text(encoding="utf-8").splitlines()
    assert n == len(archives) == len(urls) == len(set(urls)), (log, counts)
    # Old retained runs predate the selector; their canonical URLs still verify.
    if (log.parent / "url-format.txt").exists():
        exported = (log.parent / "simulator_urls.txt").read_text(encoding="utf-8").splitlines()
        assert len(exported) == n
        format_id = (log.parent / "url-format.txt").read_text().strip()
        formats.add(format_id)
        prefixes = {"ishikawa":"https://ishikawapuyo.net/simu/pe.html?", "puyop":"https://www.puyop.com/s/", "mattulwan":"https://www.pndsng.com/puyo/index.html?"}
        for i, url in enumerate(exported, 1):
            assert url.startswith(prefixes[format_id])
            assert decode_url(url) == archive_cells(log.parent / f"board-{i}.puyo"), (log, i)
            assert decode_url(urls[i-1]) == decode_url(url)
    total += n
assert total >= 100, total
assert formats == {"ishikawa", "puyop", "mattulwan"}, formats
model_count = 0
for exported in pathlib.Path(sys.argv[2]).rglob("urls-*.txt"):
    assert decode_url(exported.read_text().strip()) == archive_cells(exported.parent / "盤面.puyo")
    model_count += 1
assert model_count == 36, model_count

# Verify displayed pre-gravity holes with the independent grid matcher, rather
# than C++ helper results. Retained original heights must survive the clear.
def transition(before, cleared, dropped):
    groups, after = wave(before)
    assert groups
    erased = set().union(*groups)
    expected = {pos: color for pos, color in before.items() if pos not in erased}
    assert cleared == expected, "post-clear frame must preserve survivor heights"
    assert dropped == after, "following frame must apply gravity"
    return len(erased)

timeline_count = 0
for path in pathlib.Path(sys.argv[2]).glob("*/timeline.txt"):
    lines = path.read_text().splitlines()
    count, ignition = map(int, lines[0].split())
    frames = []
    for line in lines[1:]:
        pair, chain, *cells = map(int, line.split())
        assert len(cells) == 78
        board = {(i % 6, 12 - i // 6): color for i, color in enumerate(cells) if color}
        frames.append((pair, chain, board))
    assert len(frames) == count
    target = frames[-1][1]
    assert len(frames) - ignition - 1 == target * 2
    for n in range(1, target + 1):
        i = ignition + 2*n - 1
        assert frames[i][1] == frames[i+1][1] == n
        transition(frames[i-1][2], frames[i][2], frames[i+1][2])
    for i in range(1, ignition):
        if frames[i][0] == frames[i-1][0] and frames[i+1][0] == frames[i][0]:
            assert frames[i][1] == frames[i+1][1] == 0
            assert transition(frames[i-1][2], frames[i][2], frames[i+1][2]) == 5
    timeline_count += 1
assert timeline_count == 12, timeline_count
print(f"Independently verified {total} GUI results (completed and cancelled runs)")
print(f"Also verified all {model_count} URL exports for 1/19 chains, 4/5 colors and 0/1/2 extra cells")
print(f"Verified post-clear holes and following gravity in all {timeline_count} displayed timelines, including preparation clears")
