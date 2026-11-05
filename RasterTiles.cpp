#include "RasterTiles.h"
#include <windows.h>
#include <gdal_priv.h>
#include <gdal_utils.h>
#include <gdalwarper.h>
#include <ogrsf_frmts.h>
#include <cpl_conv.h>
#include <algorithm>
#include <map>
#include <cpl_minixml.h>
#include "CopStyle.h"
#include <cmath>
#include <memory>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <stdexcept>
namespace mm { namespace {
constexpr double World=20037508.342789244;
using Dataset=std::unique_ptr<GDALDataset,void(*)(GDALDataset*)>;
Dataset Own(GDALDatasetH h){return Dataset((GDALDataset*)h,[](GDALDataset* p){if(p)GDALClose(p);});}
std::string Number(double n){std::ostringstream s;s.imbue(std::locale::classic());s<<std::setprecision(17)<<n;return s.str();}
struct Args {std::vector<std::string> strings;std::vector<char*> pointers;Args(std::initializer_list<std::string> a):strings(a){}char** get(){pointers.clear();for(auto& s:strings)pointers.push_back(s.data());pointers.push_back(nullptr);return pointers.data();}};
int Progress(double,const char*,void* p){return !static_cast<std::atomic_bool*>(p)->load();}
void Runtime(const fs::path& dll){
    static HMODULE loaded=nullptr;
    if(!loaded){if(!fs::is_regular_file(dll))throw std::runtime_error("Choose the installed GDAL DLL (gdal.d%ll).");
        loaded=LoadLibraryExW(fs::absolute(dll).make_preferred().c_str(),nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
        if(!loaded)throw std::runtime_error("Cannot load GDAL DLL/dependencies. Windows error "+std::to_string(GetLastError()));}
    GDALAllRegister();
    auto proj=dll.parent_path()/L"proj9/share";
    if(fs::is_regular_file(L"D:/map/styles/proj/proj.db"))proj=L"D:/map/styles/proj";
    if(fs::exists(proj/L"proj.db")){auto path=proj.u8string();const char* paths[]={path.c_str(),nullptr};OSRSetPROJSearchPaths(paths);}
    auto data=dll.parent_path()/L"gdal-data";if(fs::is_directory(data))CPLSetConfigOption("GDAL_DATA",data.u8string().c_str());
}
struct Range{int z;int64_t x0,y0,x1,y1;};
}
int ZoomForScale(double denominator,double latitude){
    if(!std::isfinite(denominator)||denominator<=0||!std::isfinite(latitude))throw std::runtime_error("Invalid target scale");
    double cosine=cos(std::clamp(latitude,-85.05112878,85.05112878)*3.141592653589793/180);
    return std::clamp((int)ceil(log2((2*World/256)*cosine/(denominator*0.0254/96))),0,24);
}
TileResult MakeVectorTiles(const std::vector<Layer>& layers,const TileOptions& opt,std::atomic_bool& cancel,const std::function<void(const std::string&)>& progress,const TileProgress& numeric){
    if(opt.minZoom<0||opt.minZoom>22||(!opt.autoZoom&&(opt.maxZoom<opt.minZoom||opt.maxZoom>22)))throw std::runtime_error("MVT zoom range must be 0..22.");
    Runtime(opt.gdalDll);auto driver=GetGDALDriverManager()->GetDriverByName("MVT");
    if(!driver||!driver->GetMetadataItem(GDAL_DCAP_CREATE))throw std::runtime_error("GDAL MVT write driver is unavailable.");
    TileResult result;fs::create_directories(opt.output);result.folder=opt.output/("mvt_"+std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));fs::create_directory(result.folder);
    std::ofstream catalog(result.folder/"map-sources.xml");catalog<<"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<MapSources version=\"1\">\n";
    auto escape=[](const std::string& s){char* p=CPLEscapeString(s.c_str(),-1,CPLES_XML);std::string v=p;CPLFree(p);return v;};
    size_t index=0;uint64_t planned=0;
    for(const auto& item:layers){if(cancel)break;++index;fs::path pending;
      try{
        if(numeric)numeric(index,layers.size(),0,100);progress("Reading SHP: "+item.local.u8string());
        auto src=Own(GDALOpenEx(item.local.u8string().c_str(),GDAL_OF_VECTOR|GDAL_OF_READONLY,nullptr,nullptr,nullptr));
        if(!src||!src->GetLayer(0))throw std::runtime_error("Cannot read SHP.");auto input=src->GetLayer(0);
        OGRSpatialReference fallback,merc;merc.importFromEPSG(3857);merc.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
        auto sr=input->GetSpatialRef();if(!sr){if(opt.fallbackEpsg<=0)throw std::runtime_error("Missing .prj: enter source EPSG.");fallback.importFromEPSG(opt.fallbackEpsg);sr=&fallback;}
        auto native=std::unique_ptr<OGRSpatialReference>(sr->Clone());native->SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
        auto ct=std::unique_ptr<OGRCoordinateTransformation,decltype(&OCTDestroyCoordinateTransformation)>(OGRCreateCoordinateTransformation(native.get(),&merc),OCTDestroyCoordinateTransformation);
        if(!ct)throw std::runtime_error("Cannot transform source CRS.");OGREnvelope env;if(input->GetExtent(&env,TRUE)!=OGRERR_NONE)throw std::runtime_error("Cannot determine SHP bounds.");
        double west,south,east,north;if(!ct->TransformBounds(env.MinX,env.MinY,env.MaxX,env.MaxY,&west,&south,&east,&north,21))throw std::runtime_error("Invalid SHP bounds.");
        west=std::max(-World,west);east=std::min(World,east);south=std::max(-World,south);north=std::min(World,north);
        if(west>east||south>north)throw std::runtime_error("Outside Web Mercator bounds.");
        int maxz=opt.autoZoom?std::max(opt.minZoom,std::min(22,ZoomForScale(opt.targetScale,atan(sinh((north+south)*0.5/(World/3.141592653589793)))*180/3.141592653589793))):opt.maxZoom;
        auto estimate=[&](int maximum){uint64_t count=0;for(int z=opt.minZoom;z<=maximum;++z){int64_t n=1LL<<z;double span=2*World/n;auto cell=[&](double v){return std::clamp((int64_t)floor(v),(int64_t)0,n-1);};count+=(cell((east+World)/span)-cell((west+World)/span)+1)*(cell((World-south)/span)-cell((World-north)/span)+1);}return count;};
        const int requested=maxz;uint64_t count=estimate(maxz);
        const uint64_t remaining=opt.maxTiles>planned?opt.maxTiles-planned:0;
        const uint64_t allowance=opt.autoZoom?remaining/(layers.size()-index+1):remaining;
        while(opt.autoZoom&&maxz>opt.minZoom&&count>allowance)count=estimate(--maxz);
        if(count>allowance)throw std::runtime_error("Tile limit exceeded: estimated="+std::to_string(count)+", available="+std::to_string(allowance)+", maxzoom="+std::to_string(maxz)+". Lower minimum/manual maximum zoom or increase tile limit.");
        if(maxz!=requested){auto message=item.local.u8string()+": AUTO ZOOM ADJUSTED "+std::to_string(requested)+" -> "+std::to_string(maxz)+" to include all selected files within tile limit.";result.messages.push_back(message);progress(message);}
        auto name=std::to_string(index)+"_"+item.local.stem().u8string();auto folder=result.folder/fs::u8path(name);pending=folder;pending+=".partial";
        Args args{"FORMAT=DIRECTORY","MINZOOM="+std::to_string(opt.minZoom),"MAXZOOM="+std::to_string(maxz),"COMPRESS=YES"};
        auto out=Own(driver->Create(pending.u8string().c_str(),0,0,0,GDT_Unknown,args.get()));if(!out)throw std::runtime_error("Cannot create MVT dataset.");
        auto layer=out->CreateLayer(item.local.stem().u8string().c_str(),&merc,input->GetGeomType(),nullptr);if(!layer)throw std::runtime_error("Cannot create MVT layer.");
        auto def=input->GetLayerDefn();for(int j=0;j<def->GetFieldCount();++j){OGRFieldDefn field(def->GetFieldDefn(j));auto type=field.GetType();if(type!=OFTInteger && type!=OFTInteger64 && type!=OFTReal && type!=OFTString)field.SetType(OFTString);if(layer->CreateField(&field)!=OGRERR_NONE)throw std::runtime_error("Cannot copy attribute schema.");}
        const GIntBig total=input->GetFeatureCount(TRUE);GIntBig done=0;input->ResetReading();
        while(!cancel){auto f=std::unique_ptr<OGRFeature,decltype(&OGRFeature::DestroyFeature)>(input->GetNextFeature(),OGRFeature::DestroyFeature);if(!f)break;
            auto dest=std::unique_ptr<OGRFeature,decltype(&OGRFeature::DestroyFeature)>(OGRFeature::CreateFeature(layer->GetLayerDefn()),OGRFeature::DestroyFeature);
            if(dest->SetFrom(f.get())!=OGRERR_NONE)throw std::runtime_error("Attribute copy failed.");dest->SetFID(f->GetFID());auto g=dest->GetGeometryRef();
            if(g&&g->transform(ct.get())!=OGRERR_NONE)throw std::runtime_error("Feature transform failed.");if(layer->CreateFeature(dest.get())!=OGRERR_NONE)throw std::runtime_error("MVT feature write failed.");
            ++done;if(numeric)numeric(index,layers.size(),(uint64_t)(total>0?done*90/total:0),100);
        }
        progress("Finalizing MVT tiles (GDAL): "+item.local.filename().u8string());CPLErrorReset();out.reset();
        if(CPLGetLastErrorType()>=CE_Failure)throw std::runtime_error(CPLGetLastErrorMsg());
        if(cancel){fs::remove_all(pending);break;}
        fs::rename(pending,folder);pending.clear();uint64_t written=0;for(auto& f:fs::recursive_directory_iterator(folder))if(f.path().extension()==".pbf")++written;result.written+=written;planned+=count;
        catalog<<"  <Layer id=\""<<result.folder.filename().u8string()<<"_"<<index<<"\" name=\""<<escape(item.local.stem().u8string())<<"\" type=\"VectorTile\">\n    <Path>"<<escape(fs::absolute(folder).u8string())<<"</Path>\n    <visual>true</visual>\n    <MinScale>0</MinScale><MaxScale>0</MaxScale>\n  </Layer>\n";
        result.messages.push_back(item.local.u8string()+" -> "+folder.u8string()+" (z"+std::to_string(opt.minZoom)+".."+std::to_string(maxz)+")");if(numeric)numeric(index,layers.size(),100,100);
      }catch(const std::exception& e){if(!pending.empty()){std::error_code ec;fs::remove_all(pending,ec);}if(!cancel){++result.failed;result.messages.push_back(item.local.u8string()+": "+e.what());}}
    }
    catalog<<"</MapSources>\n";result.cancelled=cancel;
    std::ofstream report(result.folder/"result.txt");report<<"MVT generation\nselected="<<layers.size()<<" processed="<<index<<" failed="<<result.failed<<" cancelled="<<result.cancelled<<" tiles="<<result.written<<"\n";
    report<<"autoZoom="<<opt.autoZoom<<" targetScale="<<opt.targetScale<<" minZoom="<<opt.minZoom<<" maxZoom="<<opt.maxZoom<<" maxTiles="<<opt.maxTiles<<"\n";
    for(const auto& input:layers)report<<"INPUT "<<input.local.u8string()<<"\n";
    for(const auto& message:result.messages)report<<message<<"\n";
    if(!cancel&&numeric)numeric(layers.size(),layers.size(),100,100);
    return result;
}

TileResult MakeRasterTiles(const std::vector<Layer>& layers,const TileOptions& opt,std::atomic_bool& cancel,const std::function<void(const std::string&)>& progress,const TileProgress& numeric){
    if(opt.minZoom<0||opt.minZoom>24||(!opt.autoZoom&&(opt.maxZoom>24||opt.maxZoom<opt.minZoom))||opt.maxTiles==0)throw std::runtime_error("Invalid zoom range (0..24) or tile limit.");
    if(opt.terrainRgb&&opt.jpeg)throw std::runtime_error("Elevation encoding requires lossless PNG");
    if(layers.empty())throw std::runtime_error("Check raster files first.");Runtime(opt.gdalDll);
    CPLSetThreadLocalConfigOption("GDAL_PAM_ENABLED","NO");
    struct Reset{~Reset(){CPLSetThreadLocalConfigOption("GDAL_PAM_ENABLED",nullptr);}} reset;
    fs::create_directories(opt.output);TileResult result;
    auto stamp=std::chrono::system_clock::now().time_since_epoch().count();result.folder=opt.output/("xyz_"+std::to_string(stamp));
    if(!fs::create_directory(result.folder))throw std::runtime_error("Cannot create unique tile output directory.");
    uint64_t planned=0;size_t index=0;
    for(const auto& layer:layers){if(cancel)break;++index;fs::path partial;
      try{
        if(numeric)numeric(index,layers.size(),0,0);
        progress("Inspecting "+layer.local.u8string());
        auto source=Own(GDALOpenEx(layer.local.u8string().c_str(),GDAL_OF_RASTER|GDAL_OF_READONLY,nullptr,nullptr,nullptr));
        if(!source)throw std::runtime_error("GDAL cannot read raster: "+std::string(CPLGetLastErrorMsg()));
        double transform[6];if(source->GetGeoTransform(transform)!=CE_None)throw std::runtime_error("Raster has no geotransform. Georeference it first.");
        bool hasCrs=source->GetSpatialRef()!=nullptr;if(!hasCrs&&opt.fallbackEpsg<=0)throw std::runtime_error("Raster CRS is missing. Enter its EPSG in the tile dialog.");
        int bands=source->GetRasterCount();if(!bands)throw std::runtime_error("Raster has no bands.");
        if(opt.terrainRgb&&bands!=1)throw std::runtime_error("Elevation input must have one band");
        int present=0;
        double elevationScale=source->GetRasterBand(1)->GetScale(&present);if(!present)elevationScale=1;
        double elevationOffset=source->GetRasterBand(1)->GetOffset(&present);if(!present)elevationOffset=0;
        std::string unit=source->GetRasterBand(1)->GetUnitType();
        double metres=(unit=="ft"||unit=="foot"||unit=="feet")?0.3048:1;
        if(opt.terrainRgb&&!unit.empty()&&metres==1&&unit!="m"&&unit!="metre"&&unit!="meter")
            throw std::runtime_error("Unsupported elevation unit: "+unit);
        Args normalization{"-of","VRT","-ot",opt.terrainRgb?"Float64":"Byte"};
        if(!hasCrs){normalization.strings.push_back("-a_srs");normalization.strings.push_back("EPSG:"+std::to_string(opt.fallbackEpsg));}
        if(!opt.terrainRgb&&source->GetRasterBand(1)->GetColorTable()){normalization.strings.insert(normalization.strings.end(),{"-expand","rgba"});}
        else if(bands>4){normalization.strings.insert(normalization.strings.end(),{"-b","1","-b","2","-b","3"});}
        if(!opt.terrainRgb&&source->GetRasterBand(1)->GetRasterDataType()!=GDT_Byte)normalization.strings.push_back("-scale");
        auto translateOptions=GDALTranslateOptionsNew(normalization.get(),nullptr);GDALTranslateOptionsSetProgress(translateOptions,Progress,&cancel);
        auto normalized=Own(GDALTranslate("",source.get(),translateOptions,nullptr));GDALTranslateOptionsFree(translateOptions);
        if(!normalized)throw std::runtime_error("Raster normalization failed: "+std::string(CPLGetLastErrorMsg()));
        char** transformerOptions=nullptr;transformerOptions=CSLSetNameValue(transformerOptions,"DST_SRS","EPSG:3857");
        void* transformer=GDALCreateGenImgProjTransformer2(normalized.get(),nullptr,transformerOptions);CSLDestroy(transformerOptions);
        if(!transformer)throw std::runtime_error("CRS transformation failed: "+std::string(CPLGetLastErrorMsg()));
        double geo[6],extent[4];int width=0,height=0;auto err=GDALSuggestedWarpOutput2(normalized.get(),GDALGenImgProjTransform,transformer,geo,&width,&height,extent,0);GDALDestroyGenImgProjTransformer(transformer);
        if(err!=CE_None)throw std::runtime_error("Cannot determine raster bounds.");
        for(double x:extent)if(!std::isfinite(x))throw std::runtime_error("Invalid raster bounds.");
        extent[0]=std::max(-World,extent[0]);extent[1]=std::max(-World,extent[1]);extent[2]=std::min(World,extent[2]);extent[3]=std::min(World,extent[3]);
        if(extent[0]>=extent[2]||extent[1]>=extent[3])throw std::runtime_error("Raster lies outside Web Mercator coverage.");
        int maxZoom=opt.maxZoom;
        if(opt.autoZoom){double latitude=atan(sinh((extent[1]+extent[3])*0.5/(World/3.141592653589793)))*180/3.141592653589793;
            maxZoom=std::max(opt.minZoom,ZoomForScale(opt.targetScale,latitude));
            result.messages.push_back(layer.local.u8string()+": scale 1:"+Number(opt.targetScale)+", zoom "+std::to_string(opt.minZoom)+".."+std::to_string(maxZoom)+" (96 DPI)");}
        if(opt.terrainRgb&&opt.autoZoom){
            double resolution=std::max(std::abs(geo[1]),std::abs(geo[5]));
            if(resolution>0)maxZoom=std::max(opt.minZoom,std::min(maxZoom,(int)std::ceil(std::log2(2*World/(256*resolution)))));
        }
        std::vector<Range> ranges;uint64_t count=0;
        for(int z=opt.minZoom;z<=maxZoom;++z){int64_t n=1LL<<z;double span=2*World/n;
            auto clamp=[&](double v){return std::clamp((int64_t)std::floor(v),(int64_t)0,n-1);};
            Range r{z,clamp((extent[0]+World)/span),clamp((World-extent[3])/span),clamp(std::nextafter((extent[2]+World)/span,-INFINITY)),clamp(std::nextafter((World-extent[1])/span,-INFINITY))};
            count+=(uint64_t)(r.x1-r.x0+1)*(r.y1-r.y0+1);ranges.push_back(r);
        }
        if(count>opt.maxTiles-planned)throw std::runtime_error("Tile limit exceeded: this file="+std::to_string(count)+", already planned="+std::to_string(planned)+", limit="+std::to_string(opt.maxTiles)+", max zoom="+std::to_string(maxZoom)+". Increase target scale denominator, reduce max zoom, or raise tile limit.");planned+=count;
        auto safeName=layer.local.stem().u8string();for(auto& c:safeName)if((unsigned char)c<32||std::string("<>:\"/\\|?*").find(c)!=std::string::npos)c='_';
        auto folder=result.folder/(std::to_string(index)+"_"+safeName);fs::create_directories(folder);
        if(opt.terrainRgb){std::ofstream marker(folder/".terrain-rgb");marker<<"TerrainRGB/1\n";if(!marker)throw std::runtime_error("Cannot write terrain marker");}
        uint64_t done=0;if(numeric)numeric(index,layers.size(),done,count);
        for(const auto& range:ranges){double span=2*World/(1LL<<range.z);
          for(int64_t x=range.x0;x<=range.x1&&!cancel;++x)for(int64_t y=range.y0;y<=range.y1&&!cancel;++y){
            const auto doneBefore = done;
            try
            {
            double xmin=-World+x*span,ymax=World-y*span;
            Args warp{"-of","MEM","-t_srs","EPSG:3857","-te",Number(xmin),Number(ymax-span),Number(xmin+span),Number(ymax),"-ts","256","256","-r","average","-dstalpha","-ot",opt.terrainRgb?"Float64":"Byte","-wm","64","-ovr","AUTO"};
            auto opts=GDALWarpAppOptionsNew(warp.get(),nullptr);GDALWarpAppOptionsSetProgress(opts,Progress,&cancel);GDALDatasetH src=normalized.get();
            auto tile=Own(GDALWarp("",nullptr,1,&src,opts,nullptr));GDALWarpAppOptionsFree(opts);if(cancel)break;
            if(!tile)throw std::runtime_error("Tile warp failed: "+std::string(CPLGetLastErrorMsg()));
            std::vector<unsigned char> alpha(256*256);if(tile->GetRasterBand(tile->GetRasterCount())->RasterIO(GF_Read,0,0,256,256,alpha.data(),256,256,GDT_Byte,0,0)!=CE_None)throw std::runtime_error("Cannot read tile alpha.");
            ++done;if(numeric)numeric(index,layers.size(),done,count);if(std::none_of(alpha.begin(),alpha.end(),[](unsigned char a){return a!=0;})){++result.empty;continue;}
            Dataset rgb=Own(nullptr);GDALDataset* output=tile.get();
            if(opt.terrainRgb){
                std::vector<double> heights(256*256);
                if(tile->GetRasterBand(1)->RasterIO(GF_Read,0,0,256,256,heights.data(),256,256,GDT_Float64,0,0)!=CE_None)
                    throw std::runtime_error("Cannot read elevation values");
                rgb=Own(GetGDALDriverManager()->GetDriverByName("MEM")->Create("",256,256,4,GDT_Byte,nullptr));
                if(!rgb)throw std::runtime_error("Cannot allocate encoded tile");
                std::vector<unsigned char> encoded(256*256*4,0);
                for(size_t i=0;i<heights.size();++i){
                    if(!alpha[i]||!std::isfinite(heights[i]))continue;
                    double h=(heights[i]*elevationScale+elevationOffset)*metres;
                    if(!std::isfinite(h)||h< -10000||h>1667721.5)throw std::runtime_error("Height outside Terrain-RGB range");
                    auto value=(unsigned)std::llround((h+10000)*10);
                    encoded[i*4]=(unsigned char)(value>>16);encoded[i*4+1]=(unsigned char)(value>>8);
                    encoded[i*4+2]=(unsigned char)value;encoded[i*4+3]=255;
                }
                for(int b=1;b<=4;++b){
                    rgb->GetRasterBand(b)->SetColorInterpretation((GDALColorInterp)(GCI_RedBand+b-1));
                    if(rgb->GetRasterBand(b)->RasterIO(GF_Write,0,0,256,256,encoded.data()+b-1,256,256,GDT_Byte,4,256*4)!=CE_None)
                        throw std::runtime_error("Cannot encode PNG channels");
                }
                output=rgb.get();
            }
            if(opt.jpeg){Args a{"-of","MEM"};int colors=tile->GetRasterCount()-1;for(int b=1;b<=std::min(colors,3);++b){a.strings.push_back("-b");a.strings.push_back(std::to_string(b));}
                auto t=GDALTranslateOptionsNew(a.get(),nullptr);rgb=Own(GDALTranslate("",tile.get(),t,nullptr));GDALTranslateOptionsFree(t);if(!rgb)throw std::runtime_error("Cannot prepare JPEG bands.");output=rgb.get();}
            auto target=folder/std::to_string(range.z)/std::to_string(x)/(std::to_string(y)+(opt.jpeg?".jpg":".png"));fs::create_directories(target.parent_path());partial=target;partial+=".partial";
            auto driver=GetGDALDriverManager()->GetDriverByName(opt.jpeg?"JPEG":"PNG");if(!driver)throw std::runtime_error("GDAL output driver is missing.");
            char** creation=nullptr;if(opt.jpeg)creation=CSLSetNameValue(creation,"QUALITY","90");
            auto saved=Own(driver->CreateCopy(partial.u8string().c_str(),output,FALSE,creation,Progress,&cancel));CSLDestroy(creation);
            if(!saved){std::error_code ec;fs::remove(partial,ec);if(cancel)break;throw std::runtime_error("Cannot save tile: "+std::string(CPLGetLastErrorMsg()));}saved.reset();
            if(cancel){std::error_code ec;fs::remove(partial,ec);break;}fs::rename(partial,target);partial.clear();++result.written;
            if(done==1||done%25==0)progress(layer.local.filename().u8string()+" "+std::to_string(done)+"/"+std::to_string(count));
            }
            catch (const std::exception& error)
            {
                if (!partial.empty())
                {
                    std::error_code ignored;
                    fs::remove(partial, ignored);
                    partial.clear();
                }
                if (cancel) break;
                ++result.failed;
                if (done == doneBefore) ++done;
                const std::string message = "FAILED TILE " + layer.local.u8string()
                    + " z/x/y=" + std::to_string(range.z) + "/" + std::to_string(x)
                    + "/" + std::to_string(y) + ": " + error.what();
                result.messages.push_back(message);
                std::ofstream errors(result.folder / "tile-errors.log", std::ios::app);
                errors << message << "\n";
                progress(message);
                if (numeric) numeric(index, layers.size(), done, count);
            }
          }
          if(cancel)break;
        }
        result.messages.push_back(layer.local.u8string()+" -> "+folder.u8string());
      }catch(const std::exception& error){if(!partial.empty()){std::error_code ec;fs::remove(partial,ec);}if(!cancel){++result.failed;result.messages.push_back(layer.local.u8string()+": "+error.what());}}
    }
    if(!cancel&&numeric)numeric(layers.size(),layers.size(),1,1);
    if(opt.terrainRgb){
        auto xml=CPLCreateXMLNode(nullptr,CXT_Element,"BaseMap");
        CPLSetXMLValue(xml,"Elevation.Path",fs::absolute(result.folder).u8string().c_str());
        CPLSetXMLValue(xml,"Elevation.visual","false");
        CPLSetXMLValue(xml,"Elevation.Format","TerrainRGB");
        CPLSetXMLValue(xml,"Elevation.VerticalReference","source");
        if(!CPLSerializeXMLTreeToFile(xml,(result.folder/"base-map-elevation.xml").u8string().c_str())){CPLDestroyXMLNode(xml);throw std::runtime_error("Cannot write elevation XML");}
        CPLDestroyXMLNode(xml);
        std::ofstream info(result.folder/"terrain-rgb-info.txt");
        info<<"TerrainRGB/1: elevation metres = -10000 + (R*65536+G*256+B)*0.1. Alpha 0=NoData.\nXYZ EPSG:3857, 256px, each source stored separately. Quantization <=0.05m after resampling; original vertical datum retained.\nKeep .terrain-rgb markers with PNG tiles. Point Elevation.Path in base-map.xml here; visual=false by default.\n";
    }
    result.cancelled=cancel.load();std::ofstream report(result.folder/"result.txt");report<<"XYZ EPSG:3857, 256x256. Each source has a separate folder. PNG preserves alpha; JPEG uses black for uncovered pixels. TerrainRGB preserves encoded metres at 0.1m; other imagery is scaled to Byte.\n";
    report<<"written="<<result.written<<" empty="<<result.empty<<" failed="<<result.failed<<" cancelled="<<result.cancelled<<"\n";for(const auto& message:result.messages)report<<message<<"\n";
    return result;
}

#include "ElevationTiles.inc"
#include "MergeTiles.inc"
}
