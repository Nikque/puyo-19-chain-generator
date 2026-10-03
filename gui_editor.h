#pragma once
#include "gui_model.h"

namespace puyo_gui {
// Separate from the generator's compact columns: editing permits empty cells,
// floating cells and fixed walls. Never pass these special types to the search.
enum Piece : Cell { Empty=0, Red=1, Green=2, Blue=3, Yellow=4, Purple=5,
    Garbage=6, PointPuyo=7, Hard=8, Iron=9, Wall=10 };
inline constexpr std::array<const char*,11> pieceNames{
    "消す（空）","赤 R","緑 G","青 B","黄 Y","紫 P","おじゃま ○","得点 $","かた H","鉄 I","壁 #"};
inline constexpr wchar_t pieceLetters[]=L" RGBYP○$HI#";
struct Board {
    std::array<std::array<Cell,13>,6> cells{}; // [x][bottom-up y]
    bool operator==(const Board&) const = default;
};
inline Board boardFromField(const Field& field) {
    Board b;
    for(int x=0;x<6;++x){if(field.col[x].size()>13)throw std::runtime_error("14段目は編集できません。");for(size_t y=0;y<field.col[x].size();++y){if(field.col[x][y]>Wall)throw std::runtime_error("不正なぷよです。");b.cells[x][y]=field.col[x][y];}}
    return b;
}
inline Field fieldFromBoard(const Board& b) {
    Field f;
    for(int x=0;x<6;++x){f.col[x].assign(b.cells[x].begin(),b.cells[x].end());while(!f.col[x].empty()&&!f.col[x].back())f.col[x].pop_back();}
    return f;
}
inline bool settle(Board& b) {
    const Board before=b;
    for(int x=0;x<6;++x){int dst=0;for(int y=0;y<13;++y){Cell value=b.cells[x][y];if(value==Wall){dst=y+1;continue;}if(value){b.cells[x][y]=Empty;b.cells[x][dst++]=value;}}}
    return b!=before;
}
struct EditorWave {std::vector<Point> erased,cracked;int score=0,colored=0;};
inline EditorWave editorClear(Board& b,int chain) {
    // Reuse the engine's matching and 13th-row exclusion. Specials are masked;
    // its compacted output is discarded because walls need segmented gravity.
    Field normal=fieldFromBoard(b);
    for(auto& col:normal.col)for(auto& cell:col)if(cell>5)cell=0;
    WaveInfo groups;clearAndDrop(normal,&groups);
    EditorWave wave;
    bool erase[6][13]{};int damage[6][13]{};
    std::array<bool,6> colors{};int combined=0;
    constexpr Point dirs[]={{1,0},{-1,0},{0,1},{0,-1}};
    for(const auto& group:groups.groups){
        int n=int(group.size());wave.colored+=n;combined+=n>=11?10:n>=5?n-3:0;
        for(auto [x,y]:group){colors[b.cells[x][y]]=true;erase[x][y]=true;for(auto [dx,dy]:dirs){int nx=x+dx,ny=y+dy;if(nx>=0&&nx<6&&ny>=0&&ny<12)++damage[nx][ny];}}
    }
    if(!wave.colored)return wave;
    int base=10*wave.colored,points=0;
    for(int x=0;x<6;++x)for(int y=0;y<12;++y)if(damage[x][y]){
        Cell cell=b.cells[x][y];
        if(cell==Garbage||cell==PointPuyo){erase[x][y]=true;if(cell==PointPuyo)points+=50;}
        else if(cell==Hard){if(damage[x][y]>=2){erase[x][y]=true;base+=60;}else{b.cells[x][y]=Garbage;wave.cracked.push_back({x,y});base+=10;}}
    }
    for(int x=0;x<6;++x)for(int y=0;y<12;++y)if(erase[x][y]){wave.erased.push_back({x,y});b.cells[x][y]=Empty;}
    constexpr int chainBonus[]={0,0,8,16,32,64,96,128,160,192,224,256,288,320,352,384,416,448,480,512};
    constexpr int colorBonus[]={0,0,3,6,12,24};
    int colorCount=int(std::count(colors.begin(),colors.end(),true));
    wave.score=base*std::max(1,chainBonus[std::clamp(chain,1,19)]+colorBonus[colorCount]+combined)+points;
    return wave;
}
inline std::vector<Frame> editorTimeline(Board b) {
    std::vector<Frame> frames{{fieldFromBoard(b),{},"編集盤面（開始前）",0,0,0}};
    if(settle(b))frames.push_back({fieldFromBoard(b),{},"初期落下後",0,0,0});
    int score=0;
    for(int n=1;n<=19;++n){auto wave=editorClear(b,n);if(!wave.colored)break;
        score+=wave.score;
        frames.push_back({fieldFromBoard(b),{},std::to_string(n)+"連鎖: 消去・変化後",0,n,score});
        settle(b);frames.push_back({fieldFromBoard(b),{},std::to_string(n)+"連鎖: 落下後",0,n,score});
    }
    frames.back().label+=" / シミュレーション終了";
    return frames;
}
struct Editor {
    Board board{};
    std::vector<Board> undo,redo;
    void replace(const Board& value){if(board==value)return;undo.push_back(board);if(undo.size()>256)undo.erase(undo.begin());redo.clear();board=value;}
    void back(){if(undo.empty())return;redo.push_back(board);board=undo.back();undo.pop_back();}
    void forward(){if(redo.empty())return;undo.push_back(board);board=redo.back();redo.pop_back();}
};
inline void saveBoard(const std::filesystem::path& path,const Board& b) {
    std::ofstream out(path,std::ios::binary);out<<"PUYO_BOARD 1\n";
    for(int y=12;y>=0;--y){for(int x=0;x<6;++x)out<<int(b.cells[x][y])<<(x==5?'\n':' ');}
    out.flush();if(!out)throw std::runtime_error("編集盤面を保存できません。");
}
inline Board loadBoard(const std::filesystem::path& path) {
    if(std::filesystem::file_size(path)>4096)throw std::runtime_error("盤面ファイルが大きすぎます。");
    std::ifstream in(path,std::ios::binary);std::string magic;int version;
    if(!(in>>magic>>version)||magic!="PUYO_BOARD"||version!=1)throw std::runtime_error("対応する編集盤面形式ではありません。");
    Board b;for(int y=12;y>=0;--y)for(int x=0;x<6;++x){int v;if(!(in>>v)||v<0||v>10)throw std::runtime_error("不正なぷよ種別です。");b.cells[x][y]=Cell(v);}
    std::string extra;if(in>>extra)throw std::runtime_error("盤面ファイルに余分なデータがあります。");return b;
}
}
