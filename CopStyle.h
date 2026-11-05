#pragma once
#include "Generator.h"
namespace mm 
{
    struct CopLayer 
    {
        std::string alias,source,target,display,style,scale,category;
        bool enabled=true,visible=true;
        bool manualTarget=false;
        std::string rgb; // Optional base vector color override; empty preserves XML.
    };
    struct CopProfile 
    { 
        fs::path file; 
        std::string xml; 
        std::vector<CopLayer> layers; 
    };
    struct CopOptions {
        fs::path gssRoot;
        std::string symbolBaseUrl; // HTTP export keeps portable symbols/ assets.
        fs::path sharedSymbolRoot=fs::u8path("D:/map/styles/symbols"); // PC-wide PNG library.
        int transparency=0; // 0: preserve; 1: magenta key; 2: white key.
    };
    CopProfile LoadCop(const fs::path& path);
    void MatchCop(CopProfile& profile,const std::vector<Layer>& data);
    struct CopSelection {
        std::vector<CopProfile> profiles;
        std::vector<std::string> unmatched;
        size_t matched=0;
    };
    CopSelection SelectCop(const std::vector<CopProfile>& profiles,const std::vector<Layer>& data);
    std::vector<Artifact> BuildCop(const std::vector<CopProfile>& profiles,const CopOptions& options);
    std::string BmpToPng(const fs::path& file,int transparency);
}
