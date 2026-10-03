#pragma once
#include "gui_editor.h"

namespace puyo_gui {
enum class UrlFormat { Ishikawa, PuyoPark, Mattulwan };
inline constexpr std::array<const char*,3> urlFormatNames{
    "石川ぷよ", "対戦ぷよパーク", "mattulwan系（pndsngミラー）"};
inline constexpr std::array<const char*,3> urlFormatIds{"ishikawa","puyop","mattulwan"};

// Export only the completed 6 x 13 field. Engine identity and CLI history keep
// using fieldToUrl; changing a destination cannot change the search or dedupe.
inline std::string urlUnavailableReason(const Field& field,UrlFormat format) {
    for(const auto& col:field.col){if(col.size()>13)return "14段目は出力できません。";for(Cell v:col){if(v>Wall)return "未対応のぷよ種別です。";if(v==PointPuyo&&format!=UrlFormat::Mattulwan)return "得点ぷよに対応していないため、このサイトへは出力できません。";}}
    return {};
}
inline std::string simulatorUrl(const Field& field, UrlFormat format) {
    auto reason=urlUnavailableReason(field,format);if(!reason.empty())throw std::runtime_error(reason);
    bool special=false;for(const auto& col:field.col)for(Cell v:col)special|=v>=PointPuyo;
    auto longCode=[&](const std::array<int,11>& mapping){std::string code;for(int y=12;y>=0;--y)for(int x=0;x<6;++x){Cell v=size_t(y)<field.col[x].size()?field.col[x][y]:0;code+=char('0'+mapping[v]);}auto first=code.find_first_not_of('0');return first==std::string::npos?std::string{}:code.substr(first);};
    switch(format) {
    case UrlFormat::Ishikawa:
        if(special)return "https://ishikawapuyo.net/simu/pe.html?~"+longCode({0,1,2,3,4,5,6,-1,9,8,7});
        return fieldToUrl(field);
    case UrlFormat::PuyoPark:
        // Both formats pack two RGBYP values into six bits, top row first.
        // Values 62/63 cannot occur with the generator's five normal colors.
        if(special)return "https://www.puyop.com/s/="+longCode({0,1,2,3,4,5,6,-1,7,8,9});
        return "https://www.puyop.com/s/" + fieldToUrl(field).substr(std::string("https://ishikawapuyo.net/simu/pe.html?").size());
    case UrlFormat::Mattulwan: {
        constexpr char symbols[]="abecdfgjkhi";
        std::string code;
        char previous=0;
        int count=0;
        auto flush=[&]{if(count){code+=previous;if(count>1)code+=std::to_string(count);}};
        for(int y=12;y>=0;--y)for(int x=0;x<6;++x){
            Cell value=size_t(y)<field.col[x].size()?field.col[x][y]:0;
            char symbol=symbols[value];
            if(symbol!=previous){flush();previous=symbol;count=0;}
            ++count;
        }
        flush();
        return "https://www.pndsng.com/puyo/index.html?"+code;
    }
    }
    throw std::runtime_error("不明なURL形式です。");
}
inline void writeSimulatorUrls(const std::filesystem::path& path,
                               const std::vector<Record>& records, UrlFormat format) {
    for(const auto& record:records){auto reason=urlUnavailableReason(record.solution.field,format);if(!reason.empty())throw std::runtime_error(reason);}
    std::ofstream out(path,std::ios::binary);
    if(!out)throw std::runtime_error("URL一覧を保存できません。");
    for(const auto& record:records)out<<simulatorUrl(record.solution.field,format)<<'\n';
    out.flush();
    if(!out)throw std::runtime_error("URL一覧の書き込みに失敗しました。");
}
}
