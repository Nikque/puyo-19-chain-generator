// Original exhaustive implementation, used only for equivalence tests.
static std::vector<Candidate> referencePredecessors(
    const Field& post,
    const std::vector<Shape>& shapes,
    int colorCount)
{
    std::vector<Candidate> result;
    std::unordered_set<std::string> seenFields;

    for (const Shape& shape : shapes) {
        int maxX = 0;
        int maxY = 0;
        for (const Point p : shape) {
            maxX = std::max(maxX, p.first);
            maxY = std::max(maxY, p.second);
        }
        const int shapeW = maxX + 1;

        // 各相対列に入るテトロミノぷよの相対y座標。
        std::array<std::vector<int>, W> groupYs{};
        for (const Point p : shape) groupYs[p.first].push_back(p.second);

        bool columnsAreContiguous = true;
        for (int dx = 0; dx < shapeW; ++dx) {
            auto& ys = groupYs[dx];
            if (ys.empty()) continue;
            std::sort(ys.begin(), ys.end());
            for (size_t i = 1; i < ys.size(); ++i) {
                if (ys[i] != ys[i - 1] + 1) columnsAreContiguous = false;
            }
        }
        if (!columnsAreContiguous) continue;

        for (int x0 = 0; x0 + shapeW <= W; ++x0) {
            for (int y0 = 0; y0 + maxY < CLEAR_H; ++y0) {
                std::array<int, W> slots{};
                std::array<int, W> counts{};
                bool valid = true;

                for (int dx = 0; dx < shapeW; ++dx) {
                    const auto& ys = groupYs[dx];
                    if (ys.empty()) continue;

                    const int x = x0 + dx;
                    const int slot = y0 + ys.front();
                    const int count = static_cast<int>(ys.size());

                    if (slot > static_cast<int>(post.col[x].size()) ||
                        static_cast<int>(post.col[x].size()) + count > H) {
                        valid = false;
                        break;
                    }
                    slots[x] = slot;
                    counts[x] = count;
                }
                if (!valid) continue;

                for (int color = 1; color <= colorCount; ++color) {
                    Field pre;

                    for (int x = 0; x < W; ++x) {
                        if (counts[x] == 0) {
                            pre.col[x] = post.col[x];
                            continue;
                        }

                        const int slot = slots[x];
                        pre.col[x].insert(pre.col[x].end(),
                            post.col[x].begin(), post.col[x].begin() + slot);
                        for (int n = 0; n < counts[x]; ++n) {
                            pre.col[x].push_back(static_cast<Cell>(color));
                        }
                        pre.col[x].insert(pre.col[x].end(),
                            post.col[x].begin() + slot, post.col[x].end());
                    }

                    std::array<Point, 4> trigger{};
                    for (size_t i = 0; i < shape.size(); ++i) {
                        trigger[i] = {x0 + shape[i].first, y0 + shape[i].second};
                    }

                    // 逆操作の確認: 1波でちょうどこの4個が消え、postに戻ること。
                    Field after = pre;
                    WaveInfo wave;
                    if (clearAndDrop(after, &wave) != 4 ||
                        wave.groups.size() != 1 ||
                        wave.groups.front().size() != 4) continue;

                    const std::vector<Point>& removed = wave.groups.front();
                    std::vector<Point> expected(trigger.begin(), trigger.end());
                    std::sort(expected.begin(), expected.end());
                    if (removed != expected || !(after == post)) continue;

                    const std::string key = pre.key();
                    if (seenFields.insert(key).second) {
                        result.push_back(Candidate{std::move(pre), trigger});
                    }
                }
            }
        }
    }

    return result;
}
