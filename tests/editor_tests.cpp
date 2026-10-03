#include "../gui_urls.h"
using namespace puyo_gui;
static void check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
int wmain(int argc,wchar_t** argv){try{
    check(argc==2,"need test directory");std::filesystem::path root=argv[1];std::filesystem::create_directories(root);
    Board b;for(int x=0;x<4;++x)b.cells[x][0]=Red;
    b.cells[0][1]=Garbage;b.cells[1][1]=PointPuyo;b.cells[2][1]=Hard;b.cells[3][1]=Iron;b.cells[4][0]=Wall;
    auto wave=editorClear(b,1);check(wave.colored==4&&b.cells[0][1]==0&&b.cells[1][1]==0&&b.cells[2][1]==Garbage,"special adjacent clear and hard damage");check(b.cells[3][1]==Iron&&b.cells[4][0]==Wall&&wave.score==100,"iron/wall immune and point bonus");settle(b);check(b.cells[2][0]==Garbage&&b.cells[3][0]==Iron,"specials fall");
    b={};b.cells[0][0]=b.cells[1][0]=b.cells[0][1]=b.cells[0][2]=Red;b.cells[1][1]=PointPuyo;wave=editorClear(b,1);check(wave.score==90&&b.cells[1][1]==Empty,"point counted once from two directions");
    b={};b.cells[0][0]=b.cells[1][0]=b.cells[0][1]=b.cells[0][2]=Red;b.cells[1][1]=Hard;wave=editorClear(b,1);check(b.cells[1][1]==Empty&&wave.score==100,"hard cleared by two simultaneous directions");
    b={};for(int x=0;x<4;++x)for(int y=0;y<13;++y)b.cells[x][y]=y==12?Red:Iron;check(editorClear(b,1).colored==0,"row13 does not match");
    b={};for(int x=0;x<4;++x)b.cells[x][11]=Red;b.cells[0][12]=Garbage;wave=editorClear(b,1);check(b.cells[0][12]==Garbage,"row13 garbage not erased by adjacency");settle(b);check(b.cells[0][0]==Garbage,"row13 still falls");
    b={};b.cells[0][4]=Wall;b.cells[0][12]=Iron;b.cells[0][3]=PointPuyo;settle(b);check(b.cells[0][4]==Wall&&b.cells[0][5]==Iron&&b.cells[0][0]==PointPuyo,"wall divides column gravity");
    for(Cell kind:{Garbage,PointPuyo,Hard,Iron,Wall}){b={};for(int x=0;x<6;++x)b.cells[x][0]=kind;auto f=editorTimeline(b);check(f.back().chain==0&&boardFromField(f.back().field)==b,"specials never form color groups");}
    b={};for(int x=0;x<6;++x)b.cells[x][12]=Cell(5+x); // five specials plus purple, top row
    saveBoard(root/L"特殊😀.puyoboard",b);check(loadBoard(root/L"特殊😀.puyoboard")==b,"Unicode archive preserves holes and all types");
    for(int i=0;i<3;++i){bool rejected=false;try{simulatorUrl(fieldFromBoard(b),UrlFormat(i));}catch(...){rejected=true;}check(rejected==(i!=2),"unsupported point puyo URL rejected");}
    b.cells[2][12]=Empty; // remove point (top values: 5,6,7,8,9,10)
    check(simulatorUrl(fieldFromBoard(b),UrlFormat::Ishikawa).starts_with("https://ishikawapuyo.net/simu/pe.html?~560987"),"IPS long special mapping");
    check(simulatorUrl(fieldFromBoard(b),UrlFormat::PuyoPark).starts_with("https://www.puyop.com/s/=560789"),"PuyoPark long special mapping");
    Editor e;e.replace(b);e.back();check(e.board==Board{},"undo");e.forward();check(e.board==b,"redo");e.back();e.replace(Board{});check(!e.redo.empty(),"no-op retains redo");e.replace(b);check(e.redo.empty(),"new edit discards redo");
    std::mt19937 rng(20261004);
    for(int i=0;i<500;++i){b={};for(int x=0;x<6;++x)for(int y=0;y<13;++y)b.cells[x][y]=Cell(rng()%11);if(i<100)for(int x=0;x<4;++x)b.cells[x][0]=Red;
        auto frames=editorTimeline(b);auto name=std::to_string(i);saveBoard(root/(name+".puyoboard"),b);saveBoard(root/(name+".final"),boardFromField(frames.back().field));std::ofstream stats(root/(name+".stats"));stats<<frames.back().chain<<' '<<frames.back().score<<'\n';
        for(int format=0;format<3;++format)if(urlUnavailableReason(fieldFromBoard(b),UrlFormat(format)).empty()){std::ofstream u(root/(name+"-"+urlFormatIds[format]+".url"));u<<simulatorUrl(fieldFromBoard(b),UrlFormat(format));}
    }
    // Non-point fixture exercises all destinations, row13, holes and leading blanks.
    b={};for(int x=0;x<6;++x)for(int y=0;y<13;++y){b.cells[x][y]=Cell(rng()%11);if(b.cells[x][y]==PointPuyo)b.cells[x][y]=Hard;}saveBoard(root/L"compatible.puyoboard",b);
    for(int f=0;f<3;++f){std::ofstream out(root/(std::string("compatible-")+urlFormatIds[f]+".url"));out<<simulatorUrl(fieldFromBoard(b),UrlFormat(f));}
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
