#pragma once
#include "CopStyle.h"
#include <map>
#include <regex>
#include <algorithm>
namespace mm {
inline void ApplyOgcMetadata(std::vector<Layer>& layers,const std::vector<CopProfile>& selected)
{
    std::map<std::string,CopProfile> loaded;
    auto key=[](std::string s){std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return static_cast<char>(c>='A'&&c<='Z'?c+32:c);});
        if(s.size()>4&&s.substr(s.size()-4)=="_tmk")s.resize(s.size()-4);return s;};
    for(auto& layer:layers)
    {
        std::smatch match;auto path=layer.local.generic_u8string();
        std::string profile;
        if(std::regex_search(path,match,std::regex("(EAR|EARTH|LAND|AIR)[_-]([0-9]+)M(?:/|$)",std::regex::icase)))
        {
            auto family=key(match[1]);layer.family=family=="air"?"공도":"육도";
            layer.displayScale=std::stod(match[2])*10000.0;
            profile="D:/map/styles/cop_"+std::string(family=="air"?"air":"land")+"_"+match[2].str()+"_unix.xml";
        }
        auto apply=[&](const CopProfile& p){
            for(const auto& entry:p.layers)
                if(entry.target==layer.name||key(entry.source)==key(layer.local.stem().u8string()))
                {
                    layer.displayName=entry.alias;layer.category=entry.category;
                    layer.visible=entry.visible;layer.localStyleXml=p.file.generic_u8string();
                    return true;
                }
            return false;
        };
        bool found=false;
        for(const auto& p:selected){if(apply(p)){found=true;break;}}
        if(!found&&!profile.empty()&&fs::is_regular_file(fs::u8path(profile)))
        {
            auto i=loaded.find(profile);
            if(i==loaded.end())i=loaded.emplace(profile,LoadCop(fs::u8path(profile))).first;
            apply(i->second);
        }
        if(layer.displayName.empty())layer.displayName=layer.local.stem().u8string();
    }
}
}
