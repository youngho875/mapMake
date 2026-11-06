#pragma once
#include "SldImport.h"
#include <chrono>
namespace mm {
// Import generated styles through the same validated path as manually selected SLD.
inline SldImport ImportGeneratedSld(const std::vector<Artifact>& artifacts,std::vector<Layer>& layers)
{
    auto base=fs::temp_directory_path();fs::path directory;
    for(int retry=0;retry<100;++retry){
        auto candidate=base/("mapmake-sld-"+std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count())+"-"+std::to_string(retry));
        if(fs::create_directory(candidate)){directory=candidate;break;}
    }
    if(directory.empty())throw std::runtime_error("Cannot create temporary SLD directory");
    struct Cleanup{fs::path path;~Cleanup(){std::error_code error;fs::remove_all(path,error);}} cleanup{directory};
    std::vector<fs::path> styles;
    for(const auto& a:artifacts){
        if(a.name.empty()||a.name.is_absolute()||a.name.has_root_name())throw std::runtime_error("Invalid generated style path");
        for(const auto& part:a.name)if(part=="..")throw std::runtime_error("Invalid generated style path");
        auto target=directory/a.name;fs::create_directories(target.parent_path());
        std::ofstream file(target,std::ios::binary);file.write(a.content.data(),static_cast<std::streamsize>(a.content.size()));file.close();
        if(!file)throw std::runtime_error("Cannot write generated style");
        if(a.name.extension()==".sld")styles.push_back(target);
    }
    return ImportSld(styles,layers);
}
}
