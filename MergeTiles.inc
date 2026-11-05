namespace {
std::string MergeLower(std::string s){for(auto& c:s)if(c>='A'&&c<='Z')c+=32;return s;}
struct MergeGroup{std::string key,code,family,scale;std::vector<Layer> inputs;};
std::vector<MergeGroup> MergeGroups(const std::vector<Layer>& input,bool raster){
 std::map<std::string,MergeGroup> groups;
 for(auto& item:input){std::string family=raster?"raster":"shape",scale="unspecified";
  for(auto part:item.local.parent_path()){auto p=MergeLower(part.u8string());
   for(auto prefix:{"ear_","land_","air_","kr_"})if(p.rfind(prefix,0)==0){family=std::string(prefix)=="ear_"||std::string(prefix)=="land_"?"land":std::string(prefix)=="air_"?"air":"kr";scale=p.substr(strlen(prefix));}
   if(p=="cib"||p=="satellite"||p=="satelite")family="cib";
   if(p.size()>1&&(p.back()=='m'||p.back()=='k')&&p.find_first_not_of("0123456789._mk")==std::string::npos)scale=p;
  }
  if (raster && MergeLower(item.local.extension().u8string()) == ".i42") family = "cib";
  auto code=raster?"mosaic":MergeLower(item.local.stem().u8string());auto key=family+"_"+scale+"_"+code;
  auto& g=groups[key];g.key=key;g.code=code;g.family=family;g.scale=scale;g.inputs.push_back(item);
 }
 std::vector<MergeGroup> result;for(auto& entry:groups){auto& g=entry.second;std::sort(g.inputs.begin(),g.inputs.end(),[](const Layer&a,const Layer&b){return a.local<b.local;});result.push_back(std::move(g));}return result;
}
void MergeRaster(const MergeGroup& group,const fs::path& folder,const TileOptions& opt,std::atomic_bool& cancel,const std::function<void(const std::string&)>& note, std::vector<std::string>& errors){
 std::vector<std::string> paths;int i=0;
  const bool directCib = group.family == "cib" && std::all_of(
      group.inputs.begin(), group.inputs.end(), [](const Layer& input)
      {
          return MergeLower(input.local.extension().u8string()) == ".i42";
      });

 for(auto& item:group.inputs){try { if(cancel)return;note("Mosaic source: "+item.local.u8string());auto ds=Own(GDALOpenEx(item.local.u8string().c_str(),GDAL_OF_RASTER,nullptr,nullptr,nullptr));if(!ds)throw std::runtime_error("Cannot read "+item.local.u8string());
  // Standard single-band Byte CIB frames can be referenced directly.
  // Keep the whole group homogeneous; do not mix RGB wrappers and gray frames.
  if (directCib)
  {
      if (ds->GetRasterCount() != 1 || !ds->GetSpatialRef()
          || ds->GetRasterBand(1)->GetRasterDataType() != GDT_Byte)
          throw std::runtime_error("Unsupported CIB layout: expected georeferenced single-band Byte");
      paths.push_back(item.local.u8string());
      continue;
  }
  Args args{"-of","VRT","-ot","Byte"};int bands=ds->GetRasterCount();if(!bands)throw std::runtime_error("No raster bands");
  if(!ds->GetSpatialRef()){if(!opt.fallbackEpsg)throw std::runtime_error("Missing raster CRS: "+item.local.u8string());args.strings.insert(args.strings.end(),{"-a_srs","EPSG:"+std::to_string(opt.fallbackEpsg)});}
  if(ds->GetRasterBand(1)->GetColorTable())args.strings.insert(args.strings.end(),{"-expand","rgba"});
  else if(bands<=2){args.strings.insert(args.strings.end(),{"-b","1","-b","1","-b","1"});if(bands==2)args.strings.insert(args.strings.end(),{"-b","2"});args.strings.insert(args.strings.end(),{"-colorinterp",bands==2?"red,green,blue,alpha":"red,green,blue"});}
  else {args.strings.insert(args.strings.end(),{"-b","1","-b","2","-b","3"});if(bands>=4&&ds->GetRasterBand(4)->GetColorInterpretation()==GCI_AlphaBand)args.strings.insert(args.strings.end(),{"-b","4"});}
  if(ds->GetRasterBand(1)->GetRasterDataType()!=GDT_Byte)args.strings.push_back("-scale");
  auto normalized=folder/("source_"+std::to_string(++i)+".vrt");auto t=GDALTranslateOptionsNew(args.get(),nullptr);GDALTranslateOptionsSetProgress(t,Progress,&cancel);auto src=Own(GDALTranslate(normalized.u8string().c_str(),ds.get(),t,nullptr));GDALTranslateOptionsFree(t);if(cancel)return;if(!src)throw std::runtime_error("Mosaic normalization failed");
  // CIB frames share their native geographic CRS. Mosaic these directly and
  // reproject only the requested output tile, avoiding nested warped VRT reads.
  if (group.family == "cib")
  {
      src.reset();
      paths.push_back(normalized.u8string());
      continue;
  }

  // Keep reprojection virtual: no full-resolution intermediate TIFF is written.
  const auto projected = folder / ("projected_" + std::to_string(i) + ".vrt");
  Args warpArgs{
      "-of", "VRT", "-t_srs", "EPSG:3857", "-dstalpha",
      "-r", "bilinear", "-wm", "64"
  };
  auto warpOptions = GDALWarpAppOptionsNew(warpArgs.get(), nullptr);
  GDALWarpAppOptionsSetProgress(warpOptions, Progress, &cancel);
  GDALDatasetH sourceHandle = src.get();
  auto warped = Own(GDALWarp(projected.u8string().c_str(), nullptr,
      1, &sourceHandle, warpOptions, nullptr));
  GDALWarpAppOptionsFree(warpOptions);

  if (cancel)
      return;
  if (!warped)
      throw std::runtime_error("Mosaic reprojection failed: " + std::string(CPLGetLastErrorMsg()));

  // Warped VRT overviews are virtual and bound low-zoom reprojection work.
  std::vector<int> overviewLevels;
  int longest = (std::max)(warped->GetRasterXSize(), warped->GetRasterYSize());
  for (int factor = 2; longest / factor >= 1; factor *= 2)
  {
      overviewLevels.push_back(factor);
      if (longest / factor <= 128)
          break;
  }
  if (!overviewLevels.empty())
  {
      note("Building mosaic overviews: " + item.local.u8string());
      const auto error = warped->BuildOverviews("AVERAGE",
          static_cast<int>(overviewLevels.size()), overviewLevels.data(),
          0, nullptr, Progress, &cancel);
      if (cancel)
          return;
      if (error != CE_None)
          throw std::runtime_error("Mosaic overview failed: " + std::string(CPLGetLastErrorMsg()));
  }
  warped.reset();
  paths.push_back(projected.u8string());
  }
  catch (const std::exception& error)
  {
      if (cancel) return;
      const std::string message = "SKIPPED SOURCE " + item.local.u8string() + ": " + error.what();
      errors.push_back(message);
      note(message);
  }
 }
 if (paths.empty())
     throw std::runtime_error("No readable raster sources in group " + group.key);
 std::vector<const char*> names;for(auto& s:paths)names.push_back(s.c_str());Args ba{"-resolution","highest","-strict"};if(group.family=="cib")ba.strings.push_back("-addalpha");auto b=GDALBuildVRTOptionsNew(ba.get(),nullptr);GDALBuildVRTOptionsSetProgress(b,Progress,&cancel);auto vrt=Own(GDALBuildVRT((folder/"mosaic.vrt").u8string().c_str(),(int)names.size(),nullptr,names.data(),b,nullptr));GDALBuildVRTOptionsFree(b);if(!cancel&&!vrt)throw std::runtime_error("Mosaic VRT failed: "+std::string(CPLGetLastErrorMsg()));
 if (cancel)
     return;

 // Expose mosaic-wide virtual overviews to GDALWarp. Source overviews alone
 // are not advertised by a multi-source VRT, so the warp may still read full size.
 std::vector<int> mosaicLevels;
 const int mosaicSize = (std::max)(vrt->GetRasterXSize(), vrt->GetRasterYSize());
 for (int factor = 2; mosaicSize / factor >= 1; factor *= 2)
 {
     mosaicLevels.push_back(factor);
     if (mosaicSize / factor <= 128)
         break;
 }
 if (!mosaicLevels.empty())
 {
     const char* current = CPLGetThreadLocalConfigOption("VRT_VIRTUAL_OVERVIEWS", nullptr);
     const bool hadValue = current != nullptr;
     const std::string previous = current ? current : "";
     CPLSetThreadLocalConfigOption("VRT_VIRTUAL_OVERVIEWS", "YES");
     const auto error = vrt->BuildOverviews("AVERAGE",
         static_cast<int>(mosaicLevels.size()), mosaicLevels.data(),
         0, nullptr, Progress, &cancel);
     CPLSetThreadLocalConfigOption("VRT_VIRTUAL_OVERVIEWS", hadValue ? previous.c_str() : nullptr);
     if (!cancel && error != CE_None)
         throw std::runtime_error("Mosaic virtual overview failed: " + std::string(CPLGetLastErrorMsg()));
 }

}
void MergeShape(const MergeGroup& group,const fs::path& file,const TileOptions& opt,std::atomic_bool& cancel,const std::function<void(const std::string&)>& note){
 auto driver=GetGDALDriverManager()->GetDriverByName("GPKG");if(!driver)throw std::runtime_error("GDAL GPKG driver required");auto out=Own(driver->Create(file.u8string().c_str(),0,0,0,GDT_Unknown,nullptr));if(!out)throw std::runtime_error("Cannot create merge workspace");
 OGRSpatialReference merc;merc.importFromEPSG(3857);merc.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);auto target=out->CreateLayer(group.code.c_str(),&merc,wkbUnknown,nullptr);if(!target)throw std::runtime_error("Cannot create merged layer");
 std::map<std::string,OGRFieldType> types;std::map<std::string,std::string> names;int dimension=-1;
 for(auto& item:group.inputs){if(cancel)return;auto ds=Own(GDALOpenEx(item.local.u8string().c_str(),GDAL_OF_VECTOR,nullptr,nullptr,nullptr));if(!ds||!ds->GetLayer(0))throw std::runtime_error("Cannot read "+item.local.u8string());auto layer=ds->GetLayer(0);auto def=layer->GetLayerDefn();
  auto kind=OGR_GT_GetCollection(OGR_GT_Flatten(layer->GetGeomType()));if(dimension<0)dimension=kind;else if(dimension!=kind)throw std::runtime_error("Geometry types differ in group "+group.key);
  for(int i=0;i<def->GetFieldCount();++i){auto f=def->GetFieldDefn(i);auto key=MergeLower(f->GetNameRef());if(key=="_mm_source"||key=="_mm_fid")throw std::runtime_error("Reserved source field collision: "+key);auto type=f->GetType();if(type!=OFTInteger&&type!=OFTInteger64&&type!=OFTReal)type=OFTString;
   if(!types.count(key)){types[key]=type;names[key]=f->GetNameRef();}else if(types[key]!=type){if((types[key]==OFTInteger||types[key]==OFTInteger64)&&(type==OFTInteger||type==OFTInteger64))types[key]=OFTInteger64;else types[key]=OFTString;}
  }
 }
 for(auto& entry:types){OGRFieldDefn field(names[entry.first].c_str(),entry.second);if(target->CreateField(&field)!=OGRERR_NONE)throw std::runtime_error("Cannot create merged attribute");}
 OGRFieldDefn sf("_mm_source",OFTString),fid("_mm_fid",OFTInteger64);target->CreateField(&sf);target->CreateField(&fid);GIntBig sequence=0;
 for(auto& item:group.inputs){if(cancel)return;note("Merge SHP: "+item.local.u8string());auto ds=Own(GDALOpenEx(item.local.u8string().c_str(),GDAL_OF_VECTOR,nullptr,nullptr,nullptr));auto layer=ds->GetLayer(0);OGRSpatialReference fallback;auto sr=layer->GetSpatialRef();if(!sr){if(!opt.fallbackEpsg)throw std::runtime_error("Missing SHP CRS: "+item.local.u8string());fallback.importFromEPSG(opt.fallbackEpsg);sr=&fallback;}
  auto native=std::unique_ptr<OGRSpatialReference>(sr->Clone());native->SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);auto ct=std::unique_ptr<OGRCoordinateTransformation,decltype(&OCTDestroyCoordinateTransformation)>(OGRCreateCoordinateTransformation(native.get(),&merc),OCTDestroyCoordinateTransformation);if(!ct)throw std::runtime_error("Merge CRS transform failed");
  if(target->StartTransaction()!=OGRERR_NONE)throw std::runtime_error("Merge transaction failed");
  while(!cancel){auto src=std::unique_ptr<OGRFeature,decltype(&OGRFeature::DestroyFeature)>(layer->GetNextFeature(),OGRFeature::DestroyFeature);if(!src)break;auto f=std::unique_ptr<OGRFeature,decltype(&OGRFeature::DestroyFeature)>(OGRFeature::CreateFeature(target->GetLayerDefn()),OGRFeature::DestroyFeature);
   if(f->SetFrom(src.get(),TRUE)!=OGRERR_NONE)throw std::runtime_error("Merge attribute copy failed");f->SetFID(++sequence);f->SetField("_mm_source",item.local.u8string().c_str());f->SetField("_mm_fid",src->GetFID());auto g=f->GetGeometryRef();if(g&&g->transform(ct.get())!=OGRERR_NONE)throw std::runtime_error("Merge feature projection failed");if(target->CreateFeature(f.get())!=OGRERR_NONE)throw std::runtime_error("Merge feature write failed");
  }
  if(target->CommitTransaction()!=OGRERR_NONE)throw std::runtime_error("Merge commit failed");
 }
}
}
#include "MergeStylePackage.inc"
#include "RasterPreflight.inc"
TileResult MakeTiles(const std::vector<Layer>& layers,const TileOptions& options,std::atomic_bool& cancel,const std::function<void(const std::string&)>& progress,const TileProgress& numeric){
 if(options.terrainRgb)return MakeRasterTiles(layers,options,cancel,progress,numeric);
 if(options.elevation)return MakeElevationTiles(layers,options,cancel,progress,numeric);
 if(layers.empty())throw std::runtime_error("Select data files");bool raster=layers.front().type=="RASTER";auto create=raster?MakeRasterTiles:MakeVectorTiles;if(!options.merge){auto result=create(layers,options,cancel,progress,numeric);if(!raster&&!result.cancelled){std::vector<MergeGroup> individual;for(auto& l:layers){auto one=MergeGroups({l},false);one.front().code=l.local.stem().u8string();individual.push_back(one.front());}PackageMergeStyles(individual,result);}return result;}
 Runtime(options.gdalDll);fs::create_directories(options.output);auto root=options.output/("_merge_"+std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));fs::create_directory(root);struct Cleanup{fs::path p;~Cleanup(){std::error_code ec;fs::remove_all(p,ec);}} cleanup{root};
 auto groups=MergeGroups(layers,raster);std::vector<Layer> prepared;std::vector<std::string> messages;size_t i=0;std::vector<std::string> sourceErrors;
 const auto effective = raster ? PreflightRasterTiles(groups, options, cancel, progress, messages) : options;
 for(auto& group:groups){if(cancel)break;auto folder=root/std::to_string(++i);fs::create_directory(folder);Layer merged;merged.type=raster?"RASTER":"VECTOR";merged.local=folder/(raster?"mosaic.vrt":group.code+".gpkg");merged.name=group.key;
  try {
  progress("Preparing group "+group.key+" ("+std::to_string(group.inputs.size())+" files)");if(raster)MergeRaster(group,folder,options,cancel,progress,sourceErrors);else MergeShape(group,merged.local,options,cancel,progress);if(cancel)break;prepared.push_back(merged);
  messages.push_back("GROUP "+group.key+": "+std::to_string(group.inputs.size())+" files");for(auto& item:group.inputs)messages.push_back("  INPUT "+item.local.u8string());if(numeric)numeric(1,1,i*2000/groups.size(),10000);
  }
  catch (const std::exception& error)
  {
      if (!raster) throw;
      if (cancel) break;
      const std::string message = "FAILED GROUP " + group.key + ": " + error.what();
      sourceErrors.push_back(message);
      progress(message);
  }
 }
 if(cancel){TileResult r;r.folder=options.output;r.cancelled=true;return r;}
 if (prepared.empty())
 {
     TileResult result;
     result.folder = root;
     result.failed = static_cast<int>(sourceErrors.size());
     result.messages = sourceErrors;
     std::ofstream errors(options.output / "merge-errors.log", std::ios::app);
     for (const auto& message : sourceErrors) errors << message << "\n";
     result.folder = options.output;
     return result;
 }
 auto r=create(prepared,effective,cancel,progress,[&](size_t f,size_t files,uint64_t done,uint64_t total){if(numeric)numeric(1,1,2000+(uint64_t)(8000*((f?f-1:0)+(total?double(done)/total:0))/(files?files:1)),10000);});
 if(!raster&&!r.cancelled)PackageMergeStyles(groups,r);
 r.failed += static_cast<int>(sourceErrors.size());
 if (!sourceErrors.empty())
 {
     std::ofstream errors(r.folder / "merge-errors.log");
     for (const auto& message : sourceErrors) errors << message << "\n";
     std::ofstream summary(r.folder / "result.txt", std::ios::app);
     summary << "Including skipped sources/groups: total_failed=" << r.failed << "\n";
     for (const auto& message : sourceErrors) summary << message << "\n";
 }
 r.messages.insert(r.messages.end(), sourceErrors.begin(), sourceErrors.end());
 r.messages.insert(r.messages.begin(),messages.begin(),messages.end());std::ofstream report(r.folder/"merge-report.txt");report<<"Raster overlap priority: lexically later path wins where valid. SHP overlaps are preserved.\n";for(auto& message:r.messages)report<<message<<"\n";return r;
}
