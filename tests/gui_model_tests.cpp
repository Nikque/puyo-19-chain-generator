#include "../gui_urls.h"
#include <thread>
using namespace puyo_gui;
static void require(bool v,const char* message){if(!v)throw std::runtime_error(message);}
static void saveTimeline(const std::filesystem::path& path,const std::vector<Frame>& frames,size_t ignition){
    std::ofstream out(path,std::ios::binary);out<<frames.size()<<' '<<ignition<<'\n';
    for(const auto& f:frames){out<<f.pair<<' '<<f.chain;for(int y=12;y>=0;--y)for(int x=0;x<6;++x)out<<' '<<(size_t(y)<f.field.col[x].size()?int(f.field.col[x][y]):0);out<<'\n';}
    out.flush();require(bool(out),"cannot save displayed timeline");
}
int wmain(int argc,wchar_t** argv){
    try {
        require(argc==2,"need a writable test directory");
        std::filesystem::path root=argv[1];std::filesystem::create_directories(root);
        auto c=defaults();writeConfig(root/L"設定😀.ini",c);auto loaded=loadConfig(root/L"設定😀.ini");require(configText(c)==configText(loaded),"config defaults roundtrip");
        Field colors;for(int x=0;x<6;++x)colors.col[x].push_back(Cell(x%5+1));
        require(simulatorUrl(colors,UrlFormat::PuyoPark)=="https://www.puyop.com/s/asF","puyop five-color golden URL");
        require(simulatorUrl(colors,UrlFormat::Mattulwan)=="https://www.pndsng.com/puyo/index.html?a72becdfb","mattulwan five-color golden URL");
        require(simulatorUrl(Field{},UrlFormat::Mattulwan)=="https://www.pndsng.com/puyo/index.html?a78","empty field run length");
        require(simulatorUrl(colors,UrlFormat::Ishikawa)==fieldToUrl(colors),"legacy format unchanged");
        std::array<std::string,9> values{"19","100","18446744073709551615","4","0","0","48","2","1000"};
        require(parseInputs(values).initialSeed==UINT64_MAX,"uint64 precision");
        for(auto [index,value]:std::initializer_list<std::pair<int,std::string>>{{0,"0"},{0,"20"},{1,"0"},{2,"18446744073709551616"},{2,"1.5"},{2,"-1"},{3,"3"},{4,"3"},{5,"3"},{6,"2147483648"},{7,"abc"},{8,"0"}}){auto t=values;t[index]=value;bool rejected=false;try{parseInputs(t);}catch(...){rejected=true;}require(rejected,"invalid input accepted");}
        std::ofstream log(root/L"combined.log",std::ios::binary);auto old=std::cout.rdbuf(log.rdbuf());auto err=std::cerr.rdbuf(log.rdbuf());
        struct Restore {std::streambuf* a,*b;~Restore(){std::cout.rdbuf(a);std::cerr.rdbuf(b);}} restore{old,err};
        int cases=0;
        for(int chain:{1,19})for(int colors:{4,5})for(int extra:{0,1,2}){
            auto dir=root/std::to_wstring(++cases);std::filesystem::create_directory(dir);auto config=defaults();config.targetChain=chain;config.colorCount=colors;config.targetSuccessCount=1;config.minExtraPuyos=config.maxExtraPuyos=extra;writeConfig(dir/L"config.ini",config);
            int count=0;GeneratorHooks hooks;
            hooks.saved=[&](const Solution& sol,uint64_t seed,int n){
                ++count;require(n==1,"unexpected count");auto frames=timeline(sol);auto ignition=ignitionFrame(sol);
                require(frames[ignition].field==sol.field,"ignition display mismatch");require(frames.back().chain==chain,"playback chain count");
                int remaining=0;for(auto& col:frames.back().field.col)remaining+=int(col.size());require(remaining==extra,"residual display mismatch");
                int setups=0,waves=0;for(auto& f:frames){if(f.label.find("準備:")!=std::string::npos){++setups;require(f.marked.empty() && f.chain==0,"preparation count");}if(f.chain){++waves;require(f.marked.empty(),"no pre-clear highlight frame");}}
                require(waves==2*chain,"post-clear and post-drop frames per chain");require(setups==extra%2,"odd setup display");
                saveTimeline(dir/L"timeline.txt",frames,ignition);saveRecord(dir/L"盤面.puyo",sol,UINT64_MAX);auto r=loadRecord(dir/L"盤面.puyo");require(r.seed==UINT64_MAX && r.solution.field==sol.field,"archive roundtrip");
            };
            require(runGenerator(dir/L"config.ini",&hooks,dir)==0 && count==1,"generate and play boundary cases");
            std::vector<Record> records{loadRecord(dir/L"盤面.puyo")};
            for(int format=0;format<3;++format)writeSimulatorUrls(dir/(std::string("urls-")+urlFormatIds[format]+".txt"),records,UrlFormat(format));
        }
        auto dir=root/L"cancel";std::filesystem::create_directory(dir);writeConfig(dir/L"config.ini",defaults());std::atomic_bool cancel=false;GeneratorHooks hooks;hooks.cancel=&cancel;
        std::chrono::steady_clock::time_point cancelTime;
        hooks.progress=[&](const GeneratorProgress& p){if(p.depth>=6){cancelTime=std::chrono::steady_clock::now();cancel=true;}};
        require(runGenerator(dir/L"config.ini",&hooks,dir)==3,"cooperative cancellation");require(std::chrono::steady_clock::now()-cancelTime<std::chrono::seconds(1),"slow cancel");require(activeHooks==nullptr,"hooks must restore");
        std::ofstream broken(root/L"bad.puyo");broken<<"PUYO_GUI 1\n0 19\n9999999";broken.close();bool rejected=false;try{loadRecord(root/L"bad.puyo");}catch(...){rejected=true;}require(rejected,"corrupt archive accepted");
        log.flush();return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
