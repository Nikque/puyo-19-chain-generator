#define UNICODE
#define _UNICODE
#define NOMINMAX
#include <windows.h>
#include "gui_urls.h"
#include <commctrl.h>
#include <iostream>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <thread>
#include <mutex>
#include <deque>
#include <memory>
#include <iomanip>
#include <windowsx.h>

using namespace puyo_gui;
namespace {
constexpr UINT PUMP=WM_APP+1;
enum Id { START=100, CANCEL, LOAD_CONFIG, SAVE_CONFIG, DEFAULTS, RESULTS=120,
    LOAD_RESULT, COPY_URL, OPEN_URL, PREV, NEXT, PLAY, IGNITE, LAST,
    SLIDER, SPEED, FOLDER, OPEN_FOLDER, DETAIL, STATE, COUNT, PROGRESS, FRAME_LABEL,
    OUTPUT_BASE, OUTPUT_RUN, CAPACITY, URL_FORMAT, EXPORT_URLS, EDIT_MODE,
    NEW_BOARD, LOAD_BOARD, SAVE_BOARD, CLONE_BOARD, UNDO, REDO, EDIT_HELP, PIECE_FIRST=400 };
constexpr const wchar_t* paletteLabels[]={L"消す",L"赤",L"緑",L"青",L"黄",L"紫",L"おじゃま",L"得点",L"かた",L"鉄",L"壁"};
struct Event { enum Kind { Saved, Done } kind; Record record; int code=0; std::string error; };
std::wstring wide(const std::string& s) {
    if(s.empty()) return {};
    int n=MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),nullptr,0);
    std::wstring w(n,L'\0'); MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),w.data(),n); return w;
}
std::string utf8(const std::wstring& s) {
    if(s.empty()) return {};
    int n=WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);
    std::string v(n,'\0'); WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),v.data(),n,nullptr,nullptr); return v;
}
std::wstring text(HWND w) {int n=GetWindowTextLengthW(w); std::wstring v(n+1,L'\0'); GetWindowTextW(w,v.data(),n+1); v.resize(n);return v;}
class App {
public:
    HWND window=nullptr, board=nullptr;
    HFONT font=nullptr, titleFont=nullptr, pieceFont=nullptr;
    HBRUSH background=CreateSolidBrush(RGB(245,247,250));
    std::array<HWND,INPUT_COUNT> inputs{}, labels{};
    std::vector<std::pair<int,HWND>> controls;
    std::vector<Record> records;
    std::vector<Frame> frames;
    int selected=-1, frame=0, dpi=96, target=100, saved=0, resultCode=0;
    bool running=false, closing=false, playing=false;
    bool editing=false, hasEditor=false, stroking=false;
    Editor editor;Board savedBoard{},strokeBefore{},resultViewBoard{};Cell strokePiece=Empty,selectedPiece=Red;
    std::atomic_bool cancel{false}; std::thread worker;
    std::mutex mutex; std::deque<Event> events;
    GeneratorProgress progress{}; bool progressChanged=false;
    std::filesystem::path exeDirectory, runDirectory;
    std::chrono::steady_clock::time_point started;
    double firstSeconds=-1, fifthSeconds=-1;
    double completedSeconds=0;
    bool testing=false;
    int uiTicks=0, errors=0;
    std::wstring state=L"設定を確認し、生成開始を押してください。";
    ~App(){cancel=true;if(worker.joinable())worker.join();DeleteObject(font);DeleteObject(titleFont);DeleteObject(pieceFont);DeleteObject(background);}
    int s(int v) const{return MulDiv(v,dpi,96);}
    HWND get(int id) const {for(auto p:controls)if(p.first==id)return p.second;return nullptr;}
    HWND add(int id,const wchar_t* cls,const wchar_t* caption,DWORD style=0,DWORD ex=0) {
        HWND h=CreateWindowExW(ex,cls,caption,WS_CHILD|WS_VISIBLE|style,0,0,0,0,window,reinterpret_cast<HMENU>(intptr_t(id)),nullptr,nullptr);
        if(!h) throw std::runtime_error("コントロールを作成できません。");
        SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);controls.push_back({id,h});return h;
    }
    void place(HWND h,int x,int y,int w,int hgt){SetWindowPos(h,nullptr,s(x),s(y),s(w),s(hgt),SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOREDRAW|SWP_NOCOPYBITS);}
    void place(int id,int x,int y,int w,int hgt){place(get(id),x,y,w,hgt);}
    void put(int id,const std::wstring& value){auto ctl=get(id);if(text(ctl)!=value)SetWindowTextW(ctl,value.c_str());}
    void error(const std::exception& e){++errors;state=wide(e.what());put(STATE,state);if(!testing)MessageBoxW(window,state.c_str(),L"入力・保存エラー",MB_OK|MB_ICONERROR);}
    GeneratorConfig settings(){std::array<std::string,INPUT_COUNT> t;for(int i=0;i<INPUT_COUNT;++i)t[i]=utf8(text(inputs[i]));return parseInputs(t);}
    void setSettings(const GeneratorConfig& c){
        const std::array<std::string,INPUT_COUNT> t{std::to_string(c.targetChain),std::to_string(c.targetSuccessCount),std::to_string(c.initialSeed),std::to_string(c.colorCount),std::to_string(c.minExtraPuyos),std::to_string(c.maxExtraPuyos),std::to_string(c.beamWidth),std::to_string(c.candidatesPerParent),std::to_string(c.restarts),std::to_string(c.threads)};
        for(int i=0;i<INPUT_COUNT;++i)SetWindowTextW(inputs[i],wide(t[i]).c_str()); capacity();
    }
    void capacity(){
        auto t=utf8(text(inputs[0]));
        try{int n=parsePositiveInt(t,"target_chain");put(CAPACITY,n>=1&&n<=19?L"余剰上限: "+std::to_wstring(78-4*n)+L"個":L"連鎖数は1〜19です。");}
        catch(...){put(CAPACITY,L"連鎖数は1〜19です。");}
    }
    void selectPiece(Cell value){
        selectedPiece=value;
        for(int i=0;i<=Wall;++i){put(PIECE_FIRST+i,std::wstring(paletteLabels[i])+(i==value?L"（選択中）":L""));InvalidateRect(get(PIECE_FIRST+i),nullptr,TRUE);}
    }
    void paintPiece(HDC dc,RECT box,Cell value){
        int state=SaveDC(dc);SetBkMode(dc,TRANSPARENT);
        int d=std::min(box.right-box.left,box.bottom-box.top)-s(6);
        int x=(box.left+box.right-d)/2,y=(box.top+box.bottom-d)/2;
        COLORREF colors[]={RGB(205,215,225),RGB(221,55,55),RGB(0,140,105),RGB(0,86,156),RGB(226,197,0),RGB(137,44,113),RGB(150,155,165),RGB(180,140,10),RGB(100,110,120),RGB(45,55,65),RGB(111,73,40)};
        auto c=colors[value];auto brush=CreateSolidBrush(c);auto pen=CreatePen(PS_SOLID,s(1),RGB(GetRValue(c)*2/3,GetGValue(c)*2/3,GetBValue(c)*2/3));SelectObject(dc,brush);SelectObject(dc,pen);
        if(value==Empty){POINT corners[]={{x,y+d*2/3},{x+d*2/3,y},{x+d,y+d/3},{x+d/3,y+d}};Polygon(dc,corners,4);MoveToEx(dc,x+d/3,y+d/3,nullptr);LineTo(dc,x+d*2/3,y+d*2/3);}
        else if(value==Hard||value==Wall)RoundRect(dc,x,y,x+d,y+d,s(4),s(4));
        else Ellipse(dc,x,y,x+d,y+d);
        if(value>=Red&&value<=Garbage){
            SelectObject(dc,GetStockObject(WHITE_BRUSH));SelectObject(dc,GetStockObject(NULL_PEN));
            for(int eye=0;eye<2;++eye){int ex=x+d*(eye?52:25)/100;Ellipse(dc,ex,y+d/4,ex+d/4,y+d*3/5);SelectObject(dc,GetStockObject(BLACK_BRUSH));Ellipse(dc,ex+d/9,y+d/3,ex+d/5,y+d/2);SelectObject(dc,GetStockObject(WHITE_BRUSH));}
        }
        if(value){SelectObject(dc,pieceFont);SetTextColor(dc,value==Yellow?RGB(40,40,25):RGB(255,255,255));RECT label{x,y+(value<=Garbage?d*3/5:0),x+d,y+d};wchar_t letter=pieceLetters[value];DrawTextW(dc,&letter,1,&label,DT_CENTER|DT_VCENTER|DT_SINGLELINE);}
        RestoreDC(dc,state);DeleteObject(brush);DeleteObject(pen);
    }
    void paintPalette(const DRAWITEMSTRUCT& item){
        int value=int(item.CtlID)-PIECE_FIRST;HDC dc=item.hDC;int state=SaveDC(dc);RECT box=item.rcItem;
        bool chosen=value==selectedPiece;auto b=CreateSolidBrush(chosen?RGB(215,236,255):RGB(255,255,255));FillRect(dc,&box,b);DeleteObject(b);
        auto p=CreatePen(PS_SOLID,s(chosen?2:1),chosen?RGB(0,101,176):RGB(181,192,204));SelectObject(dc,p);SelectObject(dc,GetStockObject(NULL_BRUSH));Rectangle(dc,box.left,box.top,box.right,box.bottom);
        RECT icon{(box.left+box.right)/2-s(19),box.top+s(2),(box.left+box.right)/2+s(19),box.top+s(40)};paintPiece(dc,icon,Cell(value));
        SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(25,40,59));RECT label{box.left,box.top+s(40),box.right,box.bottom-s(2)};DrawTextW(dc,paletteLabels[value],-1,&label,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        if(item.itemState&ODS_FOCUS){InflateRect(&box,-s(4),-s(4));DrawFocusRect(dc,&box);}
        RestoreDC(dc,state);DeleteObject(p);
    }
    void create(){
        dpi=int(GetDpiForWindow(window));
        font=CreateFontW(-s(14),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Yu Gothic UI");
        titleFont=CreateFontW(-s(21),0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Yu Gothic UI");
        pieceFont=CreateFontW(-s(10),0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Yu Gothic UI");
        wchar_t path[32768];GetModuleFileNameW(nullptr,path,32768);exeDirectory=std::filesystem::path(path).parent_path();
        add(1,L"STATIC",L"盤面ジェネレーター",SS_LEFT);SendMessageW(get(1),WM_SETFONT,reinterpret_cast<WPARAM>(titleFont),TRUE);
        const wchar_t* names[]={L"連鎖数（1〜19）",L"生成目標（件）",L"探索seed（uint64）",L"色数（4 / 5）",L"余剰 下限",L"余剰 上限",L"beam_width",L"親ごとの候補数",L"試行上限",L"スレッド数（0=自動）"};
        for(int i=0;i<INPUT_COUNT;++i){labels[i]=add(200+i,L"STATIC",names[i]);inputs[i]=add(300+i,L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,WS_EX_CLIENTEDGE);SendMessageW(inputs[i],EM_SETLIMITTEXT,20,0);}
        add(CAPACITY,L"STATIC",L"");
        for(auto [id,caption]:std::initializer_list<std::pair<int,const wchar_t*>>{{LOAD_CONFIG,L"設定を読込"},{SAVE_CONFIG,L"名前を付けて保存"},{DEFAULTS,L"初期値"},{START,L"生成開始"},{CANCEL,L"中断"},{FOLDER,L"保存先を選択"},{OPEN_FOLDER,L"今回の保存先を開く"},{LOAD_RESULT,L"保存結果を読込"},{COPY_URL,L"URLコピー"},{OPEN_URL,L"URLを開く"},{EXPORT_URLS,L"一覧のURLを保存"},{PREV,L"◀ 前へ"},{NEXT,L"次へ ▶"},{PLAY,L"再生"},{IGNITE,L"発火へ"},{LAST,L"終了へ"}}) add(id,L"BUTTON",caption,WS_TABSTOP);
        add(OUTPUT_BASE,L"EDIT",(exeDirectory/L"results").c_str(),ES_AUTOHSCROLL|WS_TABSTOP,WS_EX_CLIENTEDGE);
        add(OUTPUT_RUN,L"EDIT",L"実行ごとに新しいフォルダーへ保存します。",ES_AUTOHSCROLL|ES_READONLY,WS_EX_CLIENTEDGE);
        add(STATE,L"STATIC",state.c_str());add(COUNT,L"STATIC",L"0 / 100 件");add(PROGRESS,PROGRESS_CLASSW,L"");
        add(RESULTS,L"LISTBOX",L"",LBS_NOTIFY|WS_VSCROLL|WS_TABSTOP|LBS_NOINTEGRALHEIGHT,WS_EX_CLIENTEDGE);
        add(DETAIL,L"EDIT",L"生成結果を選ぶと構築手順を表示します。",ES_MULTILINE|ES_READONLY|WS_VSCROLL|ES_AUTOVSCROLL,WS_EX_CLIENTEDGE);
        add(FRAME_LABEL,L"STATIC",L"盤面: 6列 × 13段");
        add(SLIDER,TRACKBAR_CLASSW,L"",WS_TABSTOP|TBS_HORZ|TBS_NOTICKS);
        add(SPEED,L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST);
        for(auto str:{L"ゆっくり 900ms",L"標準 450ms",L"速い 150ms"})SendMessageW(get(SPEED),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(str));SendMessageW(get(SPEED),CB_SETCURSEL,1,0);
        add(10,L"STATIC",L"保存・コピーするURL形式");
        add(URL_FORMAT,L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL);
        for(auto name:urlFormatNames){auto w=wide(name);SendMessageW(get(URL_FORMAT),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(w.c_str()));}
        SendMessageW(get(URL_FORMAT),CB_SETCURSEL,0,0);
        for(auto [id,caption]:std::initializer_list<std::pair<int,const wchar_t*>>{{EDIT_MODE,L"盤面編集"},{NEW_BOARD,L"盤面をクリア"},{LOAD_BOARD,L"編集盤面を読込"},{SAVE_BOARD,L"編集盤面を保存"},{CLONE_BOARD,L"結果の表示盤面を取込"},{UNDO,L"元に戻す"},{REDO,L"やり直し"}})add(id,L"BUTTON",caption,WS_TABSTOP);
        for(int i=0;i<=Wall;++i)add(PIECE_FIRST+i,L"BUTTON",paletteLabels[i],WS_TABSTOP|BS_OWNERDRAW);
        selectPiece(Red);
        add(EDIT_HELP,L"STATIC",L"左クリック/ドラッグ: 配置  /  右クリック: 消す");
        board=add(3,L"PuyoBoard",L"6×13盤面",0);
        add(4,L"STATIC",L"13段目は消去対象外・落下対象。白い輪: 最初に消える4個 / 黄色の輪: 最後に置く1組。");
        add(5,L"STATIC",L"黄色の輪の1組を最後に置くと発火します。盤面と連鎖はエンジンで検証済みです。");
        add(6,L"STATIC",L"保存先（毎回、新規フォルダー）");add(7,L"STATIC",L"結果一覧 / 選択して再生");
        add(8,L"STATIC",L"探索詳細");add(9,L"STATIC",L"↑ 前へ / F 次へ / Space 再生・停止");
        setSettings(defaults());layout();updateEnabled();SetTimer(window,1,100,nullptr);
    }
    void layout(){
        RECT r;GetClientRect(window,&r);if(r.right<=0||r.bottom<=0)return;int w=MulDiv(r.right,96,dpi),h=MulDiv(r.bottom,96,dpi);int right=642,rw=std::max(280,w-right-18);
        place(1,18,14,400,36);
        for(int i=0;i<INPUT_COUNT;++i){int y=90+i*37+(i>=6?43:0);place(labels[i],18,y+5,133,25);place(inputs[i],153,y,144,30);}
        place(CAPACITY,18,314,280,24);place(8,18,337,280,22);
        place(LOAD_CONFIG,18,512,92,31);place(SAVE_CONFIG,115,512,182,31);place(DEFAULTS,18,553,75,31);place(START,100,553,119,31);place(CANCEL,226,553,71,31);
        place(6,18,597,280,24);place(OUTPUT_BASE,18,624,279,30);place(FOLDER,18,662,125,30);place(OPEN_FOLDER,150,662,147,30);
        place(OUTPUT_RUN,18,h-139,w-36,28);place(COUNT,18,h-103,w-36,24);place(PROGRESS,18,h-75,w-36,14);place(STATE,18,h-50,w-36,36);
        place(7,right,88,rw,25);place(LOAD_RESULT,right,120,146,30);place(EDIT_MODE,right+154,120,rw-154,30);place(RESULTS,right,160,rw,166);
        int third=(rw-12)/3;place(NEW_BOARD,right,160,third,30);place(LOAD_BOARD,right+third+6,160,third,30);place(SAVE_BOARD,right+2*(third+6),160,third,30);
        int half=(rw-12)/2,quarter=(rw-half-12)/2;place(CLONE_BOARD,right,197,half,30);place(UNDO,right+half+6,197,quarter,30);place(REDO,right+half+quarter+12,197,rw-half-quarter-12,30);
        for(int i=0;i<=Wall;++i){int col=i%6;place(PIECE_FIRST+i,right+col*rw/6,232+(i/6)*66,(col+1)*rw/6-col*rw/6-4,60);}
        place(EDIT_HELP,right,364,rw,32);
        place(10,right,400,rw,22);place(URL_FORMAT,right,425,rw,140);
        place(COPY_URL,right,465,100,31);place(OPEN_URL,right+107,465,100,31);place(EXPORT_URLS,right+214,465,rw-214,31);
        place(DETAIL,right,509,rw,std::max(100,h-669));
        place(FRAME_LABEL,325,94,300,34);int cell=std::min(37,std::max(20,(h-490)/13));int bh=13*cell+24;
        place(board,321,130,306,bh);
        int y=140+bh;place(SLIDER,321,y,300,28);place(IGNITE,321,y+33,146,31);place(LAST,472,y+33,146,31);
        place(PREV,321,y+72,91,31);place(PLAY,420,y+72,91,31);place(NEXT,519,y+72,99,31);place(SPEED,321,y+112,297,120);
        place(9,right,h-154,rw,24);place(4,321,71,650,18);place(5,18,h-177,w-36,24);
        // Repaint vacated parent areas and every child after moving them. Copying
        // old control pixels leaves text trails during interactive resizing.
        RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN|RDW_UPDATENOW);
    }
    void updateEnabled(){
        for(HWND i:inputs)EnableWindow(i,!running);
        for(int id:{START,LOAD_CONFIG,SAVE_CONFIG,DEFAULTS,FOLDER,OUTPUT_BASE,URL_FORMAT})EnableWindow(get(id),!running);
        EnableWindow(get(EXPORT_URLS),!running&&(editing||!records.empty()));
        EnableWindow(get(CANCEL),running&&!cancel);EnableWindow(get(OPEN_FOLDER),!runDirectory.empty());
        for(int id:{COPY_URL,OPEN_URL,PREV,NEXT,PLAY,IGNITE,LAST,SLIDER})EnableWindow(get(id),editing||selected>=0);
        ShowWindow(get(RESULTS),editing?SW_HIDE:SW_SHOW);
        for(int id:{NEW_BOARD,LOAD_BOARD,SAVE_BOARD,CLONE_BOARD,UNDO,REDO,EDIT_HELP})ShowWindow(get(id),editing?SW_SHOW:SW_HIDE);
        for(int i=0;i<=Wall;++i)ShowWindow(get(PIECE_FIRST+i),editing?SW_SHOW:SW_HIDE);
        EnableWindow(get(UNDO),!editor.undo.empty());EnableWindow(get(REDO),!editor.redo.empty());EnableWindow(get(CLONE_BOARD),selected>=0);
        put(EDIT_MODE,editing?L"結果表示へ戻る":L"盤面編集");put(EXPORT_URLS,editing?L"編集盤面URLを保存":L"一覧のURLを保存");put(7,editing?L"盤面編集 / パレットで種類を選択":L"結果一覧 / 選択して再生");
        put(IGNITE,editing?L"編集盤面へ":L"発火へ");
        put(4,editing?L"13段目は消去対象外。壁は固定、その他は落下。":L"13段目は消去対象外・落下対象。白い輪: 最初に消える4個 / 黄色の輪: 最後に置く1組。");
        put(5,editing?L"ぷよを選んで盤面へ配置。「再生」「次へ」で連鎖を確認できます。":L"黄色の輪の1組を最後に置くと発火します。盤面と連鎖はエンジンで検証済みです。");
        if(editing){auto reason=urlUnavailableReason(editor.board,urlFormat());for(int id:{COPY_URL,OPEN_URL,EXPORT_URLS})EnableWindow(get(id),reason.empty()&&(id!=EXPORT_URLS||!running));}
    }
    std::filesystem::path chooseFile(bool save,bool config){
        wchar_t path[32768]{};OPENFILENAMEW o{sizeof(o)};o.hwndOwner=window;o.lpstrFile=path;o.nMaxFile=32768;
        o.lpstrFilter=config?L"設定ファイル (*.ini)\0*.ini\0すべて\0*.*\0":L"検証済み盤面 (*.puyo)\0*.puyo\0\0";
        o.lpstrDefExt=config?L"ini":L"puyo";o.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|(save?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);
        if(save?GetSaveFileNameW(&o):GetOpenFileNameW(&o))return path;return {};
    }
    void chooseFolder(){
        BROWSEINFOW b{};b.hwndOwner=window;b.lpszTitle=L"生成結果の保存先（中に実行ごとのフォルダーを作成）";b.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE;
        if(auto id=SHBrowseForFolderW(&b)){wchar_t path[MAX_PATH];if(SHGetPathFromIDListW(id,path))put(OUTPUT_BASE,path);CoTaskMemFree(id);}
    }
    void post(Event e){ {std::lock_guard lock(mutex);events.push_back(std::move(e));}PostMessageW(window,PUMP,0,0); }
    void start(){
        if(running)return;
        const auto format=urlFormat();
        auto c=settings(); auto base=std::filesystem::absolute(std::filesystem::path(text(get(OUTPUT_BASE))));
        std::filesystem::create_directories(base);
        SYSTEMTIME st;GetLocalTime(&st);wchar_t name[100];swprintf_s(name,L"run-%04u%02u%02u-%02u%02u%02u-%03u-%lu",st.wYear,st.wMonth,st.wDay,st.wHour,st.wMinute,st.wSecond,st.wMilliseconds,GetCurrentProcessId());
        runDirectory=base/name;if(!std::filesystem::create_directory(runDirectory))throw std::runtime_error("新規の保存先を作成できません。");
        writeConfig(runDirectory/L"config.ini",c);
        {std::ofstream meta(runDirectory/L"url-format.txt",std::ios::binary);meta<<urlFormatIds[int(format)]<<'\n';if(!meta)throw std::runtime_error("URL形式を保存できません。");}
        if(worker.joinable())worker.join();cancel=false;closing=false;running=true;target=c.targetSuccessCount;saved=0;resultCode=0;
        firstSeconds=fifthSeconds=-1;completedSeconds=0;started=std::chrono::steady_clock::now();state=L"探索中";
        {std::lock_guard lock(mutex);progress={};}
        put(OUTPUT_RUN,runDirectory.wstring());updateEnabled();
        worker=std::thread([this,dir=runDirectory,format]{
            try {
                std::ofstream log(dir/L"generator.log",std::ios::binary);if(!log)throw std::runtime_error("ログを作成できません。");
                std::ofstream urls(dir/L"simulator_urls.txt",std::ios::binary);if(!urls)throw std::runtime_error("選択形式のURL一覧を作成できません。");
                // Only the worker uses the engine's human-readable log. UI data uses hooks.
                struct Redirect {std::streambuf* out,*err;Redirect(std::ostream& l):out(std::cout.rdbuf(l.rdbuf())),err(std::cerr.rdbuf(l.rdbuf())){}~Redirect(){std::cout.flush();std::cerr.flush();std::cout.rdbuf(out);std::cerr.rdbuf(err);}} redirect(log);
                GeneratorHooks hooks;hooks.cancel=&cancel;
                hooks.progress=[this](const GeneratorProgress& p){std::lock_guard lock(mutex);progress=p;progressChanged=true;};
                hooks.saved=[this,dir,format,&urls](const Solution& solution,uint64_t seed,int count){
                    auto path=dir/(L"board-"+std::to_wstring(count)+L".puyo");saveRecord(path,solution,seed);
                    urls<<simulatorUrl(solution.field,format)<<'\n';urls.flush();if(!urls)throw std::runtime_error("選択形式のURL一覧の書き込みに失敗しました。");
                    post({Event::Saved,{solution,seed,path}});
                };
                int code=runGenerator(dir/L"config.ini",&hooks,dir);post({Event::Done,{},code});
            }catch(const std::exception& e){post({Event::Done,{},2,e.what()});}
        });
    }
    void select(int n){
        if(n<0 || n>=int(records.size()))return;stopPlay();editing=false;selected=n;frames=timeline(records[n].solution);frame=0;
        SendMessageW(get(RESULTS),LB_SETCURSEL,n,0);SendMessageW(get(SLIDER),TBM_SETRANGE,TRUE,MAKELPARAM(0,int(frames.size()-1)));showFrame();updateEnabled();
    }
    void append(Record r){
        int cells=r.solution.field.count();
        std::wstring label=std::to_wstring(records.size()+1)+L".  "+std::to_wstring(r.solution.targetChain)+L"連鎖 / "+std::to_wstring(cells)+L"個 / 余剰"+std::to_wstring(cells-4*r.solution.targetChain);
        records.push_back(std::move(r));SendMessageW(get(RESULTS),LB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));if(selected<0&&!editing)select(int(records.size()-1));else updateEnabled();
    }
    void drain(){
        std::deque<Event> pending;{std::lock_guard lock(mutex);pending.swap(events);}
        if(pending.empty()&&!running)return;
        for(auto& e:pending){
            if(e.kind==Event::Saved){++saved;double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();if(saved==1)firstSeconds=elapsed;if(saved==5)fifthSeconds=elapsed;append(std::move(e.record));}
            else{running=false;completedSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();resultCode=e.code;if(worker.joinable())worker.join();state=e.code==0?L"目標達成":e.code==1?L"試行上限: 部分成果を保存しました。":e.code==3?L"中断: 部分成果を保存しました。":L"エラー: 今回の保存先のgenerator.logを確認してください。";if(!e.error.empty())state+=L" "+wide(e.error);updateEnabled();if(closing){DestroyWindow(window);return;}}
        }
        metrics();
    }
    void metrics(){
        GeneratorProgress p;{std::lock_guard lock(mutex);p=progress;}
        double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
        if(!running)seconds=completedSeconds;
        std::wostringstream o;o<<saved<<L" / "<<target<<L"件";
        if(started!=std::chrono::steady_clock::time_point{}) {o<<L"  |  "<<std::fixed<<std::setprecision(1)<<seconds<<L"秒  |  "<<std::setprecision(2)<<(seconds>0?saved/seconds:0)<<L"件/秒";if(firstSeconds>=0)o<<L"  |  初回 "<<firstSeconds<<L"秒";if(fifthSeconds>=0)o<<L"  |  5件目 "<<fifthSeconds<<L"秒";}
        put(COUNT,o.str());int position=target?int(100.0*saved/target):0;if(SendMessageW(get(PROGRESS),PBM_GETPOS,0,0)!=position)SendMessageW(get(PROGRESS),PBM_SETPOS,position,0);
        std::wstring st=state;if(running)st+=L"  |  試行 "+std::to_wstring(p.attempt)+L"/"+std::to_wstring(p.totalAttempts)+L"  |  段階 "+std::to_wstring(p.depth)+L"/"+std::to_wstring(p.targetChain)+L"  "+wide(p.phase);put(STATE,st);
    }
    void showFrame(){
        if(frames.empty())return;const auto& f=frames[frame];
        if(editing){put(FRAME_LABEL,wide(f.label)+L" ["+std::to_wstring(frame)+L"/"+std::to_wstring(frames.size()-1)+L"]");SendMessageW(get(SLIDER),TBM_SETRANGE,TRUE,MAKELPARAM(0,int(frames.size()-1)));SendMessageW(get(SLIDER),TBM_SETPOS,TRUE,frame);
            auto reason=urlUnavailableReason(editor.board,urlFormat());std::wstring detail=L"編集盤面 / "+std::to_wstring(f.chain)+L"連鎖 / "+std::to_wstring(f.score)+L"点\r\n"+(editor.board==savedBoard?L"保存状態と一致\r\n":L"未保存の変更あり\r\n")+L"\r\nおじゃま: 隣の色消去で消える\r\n得点: 消去時50点（1個につき1回）\r\nかた: 1方向でおじゃま、2方向で消去\r\n鉄: 消えない・落下する\r\n壁: 消えない・固定\r\n\r\nURLは計算開始前の編集盤面。\r\n";
            if(!reason.empty())detail+=L"\r\n出力不可: "+wide(reason)+L"\r\nmattulwan系を選択してください。";put(DETAIL,detail);InvalidateRect(board,nullptr,FALSE);updateEnabled();return;}
        if(selected<0)return;resultViewBoard=f.board;const auto& rec=records[selected];const auto& sol=rec.solution;
        put(FRAME_LABEL,wide(f.label)+L"  ["+std::to_wstring(frame)+L"/"+std::to_wstring(frames.size()-1)+L"]");SendMessageW(get(SLIDER),TBM_SETPOS,TRUE,frame);
        int cells=sol.field.count();
        std::wostringstream o;o<<L"盤面・連鎖・最後の1組: エンジン検証済み\r\n"<<L"探索seed: "<<rec.seed<<L"\r\n";
        o<<sol.targetChain<<L"連鎖 / "<<cells<<L"個 / 余剰"<<cells-4*sol.targetChain<<L"個\r\n";
        o<<L"現在 "<<f.chain<<L" / "<<sol.targetChain<<L"連鎖  "<<f.score<<L"点\r\n\r\n";
        o<<L"最初に消える4個（列,段）:\r\n ";for(Point q:pointsOf(sol.trigger))o<<L" ("<<q.first+1<<L","<<q.second+1<<L")";
        o<<L"\r\n\r\n最後に置く1組の候補（どれか1つで発火）:\r\n";
        bool firstOption=true;
        for(int id=0;id<PAIR_POSITIONS;++id)if(sol.firePairs>>id&1){o<<(firstOption?L"> ":L"  ")<<(id<W?L"縦":L"横");for(Point q:pointsOf(pairCells(sol.field,id)))o<<L" ("<<q.first+1<<L","<<q.second+1<<L")";o<<(firstOption?L"  ← 黄色の輪\r\n":L"\r\n");firstOption=false;}
        o<<L"\r\nこの1組を除いた盤面は何も消えず、3列目12段目が空いていて、組ぷよがその場所へ届きます。\r\n";
        put(DETAIL,o.str());InvalidateRect(board,nullptr,FALSE);
    }
    void stopPlay(){playing=false;KillTimer(window,2);put(PLAY,L"再生");}
    void play(){if((selected<0&&!editing)||frames.empty())return;if(playing){stopPlay();return;}if(editing&&frames.size()==1)frames=editorTimeline(editor.board);if(frame+1>=int(frames.size()))frame=0;playing=true;put(PLAY,L"停止");int i=int(SendMessageW(get(SPEED),CB_GETCURSEL,0,0));SetTimer(window,2,i==0?900:i==2?150:450,nullptr);showFrame();}
    void moveFrame(int n){if((selected<0&&!editing)||frames.empty())return;stopPlay();if(editing&&frames.size()==1&&n>0)frames=editorTimeline(editor.board);frame=std::clamp(n,0,int(frames.size()-1));showFrame();}
    void editorView(){stopPlay();editing=hasEditor=true;frames={{editor.board,{},{},"編集中"}};frame=0;showFrame();}
    void toggleEditor(){if(editing){stopPlay();editing=false;if(selected>=0)select(selected);else{frames.clear();put(DETAIL,L"生成結果を選択してください。");InvalidateRect(board,nullptr,FALSE);updateEnabled();}}else{if(!hasEditor&&!frames.empty())editor.replace(frames[frame].board);editorView();}}
    std::filesystem::path boardFile(bool save){wchar_t path[32768]=L"";OPENFILENAMEW o{sizeof(o)};o.hwndOwner=window;o.lpstrFile=path;o.nMaxFile=32768;o.lpstrFilter=L"編集盤面 (*.puyoboard)\0*.puyoboard\0\0";o.lpstrDefExt=L"puyoboard";o.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|(save?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);if(save?GetSaveFileNameW(&o):GetOpenFileNameW(&o))return path;return {};}
    bool saveEditor(){auto path=boardFile(true);if(path.empty())return false;saveBoard(path,editor.board);savedBoard=editor.board;showFrame();return true;}
    bool confirmClose(){if(!hasEditor||editor.board==savedBoard)return true;int choice=MessageBoxW(window,L"編集盤面に未保存の変更があります。保存して終了しますか？",L"編集盤面の保存",MB_YESNOCANCEL|MB_ICONQUESTION);return choice==IDNO||(choice==IDYES&&saveEditor());}
    void drawCell(int px,int py,Cell piece){if(!editing)return;RECT r;GetClientRect(board,&r);int cell=std::min((r.right-s(35))/6,(r.bottom-s(24))/13),left=(r.right-6*cell)/2+s(9),top=s(5);if(px<left||px>=left+6*cell||py<top||py>=top+13*cell)return;int x=(px-left)/cell,y=12-(py-top)/cell;editor.board.cells[x][y]=piece;frames={{editor.board,{},{},"編集中"}};frame=0;showFrame();}
    void beginStroke(int x,int y,bool erase){if(!editing)return;if(stroking)endStroke();RECT r;GetClientRect(board,&r);int cell=std::min((r.right-s(35))/6,(r.bottom-s(24))/13),left=(r.right-6*cell)/2+s(9),top=s(5);if(x<left||x>=left+6*cell||y<top||y>=top+13*cell)return;stopPlay();SetFocus(board);strokeBefore=editor.board;if(!frames.empty())editor.board=frames[frame].board;strokePiece=erase?Empty:selectedPiece;stroking=true;SetCapture(board);drawCell(x,y,strokePiece);}
    void endStroke(){if(!stroking)return;stroking=false;auto after=editor.board;editor.board=strokeBefore;editor.replace(after);ReleaseCapture();showFrame();}
    UrlFormat urlFormat() const {auto n=SendMessageW(get(URL_FORMAT),CB_GETCURSEL,0,0);if(n<0||n>=3)throw std::runtime_error("URL形式を選択してください。");return UrlFormat(n);}
    std::string selectedUrl() const {return editing?simulatorUrl(editor.board,urlFormat()):selected>=0?simulatorUrl(records[selected].solution.field,urlFormat()):std::string{};}
    void exportUrls(){
        // Reject incompatible types before any dialog or file is opened.
        const auto boardUrl=editing?selectedUrl():std::string{};
        wchar_t path[32768]=L"simulator_urls.txt";OPENFILENAMEW o{sizeof(o)};o.hwndOwner=window;o.lpstrFile=path;o.nMaxFile=32768;
        o.lpstrTitle=L"結果一覧の全件を選択形式で保存";o.lpstrFilter=L"URL一覧 (*.txt)\0*.txt\0\0";o.lpstrDefExt=L"txt";o.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|OFN_OVERWRITEPROMPT;
        if(GetSaveFileNameW(&o)){if(editing){std::ofstream out(std::filesystem::path(path),std::ios::binary);out<<boardUrl<<'\n';out.flush();if(!out)throw std::runtime_error("URLを保存できません。");state=L"編集盤面URLを保存しました。";}else{writeSimulatorUrls(path,records,urlFormat());state=L"結果一覧の全"+std::to_wstring(records.size())+L"件を選択形式で保存しました。";}metrics();}
    }
    void copyUrl(){if(selected<0&&!editing)return;auto w=wide(selectedUrl());auto mem=GlobalAlloc(GMEM_MOVEABLE,(w.size()+1)*sizeof(wchar_t));if(!mem)throw std::runtime_error("クリップボードメモリ不足");auto p=GlobalLock(mem);if(!p){GlobalFree(mem);throw std::runtime_error("クリップボードメモリを取得できません");}memcpy(p,w.c_str(),(w.size()+1)*sizeof(wchar_t));GlobalUnlock(mem);if(!OpenClipboard(window)){GlobalFree(mem);throw std::runtime_error("クリップボードを開けません");}EmptyClipboard();if(!SetClipboardData(CF_UNICODETEXT,mem)){CloseClipboard();GlobalFree(mem);throw std::runtime_error("URLをコピーできません");}CloseClipboard();state=L"盤面のURLをコピーしました。";metrics();}
    void open(const std::wstring& path){if(reinterpret_cast<INT_PTR>(ShellExecuteW(window,L"open",path.c_str(),nullptr,nullptr,SW_SHOWNORMAL))<=32)throw std::runtime_error("開けません: "+utf8(path));}
    void command(int id,int notification){
        if(stroking)endStroke();
        if(id==300 && notification==EN_CHANGE){capacity();return;}
        if(id==RESULTS && notification==LBN_SELCHANGE){select(int(SendMessageW(get(RESULTS),LB_GETCURSEL,0,0)));return;}
        if(id==URL_FORMAT && notification==CBN_SELCHANGE){state=L"URL形式: "+wide(urlFormatNames[int(urlFormat())])+L"（次回生成・コピー・一覧保存に適用）";updateEnabled();if(editing)showFrame();metrics();return;}
        if(notification!=BN_CLICKED && id!=SPEED)return;
        if(id>=PIECE_FIRST&&id<=PIECE_FIRST+Wall){selectPiece(Cell(id-PIECE_FIRST));return;}
        switch(id){
        case START:start();break;case CANCEL:cancel=true;state=L"中断処理中";updateEnabled();break;
        case DEFAULTS:if(!running)setSettings(defaults());break;
        case LOAD_CONFIG:if(!running){auto p=chooseFile(false,true);if(!p.empty()){auto c=loadConfig(p);setSettings(c);state=L"設定を読み込みました: "+p.wstring();}}break;
        case SAVE_CONFIG:if(!running){auto c=settings();auto p=chooseFile(true,true);if(!p.empty()){writeConfig(p,c);state=L"設定を保存しました: "+p.wstring();}}break;
        case FOLDER:if(!running)chooseFolder();break;case OPEN_FOLDER:if(!runDirectory.empty())open(runDirectory.wstring());break;
        case LOAD_RESULT:{auto p=chooseFile(false,false);if(!p.empty()){append(loadRecord(p));select(int(records.size()-1));}}break;
        case COPY_URL:copyUrl();break;case OPEN_URL:if(selected>=0||editing)open(wide(selectedUrl()));break;case EXPORT_URLS:if(!running)exportUrls();break;
        case EDIT_MODE:toggleEditor();break;case NEW_BOARD:editor.replace({});editorView();break;
        case LOAD_BOARD:{auto p=boardFile(false);if(!p.empty()){auto b=loadBoard(p);editor.replace(b);savedBoard=b;editorView();}}break;
        case SAVE_BOARD:saveEditor();break;
        case CLONE_BOARD:if(selected>=0){editor.replace(resultViewBoard);editorView();}break;
        case UNDO:editor.back();editorView();break;case REDO:editor.forward();editorView();break;
        case PREV:moveFrame(frame-1);break;case NEXT:moveFrame(frame+1);break;case PLAY:play();break;
        case IGNITE:moveFrame(0);break;case LAST:if(editing&&frames.size()==1)frames=editorTimeline(editor.board);moveFrame(int(frames.size()-1));break;
        case SPEED:if(playing){stopPlay();play();}break;
        }
    }
    void paintBoard(HWND h,HDC printDC=nullptr){
        PAINTSTRUCT ps{};HDC dc=printDC?printDC:BeginPaint(h,&ps);RECT r;GetClientRect(h,&r);HDC mem=CreateCompatibleDC(dc);HBITMAP bmp=CreateCompatibleBitmap(dc,r.right,r.bottom);auto oldBmp=SelectObject(mem,bmp);
        auto bg=CreateSolidBrush(RGB(24,34,49));FillRect(mem,&r,bg);DeleteObject(bg);SelectObject(mem,font);SetBkMode(mem,TRANSPARENT);
        int cell=std::min((r.right-s(35))/6,(r.bottom-s(24))/13),left=(r.right-6*cell)/2+s(9),top=s(5);
        const Frame* f=frames.empty()?nullptr:&frames[frame];
        auto gridPen=CreatePen(PS_SOLID,1,RGB(58,73,91));auto oldPen=SelectObject(mem,gridPen);auto oldBrush=SelectObject(mem,GetStockObject(NULL_BRUSH));
        for(int y=0;y<13;++y){int yy=top+(12-y)*cell;SetTextColor(mem,RGB(170,186,205));RECT nr{0,yy,left-s(3),yy+cell};auto n=std::to_wstring(y+1);DrawTextW(mem,n.c_str(),-1,&nr,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
            for(int x=0;x<6;++x){int xx=left+x*cell;RECT box{xx,yy,xx+cell,yy+cell};if(y==12){auto b=CreateSolidBrush(RGB(54,49,68));FillRect(mem,&box,b);DeleteObject(b);}Rectangle(mem,xx,yy,xx+cell,yy+cell);
                int v=f?f->board.cells[x][y]:0;
                if(v)paintPiece(mem,box,Cell(v));
                for(int ring=0;f&&ring<2;++ring){const auto& list=ring?f->pair:f->marked;if(std::find(list.begin(),list.end(),Point{x,y})==list.end())continue;int inset=ring?s(4):s(1);auto p=CreatePen(PS_SOLID,s(2),ring?RGB(255,221,51):RGB(255,255,255));SelectObject(mem,p);Ellipse(mem,xx+inset,yy+inset,xx+cell-inset,yy+cell-inset);SelectObject(mem,gridPen);DeleteObject(p);}
            }
        }
        SetTextColor(mem,RGB(170,186,205));for(int x=0;x<6;++x){RECT nr{left+x*cell,top+13*cell,left+(x+1)*cell,r.bottom};auto n=std::to_wstring(x+1);DrawTextW(mem,n.c_str(),-1,&nr,DT_CENTER|DT_SINGLELINE);}
        SelectObject(mem,oldPen);SelectObject(mem,oldBrush);DeleteObject(gridPen);BitBlt(dc,0,0,r.right,r.bottom,mem,0,0,SRCCOPY);SelectObject(mem,oldBmp);DeleteObject(bmp);DeleteDC(mem);if(!printDC)EndPaint(h,&ps);
    }
    void snapshot(const std::filesystem::path& path,bool live=false){
        RECT r;GetClientRect(window,&r);HDC dc=GetDC(window),mem=CreateCompatibleDC(dc);
        BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),r.right,-r.bottom,1,32,BI_RGB};void* pixels=nullptr;
        HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);auto old=SelectObject(mem,bitmap);
        if(live)BitBlt(mem,0,0,r.right,r.bottom,dc,0,0,SRCCOPY);
        else SendMessageW(window,WM_PRINT,reinterpret_cast<WPARAM>(mem),PRF_CLIENT|PRF_CHILDREN|PRF_ERASEBKGND);
        GdiFlush(); // Complete GDI writes before reading the DIB's pixel memory.
        DWORD size=DWORD(r.right*r.bottom*4);BITMAPFILEHEADER file{0x4d42,DWORD(sizeof(BITMAPFILEHEADER)+sizeof(BITMAPINFOHEADER))+size,0,0,DWORD(sizeof(BITMAPFILEHEADER)+sizeof(BITMAPINFOHEADER))};
        std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(&file),sizeof(file));out.write(reinterpret_cast<const char*>(&info.bmiHeader),sizeof(info.bmiHeader));out.write(static_cast<const char*>(pixels),size);
        SelectObject(mem,old);DeleteObject(bitmap);DeleteDC(mem);ReleaseDC(window,dc);if(!out)throw std::runtime_error("プレビュー画像を保存できません。");
    }
};
App* app=nullptr;
struct PaintProbe {int textWrites=0,paints=0,erases=0,progressWrites=0;};
LRESULT CALLBACK probeProc(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data){
    auto& probe=*reinterpret_cast<PaintProbe*>(data);if(m==WM_SETTEXT)++probe.textWrites;if(m==WM_PAINT)++probe.paints;if(m==WM_ERASEBKGND)++probe.erases;if(m==PBM_SETPOS)++probe.progressWrites;return DefSubclassProc(h,m,w,l);
}
// A native integration test drives this app's own commands and message pump.
// It does not inspect or manipulate other applications.
int selfTest(App& a,const std::filesystem::path& root) {
    std::filesystem::create_directories(root);std::ofstream report(root/L"gui-test.txt",std::ios::binary);
    auto check=[&](bool v,const char* name){report<<(v?"PASS ":"FAIL ")<<name<<'\n';report.flush();if(!v)throw std::runtime_error(name);};
    auto pump=[&]{MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){if(msg.message!=WM_QUIT){TranslateMessage(&msg);DispatchMessageW(&msg);}}};
    auto awaitDone=[&](double timeout){auto limit=std::chrono::steady_clock::now()+std::chrono::milliseconds(int(timeout*1000));while(a.running && std::chrono::steady_clock::now()<limit){pump();std::this_thread::sleep_for(std::chrono::milliseconds(5));}pump();check(!a.running,"worker completes without blocking the message pump");};
    auto click=[&](int id){SendMessageW(a.window,WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),reinterpret_cast<LPARAM>(a.get(id)));};
    try {
        a.testing=true;
        a.put(OUTPUT_BASE,root.wstring());
        auto c=defaults();check(configText(a.settings())==configText(c),"shipped GUI defaults");
        check(c.initialSeed==20261004&&loadConfig(a.exeDirectory/L"config.ini").initialSeed==c.initialSeed,"release GUI and shipped config use 20261004 seed");
        check(SendMessageW(a.get(URL_FORMAT),CB_GETCOUNT,0,0)==3 && a.urlFormat()==UrlFormat::Ishikawa,"three URL destinations, legacy default");
        check(IsWindowEnabled(a.get(START)) && !IsWindowEnabled(a.get(CANCEL)),"initial start/cancel button state");
        SetWindowTextW(a.inputs[2],L"18446744073709551616");click(START);check(!a.running && a.errors==1,"GUI rejects overflowing uint64 before starting");
        a.setSettings(c);SetWindowTextW(a.inputs[5],L"3");click(START);check(!a.running && a.errors==2,"GUI rejects extras exceeding chain capacity");a.setSettings(c);
        auto current=std::filesystem::current_path();
        SendMessageW(a.get(URL_FORMAT),CB_SETCURSEL,1,0);
        click(START);check(a.running && !IsWindowEnabled(a.get(START)) && IsWindowEnabled(a.get(CANCEL)) && !IsWindowEnabled(a.get(URL_FORMAT)) && !IsWindowEnabled(a.get(EXPORT_URLS)),"double start and format editing disabled");
        awaitDone(180);check(a.resultCode==0 && a.saved==100,"default 100 boards via GUI");
        check(std::filesystem::current_path()==current,"GUI keeps cwd and old history untouched");
        report<<"default_seconds="<<a.completedSeconds<<" outputs_per_second="<<100/a.completedSeconds<<" first_seconds="<<a.firstSeconds<<" fifth_seconds="<<a.fifthSeconds<<'\n';
        ShowWindow(a.window,SW_SHOWNOACTIVATE);UpdateWindow(a.window);pump();
        PaintProbe countProbe,stateProbe,barProbe;
        SetWindowSubclass(a.get(COUNT),probeProc,1,reinterpret_cast<DWORD_PTR>(&countProbe));SetWindowSubclass(a.get(STATE),probeProc,1,reinterpret_cast<DWORD_PTR>(&stateProbe));SetWindowSubclass(a.get(PROGRESS),probeProc,1,reinterpret_cast<DWORD_PTR>(&barProbe));
        auto idleText=text(a.get(COUNT)),idleState=text(a.get(STATE));int idleTicks=a.uiTicks;
        auto idleLimit=std::chrono::steady_clock::now()+std::chrono::milliseconds(2200);
        while(std::chrono::steady_clock::now()<idleLimit){pump();std::this_thread::sleep_for(std::chrono::milliseconds(5));}
        for(int i=0;i<20;++i)a.metrics();
        bool unchanged=a.uiTicks-idleTicks>=15&&countProbe.textWrites==0&&countProbe.paints==0&&countProbe.erases==0&&stateProbe.textWrites==0&&stateProbe.paints==0&&stateProbe.erases==0&&barProbe.progressWrites==0;
        bool actualText=text(a.get(COUNT))==idleText&&text(a.get(STATE))==idleState&&idleText.starts_with(L"100 / 100件")&&SendMessageW(a.get(PROGRESS),PBM_GETPOS,0,0)==100;
        a.state=L"待機メッセージ変更テスト";a.metrics();bool changed=stateProbe.textWrites==1&&text(a.get(STATE))==a.state; a.state=idleState;a.metrics();
        RemoveWindowSubclass(a.get(COUNT),probeProc,1);RemoveWindowSubclass(a.get(STATE),probeProc,1);RemoveWindowSubclass(a.get(PROGRESS),probeProc,1);
        check(unchanged&&actualText,"completed count and status do not rewrite or repaint during idle timers or unchanged metrics");check(changed,"changed status message still updates immediately");
        auto firstRun=a.runDirectory;
        a.select(0);check(a.frame==0&&a.frames[0].board==boardFromBits(a.records[0].solution.field)&&a.frames[0].marked.size()==4&&a.frames[0].pair.size()==2,"selected board is before ignition, with trigger and last pair marked");a.snapshot(root/L"gui-19chain.bmp");
        click(NEXT);auto before=boardFromBits(a.records[0].solution.field);int removed=0;bool floating=false;for(int x=0;x<6;++x)for(int y=0;y<13;++y){if(!before.cells[x][y])continue;Cell cell=a.frames[a.frame].board.cells[x][y];if(!cell)++removed;else{check(cell==before.cells[x][y],"post-clear survivor keeps original cell");if(y&&!a.frames[a.frame].board.cells[x][y-1])floating=true;}}
        check(removed==4&&floating&&a.frames[a.frame].chain==1&&a.frames[a.frame].marked.empty()&&a.frames[a.frame].pair.empty(),"next shows erased holes before gravity without highlight");a.snapshot(root/L"gui-after-clear.bmp");click(NEXT);BitField dropped=a.records[0].solution.field;Wave firstWave;stepWave(dropped,firstWave);check(a.frames[a.frame].board==boardFromBits(dropped),"following next applies gravity");a.snapshot(root/L"gui-after-drop.bmp");click(IGNITE);check(a.frame==0,"ignition button returns to the board before firing");
        check(a.selectedUrl()==simulatorUrl(a.records[0].solution.field,UrlFormat::PuyoPark),"selected URL uses format selector");
        for(int format=0;format<3;++format){SendMessageW(a.get(URL_FORMAT),CB_SETCURSEL,format,0);SendMessageW(a.window,WM_COMMAND,MAKEWPARAM(URL_FORMAT,CBN_SELCHANGE),reinterpret_cast<LPARAM>(a.get(URL_FORMAT)));check(a.selectedUrl()==simulatorUrl(a.records[0].solution.field,UrlFormat(format)),"switch destination after generation");writeSimulatorUrls(root/(std::wstring(L"一覧-")+std::to_wstring(format)+L".txt"),a.records,UrlFormat(format));}
        check(SendMessageW(a.get(SPEED),CB_GETCOUNT,0,0)==3 && SendMessageW(a.get(SPEED),CB_GETCURSEL,0,0)==1,"playback speed selector initialized");
        click(NEXT);check(a.frame==1,"forward navigation");click(PREV);check(a.frame==0,"backward navigation");click(LAST);check(a.frame==int(a.frames.size())-1&&a.frames.size()==39,"19 chains are 1 + 2 x 19 frames");click(IGNITE);check(size_t(a.frame)==ignitionFrame(a.records[0].solution),"ignition navigation");check(text(a.get(IGNITE))==L"発火へ","one button returns to the firing board");
        SendMessageW(a.get(SPEED),CB_SETCURSEL,2,0);click(PLAY);check(a.playing,"playback starts");auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);while(a.playing && std::chrono::steady_clock::now()<deadline){pump();std::this_thread::sleep_for(std::chrono::milliseconds(5));}check(!a.playing && a.frames[a.frame].chain==19,"timer plays all 19 waves");
        for(int extra:{1,2}){SendMessageW(a.get(URL_FORMAT),CB_SETCURSEL,extra==1?2:0,0);c.targetSuccessCount=1;c.minExtraPuyos=c.maxExtraPuyos=extra;a.setSettings(c);click(START);awaitDone(30);check(a.resultCode==0 && a.saved==1,"77/78-cell GUI generation");a.select(int(a.records.size()-1));click(LAST);check(bitsFromBoard(a.frames[a.frame].board).count()==extra,"77/78-cell playback residual");}
        SendMessageW(a.get(URL_FORMAT),CB_SETCURSEL,2,0);
        c=defaults();c.targetSuccessCount=10000000;c.restarts=2000000000;c.threads=1;a.setSettings(c);int ticksBefore=a.uiTicks;click(START);auto limit=std::chrono::steady_clock::now()+std::chrono::milliseconds(1500);while(a.running && std::chrono::steady_clock::now()<limit){pump();std::this_thread::sleep_for(std::chrono::milliseconds(5));}check(a.running&&a.saved>=3,"partial results visible during search");check(a.uiTicks-ticksBefore>=10,"UI timers remain responsive during search");auto cancelTime=std::chrono::steady_clock::now();click(CANCEL);awaitDone(3);check(a.resultCode==3 && a.saved>=3,"cancel retains partial results");report<<"cancel_seconds="<<std::chrono::duration<double>(std::chrono::steady_clock::now()-cancelTime).count()<<'\n';
        c=defaults();c.targetChain=1;c.targetSuccessCount=2;c.initialSeed=UINT64_MAX;a.setSettings(c);click(START);awaitDone(5);check(a.resultCode==0 && a.saved==2,"restart after cancellation with uint64 maximum seed");a.select(int(a.records.size()-1));check(a.records.back().seed==UINT64_MAX,"display preserves seed precision");
        auto record=loadRecord(a.records.back().path);check(record.seed==UINT64_MAX,"saved result reopens and validates");
        c.targetSuccessCount=100;c.restarts=1;a.setSettings(c);click(START);awaitDone(5);check(a.resultCode==1 && a.saved==1,"exit code 1 keeps saved result");
        check(std::filesystem::exists(firstRun/L"board-100.puyo"),"earlier run is not overwritten");
        for(const auto& r:a.records){auto loaded=loadRecord(r.path);check(loaded.solution.field==r.solution.field,"all displayed archives revalidate");}
        a.select(0);auto original=a.records[0].solution.field;click(EDIT_MODE);check(a.editing&&a.editor.board==boardFromBits(original),"edit generated board without changing verified record");check(text(a.get(IGNITE))==L"編集盤面へ","editor button caption");click(NEW_BOARD);
        RECT br;GetClientRect(a.board,&br);int cell=std::min((br.right-a.s(35))/6,(br.bottom-a.s(24))/13),left=(br.right-6*cell)/2+a.s(9),top=a.s(5);
        auto mouse=[&](UINT msg,int x,int y){SendMessageW(a.board,msg,0,MAKELPARAM(left+x*cell+cell/2,top+(12-y)*cell+cell/2));};
        for(int i=0;i<=Wall;++i){SendMessageW(a.get(PIECE_FIRST+i),BM_CLICK,0,0);check(a.selectedPiece==i,"image palette button selects its piece");}
        click(PIECE_FIRST+Wall);mouse(WM_LBUTTONDOWN,0,12);mouse(WM_LBUTTONUP,0,12);check(a.editor.board.cells[0][12]==Wall,"mouse editing includes row 13");click(UNDO);check(a.editor.board==Board{},"undo mouse stroke");click(REDO);check(a.editor.board.cells[0][12]==Wall,"redo mouse stroke");
        click(PIECE_FIRST+PointPuyo);mouse(WM_LBUTTONDOWN,1,0);mouse(WM_MOUSEMOVE,2,0);mouse(WM_LBUTTONUP,2,0);check(a.editor.board.cells[1][0]==PointPuyo&&a.editor.board.cells[2][0]==PointPuyo,"drag paints multiple cells as one undo stroke");
        for(int format=0;format<3;++format){SendMessageW(a.get(URL_FORMAT),CB_SETCURSEL,format,0);SendMessageW(a.window,WM_COMMAND,MAKEWPARAM(URL_FORMAT,CBN_SELCHANGE),reinterpret_cast<LPARAM>(a.get(URL_FORMAT)));check(bool(IsWindowEnabled(a.get(COPY_URL)))==(format==2)&&bool(IsWindowEnabled(a.get(OPEN_URL)))==(format==2)&&bool(IsWindowEnabled(a.get(EXPORT_URLS)))==(format==2),"point puyo blocks unsupported destination copy/open/save");}
        auto draft=a.editor.board;click(EDIT_MODE);check(!a.editing&&a.records[0].solution.field==original,"leaving editor preserves generated result");click(EDIT_MODE);check(a.editor.board==draft,"returning to editor preserves draft");
        mouse(WM_RBUTTONDOWN,1,0);mouse(WM_RBUTTONUP,1,0);check(a.editor.board.cells[1][0]==Empty,"right click erases");
        saveBoard(root/L"編集盤面😀.puyoboard",a.editor.board);check(loadBoard(root/L"編集盤面😀.puyoboard")==a.editor.board,"edited board saves and reopens with Unicode path");
        click(NEW_BOARD);click(PIECE_FIRST+Red);for(int x=0;x<4;++x){mouse(WM_LBUTTONDOWN,x,0);mouse(WM_LBUTTONUP,x,0);}click(PIECE_FIRST+Hard);mouse(WM_LBUTTONDOWN,0,1);mouse(WM_LBUTTONUP,0,1);click(NEXT);check(a.editing&&a.frames.back().chain==1&&a.frames[a.frame].board.cells[0][0]==Empty&&a.frames[a.frame].board.cells[0][1]==Garbage,"next automatically computes and displays post-clear board");for(auto& f:a.frames)check(f.marked.empty(),"editor playback has no pre-clear highlight frames");click(LAST);check(a.frames.back().board.cells[0][0]==Garbage&&a.frames.back().board.cells[0][1]==Empty,"editor playback shows hard converted then dropped");a.editorView();click(PLAY);check(a.playing&&a.frames.back().chain==1,"play automatically computes editor timeline");a.stopPlay();click(IGNITE);check(a.frame==0,"editor button returns to the edited board");
        click(PIECE_FIRST+Empty);mouse(WM_LBUTTONDOWN,3,0);mouse(WM_LBUTTONUP,3,0);check(a.editor.board.cells[3][0]==Empty,"eraser image selects left-click erase");click(UNDO);
        // Exercise real window resizing and compare its displayed pixels with a
        // complete repaint, rather than only inspecting an off-screen preview.
        ShowWindow(a.window,SW_SHOWNOACTIVATE);
        int resizeCase=0;
        for(auto [width,height]:std::initializer_list<std::pair<int,int>>{{1080,897},{1440,1040},{1160,940},{1080,897},{1144,951}}){
            RECT outer{0,0,a.s(width),a.s(height)};AdjustWindowRectExForDpi(&outer,WS_OVERLAPPEDWINDOW,FALSE,0,a.dpi);
            SetWindowPos(a.window,nullptr,0,0,outer.right-outer.left,outer.bottom-outer.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);UpdateWindow(a.window);
            // Native button theme transitions (e.g. disabled to enabled) also
            // animate. Let them settle before comparing stable resized pixels.
            auto settled=std::chrono::steady_clock::now()+std::chrono::milliseconds(300);while(std::chrono::steady_clock::now()<settled){pump();std::this_thread::sleep_for(std::chrono::milliseconds(5));}
            RECT footer,settings;GetWindowRect(a.get(5),&footer);GetWindowRect(a.get(OPEN_FOLDER),&settings);check(settings.bottom<footer.top,"minimum size keeps settings and footer apart");
            RECT client;GetClientRect(a.window,&client);for(auto [id,ctl]:a.controls)if(IsWindowVisible(ctl)){RECT box;GetWindowRect(ctl,&box);MapWindowPoints(nullptr,a.window,reinterpret_cast<POINT*>(&box),2);check(box.left>=0&&box.top>=0&&box.right<=client.right&&box.bottom<=client.bottom,"visible controls fit resized client");}
            auto actual=root/("resize-"+std::to_string(++resizeCase)+".bmp"),clean=root/L"resize-clean.bmp";a.snapshot(actual,true);RedrawWindow(a.window,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN|RDW_UPDATENOW);a.snapshot(clean,true);
            RECT animation;GetWindowRect(a.get(PROGRESS),&animation);MapWindowPoints(nullptr,a.window,reinterpret_cast<POINT*>(&animation),2);
            // The native progress bar animates independently between captures;
            // exclude only its pixels, while comparing all text and controls.
            auto stablePixels=[&](const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);std::string bytes(std::istreambuf_iterator<char>(in),{});size_t offset=sizeof(BITMAPFILEHEADER)+sizeof(BITMAPINFOHEADER);for(int y=animation.top;y<animation.bottom;++y){size_t first=offset+size_t(y*client.right+animation.left)*4,last=offset+size_t(y*client.right+animation.right)*4;std::fill(bytes.begin()+first,bytes.begin()+last,0);}return bytes;};if(_wgetenv(L"PUYO_SELFTEST_SKIP_PIXELS")&&stablePixels(actual)!=stablePixels(clean)){report<<"SKIP resized live text and controls equal clean repaint without trails (screen capture not available)\n";}else check(stablePixels(actual)==stablePixels(clean),"resized live text and controls equal clean repaint without trails");
        }
        a.snapshot(root/L"gui-editor.bmp");a.savedBoard=a.editor.board;click(EDIT_MODE);
        c=defaults();c.targetSuccessCount=10000000;c.restarts=2000000000;a.setSettings(c);click(START);SendMessageW(a.window,WM_CLOSE,0,0);awaitDone(3);check(!IsWindow(a.window),"window close cancels and joins worker");
        report<<"ALL GUI TESTS PASSED\n";return 0;
    }catch(const std::exception& e){report<<"ERROR "<<e.what()<<'\n';a.cancel=true;if(a.worker.joinable())a.worker.join();return 1;}
}
LRESULT CALLBACK boardProc(HWND h,UINT m,WPARAM w,LPARAM l){try{if(m==WM_PAINT){app->paintBoard(h);return 0;}if(m==WM_PRINTCLIENT){app->paintBoard(h,reinterpret_cast<HDC>(w));return 0;}if(m==WM_ERASEBKGND)return 1;
    if(m==WM_LBUTTONDOWN||m==WM_RBUTTONDOWN){app->beginStroke(GET_X_LPARAM(l),GET_Y_LPARAM(l),m==WM_RBUTTONDOWN);return 0;}
    if(m==WM_MOUSEMOVE&&app->stroking){app->drawCell(GET_X_LPARAM(l),GET_Y_LPARAM(l),app->strokePiece);return 0;}
    if(m==WM_LBUTTONUP||m==WM_RBUTTONUP||m==WM_CAPTURECHANGED||m==WM_CANCELMODE){app->endStroke();return 0;}
    }catch(const std::exception& e){app->error(e);}return DefWindowProcW(h,m,w,l);}
LRESULT CALLBACK windowProc(HWND h,UINT m,WPARAM w,LPARAM l){
    try{
        switch(m){
        case WM_CREATE:app->window=h;app->create();return 0;
        case WM_SIZE:if(w!=SIZE_MINIMIZED&&app->board)app->layout();return 0;
        case WM_GETMINMAXINFO:{auto p=reinterpret_cast<MINMAXINFO*>(l);RECT client{0,0,app->s(1080),app->s(897)};AdjustWindowRectExForDpi(&client,WS_OVERLAPPEDWINDOW,FALSE,0,app->dpi);p->ptMinTrackSize={client.right-client.left,client.bottom-client.top};return 0;}
        case WM_DRAWITEM:{auto item=reinterpret_cast<DRAWITEMSTRUCT*>(l);if(item->CtlID>=PIECE_FIRST&&item->CtlID<=PIECE_FIRST+Wall){app->paintPalette(*item);return TRUE;}break;}
        case WM_COMMAND:app->command(LOWORD(w),HIWORD(w));return 0;
        case WM_HSCROLL:if(reinterpret_cast<HWND>(l)==app->get(SLIDER))app->moveFrame(int(SendMessageW(app->get(SLIDER),TBM_GETPOS,0,0)));return 0;
        case PUMP:app->drain();return 0;
        case WM_TIMER:if(w==1){++app->uiTicks;app->drain();}else if(w==2){if(app->frame+1<int(app->frames.size())){++app->frame;app->showFrame();}else app->stopPlay();}return 0;
        case WM_CTLCOLORSTATIC:SetBkMode(reinterpret_cast<HDC>(w),OPAQUE);SetBkColor(reinterpret_cast<HDC>(w),RGB(245,247,250));SetTextColor(reinterpret_cast<HDC>(w),RGB(25,40,59));return reinterpret_cast<LRESULT>(app->background);
        case WM_CLOSE:app->endStroke();app->stopPlay();if(!app->confirmClose())return 0;if(app->running){app->closing=true;app->cancel=true;app->state=L"終了処理中: 部分成果を保存して閉じます。";app->updateEnabled();}else DestroyWindow(h);return 0;
        case WM_DESTROY:KillTimer(h,1);KillTimer(h,2);PostQuitMessage(0);return 0;
        }
    }catch(const std::exception& e){app->error(e);if(m==WM_CREATE)return -1;}
    return DefWindowProcW(h,m,w,l);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show){
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX ic{sizeof(ic),ICC_BAR_CLASSES|ICC_PROGRESS_CLASS};InitCommonControlsEx(&ic);
    App a;app=&a;WNDCLASSEXW wc{sizeof(wc)};wc.hInstance=instance;wc.lpfnWndProc=windowProc;wc.lpszClassName=L"PuyoGeneratorGui";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=a.background;wc.hIcon=LoadIconW(nullptr,IDI_APPLICATION);RegisterClassExW(&wc);
    wc.lpfnWndProc=boardProc;wc.lpszClassName=L"PuyoBoard";wc.hbrBackground=nullptr;RegisterClassExW(&wc);
    HWND h=CreateWindowExW(0,L"PuyoGeneratorGui",L"盤面ジェネレーター",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,1160,987,nullptr,nullptr,instance,nullptr);
    if(!h){CoUninitialize();return 2;}
    int argc;auto args=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(args && argc==3 && std::wstring(args[1])==L"--self-test") {int code=selfTest(a,args[2]);LocalFree(args);if(IsWindow(h))DestroyWindow(h);CoUninitialize();return code;}
    if(args && argc==4 && std::wstring(args[1])==L"--preview") {try{a.append(loadRecord(args[3]));ShowWindow(h,SW_SHOWNOACTIVATE);UpdateWindow(h);a.snapshot(args[2]);}catch(const std::exception& e){LocalFree(args);CoUninitialize();return 2;}LocalFree(args);DestroyWindow(h);CoUninitialize();return 0;}
    if(args && argc==4 && std::wstring(args[1])==L"--preview-board") {try{a.editor.replace(loadBoard(args[3]));a.savedBoard=a.editor.board;a.editorView();ShowWindow(h,SW_SHOWNOACTIVATE);UpdateWindow(h);a.snapshot(args[2]);}catch(const std::exception&){LocalFree(args);DestroyWindow(h);CoUninitialize();return 2;}LocalFree(args);DestroyWindow(h);CoUninitialize();return 0;}
    ShowWindow(h,show);UpdateWindow(h);
    // Opening a .puyo from the command line is read-only and fully revalidated.
    if(args && argc==2)try{std::filesystem::path p=args[1];if(p.extension()==L".puyoboard"){a.editor.replace(loadBoard(p));a.savedBoard=a.editor.board;a.editorView();}else a.append(loadRecord(p));}catch(const std::exception& e){a.error(e);}if(args)LocalFree(args);
    MSG msg;
    while(GetMessageW(&msg,nullptr,0,0)>0){
        if(msg.message==WM_KEYDOWN && (msg.hwnd==h || IsChild(h,msg.hwnd))){
            wchar_t cls[64];GetClassNameW(GetFocus(),cls,64);bool editing=wcscmp(cls,L"Edit")==0 || wcscmp(cls,L"ComboBox")==0;
            int focusId=GetDlgCtrlID(GetFocus());bool palette=focusId>=PIECE_FIRST&&focusId<=PIECE_FIRST+Wall;
            if(!editing && !palette && (a.selected>=0||a.editing)){int id=msg.wParam==VK_UP?PREV:msg.wParam=='F'?NEXT:msg.wParam==VK_SPACE?PLAY:0;if(a.editing&&(GetKeyState(VK_CONTROL)&0x8000))id=msg.wParam=='Z'?UNDO:msg.wParam=='Y'?REDO:0;if(id){a.command(id,BN_CLICKED);continue;}}
        }
        if(!IsDialogMessageW(h,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}
    }
    CoUninitialize();return 0;
}
