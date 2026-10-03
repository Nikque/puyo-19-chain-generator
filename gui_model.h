#pragma once
// The CLI and GUI compile the same engine, including its route verifier.
#define PUYO_GENERATOR_NO_MAIN
#include "random_19_chain.cpp"
#include <iomanip>

namespace puyo_gui {
inline GeneratorConfig defaults() {
    return {20261004, 4, 48, 1000, 2, 100, 0, 0, 19};
}
inline std::string configText(const GeneratorConfig& c) {
    std::ostringstream o;
    o << "[Generator]\n"
      << "target_chain = " << c.targetChain << '\n'
      << "target_success_count = " << c.targetSuccessCount << '\n'
      << "initial_seed = " << c.initialSeed << '\n'
      << "color_count = " << c.colorCount << '\n'
      << "beam_width = " << c.beamWidth << '\n'
      << "candidates_per_parent = " << c.candidatesPerParent << '\n'
      << "restarts = " << c.restarts << '\n'
      << "min_extra_puyos = " << c.minExtraPuyos << '\n'
      << "max_extra_puyos = " << c.maxExtraPuyos << '\n';
    return o.str();
}
inline void writeConfig(const std::filesystem::path& path, const GeneratorConfig& c) {
    std::ofstream f(path, std::ios::binary);
    f << configText(c); f.flush();
    if (!f) throw std::runtime_error("設定を保存できません: " + pathToUtf8(path));
}
// No floating point conversion, including values above 2^53.
inline GeneratorConfig parseInputs(const std::array<std::string,9>& t) {
    for (const auto& s : t)
        if (s.empty() || s.find_first_not_of("0123456789") != std::string::npos)
            throw std::runtime_error("設定は半角の10進整数で入力してください。");
    GeneratorConfig c;
    c.targetChain=parsePositiveInt(t[0],"target_chain");
    c.targetSuccessCount=parsePositiveInt(t[1],"target_success_count");
    c.initialSeed=parseSeed(t[2]);
    c.colorCount=parsePositiveInt(t[3],"color_count");
    c.minExtraPuyos=t[4]=="0"?0:parsePositiveInt(t[4],"min_extra_puyos");
    c.maxExtraPuyos=t[5]=="0"?0:parsePositiveInt(t[5],"max_extra_puyos");
    c.beamWidth=parsePositiveInt(t[6],"beam_width");
    c.candidatesPerParent=parsePositiveInt(t[7],"candidates_per_parent");
    c.restarts=parsePositiveInt(t[8],"restarts");
    if(c.targetChain<1 || c.targetChain>19) throw std::runtime_error("連鎖数は1〜19です。");
    if(c.colorCount!=4 && c.colorCount!=5) throw std::runtime_error("色数は4または5です。");
    if(c.minExtraPuyos>c.maxExtraPuyos || c.maxExtraPuyos>78-4*c.targetChain)
        throw std::runtime_error("余剰は 0 ≤ 下限 ≤ 上限 ≤ 78−4×連鎖数 です。");
    return c;
}

struct Frame {
    Field field;
    std::vector<Point> marked;
    std::string label;
    int pair=0, chain=0;
    int score=0;
};
// Display the engine's erased coordinates without applying its gravity yet.
// Zero cells retain all surviving puyos' original heights in this frame only.
inline Field afterClear(Field field,const WaveInfo& wave) {
    for(const auto& group:wave.groups)for(auto [x,y]:group)field.col[x][y]=0;
    return field;
}
inline std::vector<Frame> timeline(const Solution& s) {
    if(!verifyBuildSequence(s)) throw std::runtime_error("構築経路の検証に失敗しました。");
    std::vector<Frame> frames{{{}, {}, "開始前", 0, 0}};
    Field built;
    for(size_t i=0;i<s.placementSequence.size();++i) {
        const auto& d=s.placementSequence[i];
        Point p[]{d.a,d.b}; Cell colors[]{d.colorA,d.colorB};
        if(p[0].first==p[1].first && p[0].second>p[1].second) {
            std::swap(p[0],p[1]); std::swap(colors[0],colors[1]);
        }
        for(int k=0;k<2;++k) built.col[p[k].first].push_back(colors[k]);
        frames.push_back({built, {d.a,d.b}, std::to_string(i+1)+"組目を配置", int(i+1), 0});
        if(d.setupClear) {
            WaveInfo w; Field after=built; clearAndDrop(after,&w);
            frames.push_back({afterClear(built,w),{},"準備: 5個消し後・落下前（本体に含めない）",int(i+1),0});
            built=after;
            frames.push_back({built,{},"準備落下後",int(i+1),0});
        }
    }
    frames.back().marked.assign(s.trigger.begin(),s.trigger.end());
    frames.back().label="発火直前: "+std::to_string(s.targetChain)+"連鎖";
    for(int n=1;;++n) {
        Field after=built; WaveInfo w;
        if(!clearAndDrop(after,&w)) break;
        frames.push_back({afterClear(built,w),{},"本体 "+std::to_string(n)+"連鎖目: 消去後・落下前",int(s.placementSequence.size()),n});
        built=after;
        frames.push_back({built,{},"本体 "+std::to_string(n)+"連鎖目: 落下後",int(s.placementSequence.size()),n});
    }
    frames.back().label="連鎖終了: 余剰が残る";
    return frames;
}
inline size_t ignitionFrame(const Solution& s) {
    return s.placementSequence.size()+2*size_t(s.placementSequence.size()>2 && s.placementSequence[2].setupClear!=0);
}
struct Record { Solution solution; uint64_t seed=0; std::filesystem::path path; };
// Versioned, bounded archive; controls and uint64 seed round-trip exactly.
inline void saveRecord(const std::filesystem::path& path,const Solution& s,uint64_t seed) {
    std::ofstream o(path,std::ios::binary);
    o << "PUYO_GUI 1\n" << seed << ' ' << s.targetChain << '\n';
    for(const auto& c:s.field.col) {
        o << c.size(); for(Cell v:c) o << ' ' << int(v); o << '\n';
    }
    for(Point p:s.trigger) o << p.first << ' ' << p.second << '\n';
    o << s.placementSequence.size() << '\n';
    for(const auto& d:s.placementSequence)
        o << d.a.first << ' ' << d.a.second << ' ' << d.b.first << ' ' << d.b.second
          << ' ' << int(d.colorA) << ' ' << int(d.colorB) << ' ' << d.orientation
          << ' ' << d.setupClear << ' ' << std::quoted(d.controls) << '\n';
    o.flush(); if(!o) throw std::runtime_error("結果を保存できません: "+pathToUtf8(path));
}
inline Record loadRecord(const std::filesystem::path& path) {
    if(std::filesystem::file_size(path)>65536) throw std::runtime_error("結果ファイルが大きすぎます。");
    std::ifstream in(path,std::ios::binary); std::string magic; int version;
    Record r; r.path=path;
    if(!(in>>magic>>version) || magic!="PUYO_GUI" || version!=1) throw std::runtime_error("対応する.puyo形式ではありません。");
    std::string seed; in>>seed>>r.solution.targetChain; r.seed=parseSeed(seed);
    if(r.solution.targetChain<1 || r.solution.targetChain>19) throw std::runtime_error("不正な連鎖数です。");
    for(auto& c:r.solution.field.col) {
        int n; if(!(in>>n) || n<0 || n>13) throw std::runtime_error("不正な盤面です。");
        for(int i=0;i<n;++i) {int v; if(!(in>>v) || v<1 || v>5) throw std::runtime_error("不正な色です。"); c.push_back(Cell(v));}
    }
    auto point=[&](Point& p) {if(!(in>>p.first>>p.second) || p.first<0 || p.first>=6 || p.second<0 || p.second>=13) throw std::runtime_error("不正な座標です。");};
    for(auto& p:r.solution.trigger) point(p);
    int count; if(!(in>>count) || count<2 || count>41) throw std::runtime_error("不正な組数です。");
    for(int i=0;i<count;++i) {
        Domino d{}; int a,b; point(d.a); point(d.b);
        if(!(in>>a>>b>>d.orientation>>d.setupClear>>std::quoted(d.controls)) || a<1 || a>5 || b<1 || b>5 ||
           (d.orientation!='V' && d.orientation!='H') || (d.setupClear!=0 && d.setupClear!=5) || d.controls.size()>200)
            throw std::runtime_error("不正な構築手順です。");
        d.colorA=Cell(a); d.colorB=Cell(b); r.solution.placementSequence.push_back(d);
    }
    std::string tail; if(in>>tail) throw std::runtime_error("結果ファイルに余分なデータがあります。");
    if(!verifyBuildSequence(r.solution)) throw std::runtime_error("結果ファイルのエンジン検証に失敗しました。");
    return r;
}
}
