#pragma once
#include "Generator.h"
#include <functional>
namespace mm {
struct TileOptions { fs::path output,gdalDll;int minZoom=0,maxZoom=12,fallbackEpsg=0;bool jpeg=false,autoZoom=false,merge=false,elevation=false;double targetScale=50000;uint64_t maxTiles=50000; };
struct TileResult { fs::path folder;uint64_t written=0,empty=0;int failed=0;bool cancelled=false;std::vector<std::string> messages; };
TileResult MakeVectorTiles(const std::vector<Layer>&,const TileOptions&,std::atomic_bool&,const std::function<void(const std::string&)>&,const std::function<void(size_t,size_t,uint64_t,uint64_t)>&);
int ZoomForScale(double denominator,double latitude);
using TileProgress=std::function<void(size_t,size_t,uint64_t,uint64_t)>;
TileResult MakeTiles(const std::vector<Layer>&,const TileOptions&,std::atomic_bool&,const std::function<void(const std::string&)>&,const TileProgress&);
TileResult MakeRasterTiles(const std::vector<Layer>& layers,const TileOptions& options,std::atomic_bool& cancel,const std::function<void(const std::string&)>& progress,const TileProgress& numeric={});
}
