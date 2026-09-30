#define main generatorProgramMain
#include "../random_19_chain.cpp"
#undef main
#include "reference_predecessors.h"

static void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
static std::set<std::string> keys(const std::vector<Candidate>& values) {
    std::set<std::string> result;
    for (const auto& v:values) result.insert(v.field.key());
    check(result.size()==values.size(), "duplicate predecessors");
    return result;
}
int main() {
    try {
        auto shapes=makeTetrominoShapes();
        check(shapes.size()==19, "all fixed tetrominoes");
        std::mt19937_64 rng(42);
        // Compare optimized geometric validation against the original full
        // simulator on arbitrary boards, hidden rows, and inverse-chain boards.
        for (int i=0;i<160;++i) {
            Field f;
            int colors=i%2?4:5;
            if (i<80) {
                for (auto& c:f.col) {
                    int h=int(rng()%14);
                    for (int y=0;y<h;++y) c.push_back(Cell(1+rng()%colors));
                }
            } else {
                if (i%3==0) {f.col[0]={1};f.col[5]={2};}
                for (int depth=0;depth<i%19;++depth) {
                    auto options=predecessors(f,shapes,colors);
                    if (options.empty()) break;
                    f=options[rng()%options.size()].field;
                }
            }
            check(keys(predecessors(f,shapes,colors))==keys(referencePredecessors(f,shapes,colors)),
                  "optimized predecessors changed the solution set");
            for (size_t limit : {size_t(0), size_t(2), size_t(8)}) {
                std::mt19937_64 fullRng(i), sampledRng(i);
                auto full=predecessors(f,shapes,colors);
                std::shuffle(full.begin(),full.end(),fullRng);
                if (limit && full.size()>limit) full.resize(limit);
                auto sampled=predecessors(f,shapes,colors,&sampledRng,limit);
                check(full.size()==sampled.size(),"sampling changed candidate count");
                for (size_t j=0;j<full.size();++j)
                    check(full[j].field.key()==sampled[j].field.key() &&
                          full[j].trigger==sampled[j].trigger,"sampling changed candidate order");
                check(fullRng==sampledRng,"sampling changed random state");
            }
        }
        Field ghost;ghost.col[0]={2,2,2,2,3,4,3,4,3,1,1,1,1};
        check(chainCount(ghost)==2,"row 13 must drop but not connect before dropping");
        Field noClear;noClear.col[0]={2,3,2,3,2,3,2,3,2,1,1,1,1};
        check(clearAndDrop(noClear)==0,"row 13 must not count toward a clear");

        Field blocked;blocked.col[3].assign(13,1);
        Domino right{{5,0},{5,1},1,2,'V',{}};
        check(!findPlacementRoute(blocked,right),"must not cross a row-13 wall to column 6");
        Field leftWall;leftWall.col[1].assign(13,1);
        Domino left{{0,0},{0,1},1,2,'V',{}};
        check(!findPlacementRoute(leftWall,left),"must not cross a row-13 wall to column 1");
        Field dead;dead.col[2].assign(12,1);
        check(!findPlacementRoute(dead,right),"occupied death cell must prevent spawning");
        check(!pairFits(Field{}, {2,13,2}),"axis cannot enter row 14");

        Field pass;pass.col[1].assign(12,1);pass.col[2].assign(11,2);pass.col[3].assign(12,1);
        check(findPlacementRoute(pass,right),"quick turn should cross a row-12 wall");
        check(replayPlacement(pass,right),"replay the wall-crossing route");
        check(right.controls.find("AA")!=std::string::npos || right.controls.find("BB")!=std::string::npos,
              "quick turn must emit two presses");
        Field low;low.col[0]={1,2,3};
        Domino split{{0,3},{1,0},2,1,'H',{}};
        check(findPlacementRoute(low,split),"unequal-height horizontal placement (chigiri)");
        check(replayPlacement(low,split),"replay chigiri");
        split.controls="invalid";
        check(!replayPlacement(low,split),"reject corrupted route");

        // Every possible residual column/color supports the five-clear parity
        // preamble, using no row-14 placement or premature four-clear.
        for (int x=0;x<W;++x) for (int color=1;color<=5;++color) {
            Field residual;residual.col[x]={Cell(color)};
            std::vector<Domino> preamble;
            check(makeOddPreamble(residual,preamble),"odd parity preamble");
            check(preamble.size()==3 && preamble.back().setupClear==5,"explicit setup clear");
            for (const auto& d:preamble)
                check(d.a.second<H && d.b.second<H,"preamble must stay below row 14");
        }

        // Peeling a field already clearing before the final pair must fail.
        Field early;early.col[0]={1,1,1,1};early.col[5]={2,3};
        std::array<Point,4> trigger{{{0,0},{0,1},{0,2},{0,3}}};
        int budget=2000;std::unordered_set<std::string> memo;std::vector<Domino> sequence;
        // Force the alleged final trigger to be disjoint from the existing clear.
        std::array<Point,4> fake{{{5,0},{5,1},{4,0},{4,1}}};
        check(!peelToBuildSequence(early,fake,true,rng,budget,memo,sequence),"reject an early clear");
        std::cout << "All regression tests passed (160 predecessor equivalence cases).\n";
        return 0;
    } catch (const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
