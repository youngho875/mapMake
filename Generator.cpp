#include "Generator.h"
#include "OgcHelp.h"
#include "XmlOutput.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <locale>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
namespace mm {
namespace {
std::string Lower(std::string s) 
{ 
    for(auto& c:s) 
        if(c>='A'&&c<='Z')
            c=char(c-'A'+'a'); 
    return s; 
}
std::string Escape(const std::string& s, bool xml) 
{
    std::string out=xml?"":"\"";
    for(unsigned char c:s) 
    {
        if(c<32 || c==127) 
        throw 
            std::runtime_error("Control characters are not allowed in settings or paths.");
        if(xml) 
        { 
            switch(c) 
            { 
            case '&':
            out+="&amp;";
            break;
            case '<':
            out+="&lt;";
            break;
            case '>':
            out+="&gt;";
            break;
            case '"':
            out+="&quot;";
            break;
            case '\'':
            out+="&apos;";
            break;
            default:
                out+=char(c); 
            } 
        }
        else 
        { 
            if(c=='\\'||c=='"') 
                out+='\\';out+=char(c); 
        }
    
    } 
    return xml?out:out+'"';
}


std::string Q(const std::string& s) 
{
    return Escape(s,false);
} 
std::string X(const std::string& s) 
{
    return Escape(s,true);
}
std::string UrlEncode(const std::string& s) 
{
    const char* h="0123456789ABCDEF";
    std::string out;
    for(unsigned char c:s) 
    {
        if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~') 
            out+=char(c);
    else 
        {
            out+='%';out+=h[c>>4];out+=h[c&15];
        }
    }
    return out;
}
void CheckUrl(const std::string& s)
{
    if(!std::regex_match(s,std::regex("https?://[^/?# \\t\\r\\n]+[^?# \\t\\r\\n]*")))
    throw 
        std::runtime_error("Enter an http(s) server URL without query, fragment or spaces.");
}
std::string ServerPath(std::string root, const fs::path& rel)
{
    std::replace(root.begin(),root.end(),'\\','/');
    if(root.empty() || !(root.front()=='/'||(root.size()>2&&root[1]==':'&&root[2]=='/')))
    throw 
        std::runtime_error("Server data root must be an absolute Windows or Linux path.");
    while(root.size()>1 && root.back()=='/')
        root.pop_back();
    return root+(root.back()=='/'?"":"/")+rel.generic_u8string();
}
fs::path Sidecar(const fs::path& shp, const std::string& ext) 
{
    auto direct=shp; direct.replace_extension(ext);
    if(fs::is_regular_file(direct))
        return direct;
    for(const auto& e:fs::directory_iterator(shp.parent_path()))
        if(Lower(e.path().stem().u8string())==Lower(shp.stem().u8string()) && Lower(e.path().extension().u8string())==ext)
            return e.path();
    return {};
}
std::vector<std::string> DbfFields(const fs::path& path)
{
    std::ifstream in(path,std::ios::binary); std::array<unsigned char,32> head{};
    if(!in.read(reinterpret_cast<char*>(head.data()),32))
    throw
        std::runtime_error("Invalid DBF header");
    unsigned len=head[8]+256u*head[9];
    if(len<33 || (len-33)%32!=0) 
    throw 
        std::runtime_error("Unsupported DBF field header");
    std::vector<std::string> fields;
    for(unsigned pos=32;pos+32<len;pos+=32) 
    {
        std::array<char,32> field{};
        if(!in.read(field.data(),32)) 
        throw 
            std::runtime_error("Truncated DBF fields");
        size_t n=0;
        while(n<11&&field[n]) 
            ++n;
        fields.emplace_back(field.data(),n);
    }
    if(in.get()!=13) 
    throw 
        std::runtime_error("Invalid DBF terminator");
    return fields;
}
void InspectShape(Layer& layer)
{
    std::ifstream in(layer.local,std::ios::binary);
    std::array<unsigned char,100> head{};
    if(!in.read(reinterpret_cast<char*>(head.data()),100)||head[0]!=0||head[1]!=0||head[2]!=0x27||head[3]!=0x0a)
    throw 
        std::runtime_error("Invalid Shapefile header");
    unsigned t=head[32]+256u*head[33]+65536u*head[34]+16777216u*head[35];
    switch(t) 
    {
    case 1:
    case 8:
    case 11:
    case 18:
    case 21:
    case 28:
        layer.type="POINT";
        break;
    case 3:
    case 13:
    case 23:
    layer.type="LINE";
    break;
    case 5:
    case 15:
    case 25:
        layer.type="POLYGON";
        break;
    default:
        throw 
            std::runtime_error("Unsupported Shapefile geometry");
    }
    auto dbf=Sidecar(layer.local,".dbf"),shx=Sidecar(layer.local,".shx");
    if(dbf.empty()||shx.empty()) 
    throw 
        std::runtime_error("Missing .dbf or .shx sidecar");
    layer.fields=DbfFields(dbf);
    for(const auto& field:layer.fields) 
        if(Lower(field)=="id"||Lower(field)=="fid"||Lower(field)=="objectid") 
        {
            layer.featureId=field;
            break;
        }
}
std::string Symbolizer(const Layer& l,const std::string& color) {
    auto stroke="<Stroke><CssParameter name=\"stroke\">"+color+"</CssParameter><CssParameter name=\"stroke-width\">2</CssParameter></Stroke>";
    auto fill="<Fill><CssParameter name=\"fill\">"+color+"</CssParameter></Fill>";
    if(l.type=="RASTER") return "<RasterSymbolizer><Opacity>1</Opacity></RasterSymbolizer>";
    if(l.type=="LINE") return "<LineSymbolizer>"+stroke+"</LineSymbolizer>";
    if(l.type=="POLYGON") return "<PolygonSymbolizer>"+fill+"<Stroke><CssParameter name=\"stroke\">#3c3c3c</CssParameter><CssParameter name=\"stroke-width\">1</CssParameter></Stroke></PolygonSymbolizer>";
    return "<PointSymbolizer><Graphic><Mark><WellKnownName>circle</WellKnownName>"+fill+"</Mark><Size>8</Size></Graphic></PointSymbolizer>";
}
}
std::string NormalizeRgb(const std::string& value) {
    std::istringstream in(value);int r,g,b;std::string extra;
    if(!(in>>r>>g>>b)||(in>>extra)||r<0||r>255||g<0||g>255||b<0||b>255)
        throw std::runtime_error("RGB must contain three integers from 0 to 255.");
    return std::to_string(r)+" "+std::to_string(g)+" "+std::to_string(b);
}
std::string RgbHex(const std::string& value) {
    std::istringstream in(NormalizeRgb(value));int r,g,b;in>>r>>g>>b;
    std::ostringstream out;out<<'#'<<std::hex<<std::setfill('0')<<std::setw(2)<<r<<std::setw(2)<<g<<std::setw(2)<<b;return out.str();
}
void ValidateEpsg(const std::string& value) 
{
    if(!std::regex_match(value,std::regex("[1-9][0-9]{0,5}"))) 
    throw 
        std::runtime_error("EPSG must be a numeric code, such as 4326 or 5179.");
}
std::vector<std::string> ParseExtensions(std::string text) {
    for(auto& c:text) if(c==';'||c==','||c=='\t'||c=='\r'||c=='\n') c=' ';
    std::istringstream stream(text);std::string token;std::set<std::string> result;
    while(stream>>token) {
        if(token.rfind("*.",0)==0)
            token.erase(0,2);
        else if(token.front()=='.')
            token.erase(0,1);
        token=Lower(token);
        if(!std::regex_match(token,std::regex("[a-z0-9_][a-z0-9_-]*"))||token=="shp"||token=="dbf"||token=="shx"||token=="prj")
        throw 
            std::runtime_error("Raster extensions: use tif;img;dem or *.tif. Shapefile sidecar extensions are reserved.");
        result.insert("."+token);
    }
    if(result.empty()) 
    throw 
        std::runtime_error("Enter at least one raster extension (default: tif).");

    return {result.begin(),result.end()};
}

ScanResult Scan(const fs::path& input,const std::string& extensions,bool recursive,const std::atomic_bool* cancel) 
{
    auto exts=ParseExtensions(extensions);
    auto root=fs::absolute(input).lexically_normal();
    if(!fs::is_directory(root))
    throw 
        std::runtime_error("Input folder does not exist.");
    ScanResult result;
    auto inspect=[&](const fs::directory_entry& e) 
    {
        if(cancel&&cancel->load())  return;
        std::error_code ec;
        if(e.is_symlink(ec))    return;
        if(!e.is_regular_file(ec))
        {
            if(ec)
                result.warnings.push_back(e.path().u8string()+": "+ec.message());
            return;
        }
        auto ext=Lower(e.path().extension().u8string());
        if(ext!=".shp"&&std::find(exts.begin(),exts.end(),ext)==exts.end())
            return;
        Layer l;
        l.local=e.path();
        l.relative=e.path().lexically_relative(root);
        l.type="RASTER";
        if(ext==".shp")
            try
        {
            InspectShape(l);
        }
        catch(const std::exception& err)
        {
            l.type="INVALID";
            l.issue=err.what();
        }
        result.layers.push_back(std::move(l));
    };
    // Explicit traversal reports inaccessible directories and never follows directory symlinks.
    std::vector<fs::path> pending{root};
    while(!pending.empty() && !(cancel&&cancel->load())) 
    {
        auto dir=pending.back();
        pending.pop_back();
        std::error_code ec;
        fs::directory_iterator it(dir,ec),end;
        if(ec)
        {
            result.warnings.push_back(dir.u8string()+": "+ec.message());
            continue;
        }
        while(it!=end && !(cancel&&cancel->load())) 
        {
            auto e=*it;inspect(e);
            if(recursive&&!e.is_symlink(ec)&&e.is_directory(ec))
                pending.push_back(e.path());
            it.increment(ec);
            if(ec)
            {
                result.warnings.push_back(dir.u8string()+": "+ec.message());
                break;
            }
        }
    }
    std::sort(result.layers.begin(),result.layers.end(),[](const Layer& a,const Layer& b)
        { return a.relative.generic_u8string()<b.relative.generic_u8string();});
    std::set<std::string> used;
    for(auto& l:result.layers) 
    {
        std::string base="layer_";
        auto rel=l.relative;rel.replace_extension();
        for(unsigned char c:rel.generic_u8string())
            base+=(c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')?char(c):'_';
        if(base.size()>100)
            base.resize(100);
        l.name=base;
        int n=2;
        while(!used.insert(Lower(l.name)).second)
            l.name=base+"_"+std::to_string(n++);
    }
    return result;
}
std::vector<Artifact> Build(const Settings& s,const std::vector<Layer>& layers) 
{
    if(layers.empty())
    throw 
        std::runtime_error("Check at least one file in the list.");
    if(!s.wms&&!s.wfs&&!s.wmts)
    throw 
        std::runtime_error("Select WMS, WFS or WMTS.");
    ValidateEpsg(s.mapEpsg);
    CheckUrl(s.publicUrl);
    if(s.mapUnits!="DD"&&s.mapUnits!="METERS"&&s.mapUnits!="FEET")
    throw 
        std::runtime_error("Select map coordinate units: DD, METERS or FEET.");
    if(s.wmts)
    {
        CheckUrl(s.internalUrl);
        CheckUrl(s.wmtsUrl);
        if(s.cache.empty())
        throw 
            std::runtime_error("Enter a server cache path.");
    }
    if(s.mapPath.empty())
    throw 
        std::runtime_error("Enter the deployed server mapfile path.");
    double a,b,c,d;
    std::string tail;
    std::istringstream box(s.extent);
    box.imbue(std::locale::classic());
    if(!(box>>a>>b>>c>>d)||(box>>tail)||!std::isfinite(a)||!std::isfinite(b)||!std::isfinite(c)||!std::isfinite(d)||a>=c||b>=d)
    throw 
        std::runtime_error("Map extent must be minX minY maxX maxY in the MAP CRS, with min < max.");
    int r,g,blue;
    std::istringstream rgb(s.rgb);
    if(!(rgb>>r>>g>>blue)||(rgb>>tail)||r<0||r>255||g<0||g>255||blue<0||blue>255)
    throw 
        std::runtime_error("RGB must be three integers from 0 to 255.");
    std::ostringstream hex;hex<<'#'<<std::hex<<std::setfill('0')<<std::setw(2)<<r<<std::setw(2)<<g<<std::setw(2)<<blue;
    bool hasVector=false;std::set<std::string> names;
    for(const auto& l:layers) 
    {
        if(!l.issue.empty()||l.type=="INVALID")
        throw 
            std::runtime_error(l.relative.u8string()+": "+l.issue);
        if(l.type!="RASTER"&&l.type!="POINT"&&l.type!="LINE"&&l.type!="POLYGON")
        throw 
            std::runtime_error("Unsupported layer type.");
        ValidateEpsg(l.epsg);
        if(!std::regex_match(l.name,std::regex("[A-Za-z][A-Za-z0-9_]*"))||!names.insert(Lower(l.name)).second)
        throw 
            std::runtime_error("Invalid or duplicate layer name.");
        if(!fs::is_regular_file(l.local))
        throw 
            std::runtime_error("Source file is missing: "+l.local.u8string());
        if(l.relative.empty()||l.relative.is_absolute())
        throw 
            std::runtime_error("Invalid relative data path.");
        for(const auto& part:l.relative)
            if(part=="..")
            throw 
                std::runtime_error("Relative data path escapes the input root.");
        if(l.type!="RASTER") 
        {
            hasVector=true;
            if(s.wfs&&!l.featureId.empty()&&std::find(l.fields.begin(),l.fields.end(),l.featureId)==l.fields.end())
            throw
                std::runtime_error(l.relative.u8string()+": Choose an existing DBF ID field, or leave it empty for automatic record IDs.");
        }       
        else if(s.wfs&&!s.wms&&!s.wmts)
        throw
            std::runtime_error("WFS only supports vector layers. Uncheck raster files or enable WMS/WMTS.");
    }
    if(s.wfs&&!hasVector)
    throw 
        std::runtime_error("WFS is selected but no vector files are checked. Disable WFS or select a Shapefile.");
    bool render=s.wms||s.wmts;
    auto online=s.publicUrl+"?map="+UrlEncode(s.mapPath)+"&";
    std::ostringstream m,cache,sld,manifest;
    m<<"MAP\n  NAME \"mapMake\"\n  STATUS ON\n  SIZE 256 256\n  EXTENT "<<s.extent<<"\n  UNITS "<<s.mapUnits<<"\n  PROJECTION\n    "<<Q("init=epsg:"+s.mapEpsg)<<"\n  END\n"
     <<"  IMAGETYPE \"png\"\n  OUTPUTFORMAT\n    NAME \"png\"\n    DRIVER \"AGG/PNG\"\n    MIMETYPE \"image/png\"\n    IMAGEMODE RGBA\n    EXTENSION \"png\"\n    TRANSPARENT ON\n  END\n"
     <<"  SYMBOL\n    NAME \"circle\"\n    TYPE ELLIPSE\n    FILLED TRUE\n    POINTS\n      1 1\n    END\n  END\n  WEB\n    METADATA\n"
     <<"      \"ows_title\" \"mapMake\"\n      \"ows_onlineresource\" "<<Q(online)<<"\n      \"ows_srs\" "<<Q("EPSG:"+s.mapEpsg+" EPSG:3857 EPSG:4326")
     <<"\n      \"wms_enable_request\" "<<Q(render?"GetCapabilities GetMap GetFeatureInfo":"!*")
     <<"\n      \"wms_getfeatureinfo_formatlist\" \"application/json\""
     <<"\n      \"wfs_enable_request\" "<<Q(s.wfs?"GetCapabilities DescribeFeatureType GetFeature":"!*")<<"\n    END\n  END\n";
    if(render)m<<"  OUTPUTFORMAT\n    NAME \"geojson\"\n    DRIVER \"OGR/GEOJSON\"\n    MIMETYPE \"application/json\"\n    FORMATOPTION \"STORAGE=stream\"\n    FORMATOPTION \"FORM=SIMPLE\"\n  END\n";
    cache<<"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<mapcache>\n  <cache name=\"disk\" type=\"disk\"><base>"<<X(s.cache)<<"</base></cache>\n";
    sld<<"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<StyledLayerDescriptor version=\"1.0.0\" xmlns=\"http://www.opengis.net/sld\" xmlns:ogc=\"http://www.opengis.net/ogc\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" xsi:schemaLocation=\"http://www.opengis.net/sld http://schemas.opengis.net/sld/1.0.0/StyledLayerDescriptor.xsd\">\n";
    manifest<<"mapMake generation report\nWMS="<<s.wms<<" WFS="<<s.wfs<<" WMTS="<<s.wmts<<" SLD="<<s.sld<<"\n";
    if(s.wmts&&!s.wms)manifest<<"WMS is enabled as the source for WMTS. Restrict direct WMS access at the web server if needed.\n";
    manifest<<"Map CRS: EPSG:"<<s.mapEpsg<<"; extent: "<<s.extent<<"\nRaster formats are selected by suffix; GDAL on the server must support their real content.\nRaster SLD uses the default image appearance (no ColorMap or band remapping).\nExplicit WFS ID fields must contain nonempty unique values. Empty ID uses a virtual OGR record ID without modifying the source DBF. Record IDs may change after dataset edits/reordering.\n\n";
    for(const auto& l:layers) 
    {
        const auto layerRgb=NormalizeRgb(l.rgb.empty()?s.rgb:l.rgb);
        int r,g,blue;std::istringstream(layerRgb)>>r>>g>>blue;
        auto data=ServerPath(s.serverRoot,l.relative);bool vector=l.type!="RASTER";
        std::string effectiveId=l.featureId;
        const bool automaticId=s.wfs&&vector&&effectiveId.empty();
        m<<"  LAYER\n    NAME "<<Q(l.name)<<"\n    TYPE "<<l.type<<"\n    STATUS ON\n";
        if(automaticId)
        {
            effectiveId="mm_fid";
            for(unsigned suffix=1;std::any_of(l.fields.begin(),l.fields.end(),[&](const std::string& f){return Lower(f)==Lower(effectiveId);});++suffix)
                effectiveId="mm_fid_"+std::to_string(suffix);
            std::string table=l.relative.stem().u8string(),quoted;
            for(char ch:table){if(ch=='"')quoted+='"';quoted+=ch;}
            const auto sql="SELECT FID AS "+effectiveId+", * FROM \""+quoted+"\"";
            m<<"    CONNECTIONTYPE OGR\n    CONNECTION "<<Q(data)<<"\n    DATA "<<Q(sql)<<'\n';
            manifest<<"AUTO WFS ID: "<<l.name<<" -> "<<effectiveId<<" (OGR FID; original DBF unchanged)\n";
        }
        else m<<"    DATA "<<Q(data)<<'\n';
        m<<"    PROJECTION\n      "<<Q("init=epsg:"+l.epsg)<<"\n    END\n    METADATA\n"
         <<"      \"ows_title\" "<<Q(l.displayName.empty()?l.relative.generic_u8string():l.displayName)<<"\n      \"wfs_enable_request\" "<<Q(s.wfs&&vector?"GetCapabilities DescribeFeatureType GetFeature":"!*")<<'\n';
        if(!l.sldFile.empty())m<<"      \"SLD_USE_FIRST_NAMEDLAYER\" \"false\"\n";
        if(vector)m<<"      \"gml_include_items\" \"all\"\n      \"gml_types\" \"auto\"\n";
        if(vector&&!effectiveId.empty())m<<"      \"gml_featureid\" "<<Q(effectiveId)<<'\n';
        m<<"    END\n";
        if(vector&&render)m<<"    TEMPLATE \"queryable\"\n    TOLERANCE 7\n    TOLERANCEUNITS PIXELS\n";
        if(!l.sldFile.empty())m<<"    STYLEITEM "<<Q("sld://"+l.sldFile)<<'\n';
        if(vector&&l.sldFile.empty()) 
        {
            m<<"    CLASS\n      NAME \"default\"\n      STYLE\n        COLOR "<<r<<' '<<g<<' '<<blue<<'\n';
            if(l.type=="POINT")m<<"        SYMBOL \"circle\"\n        SIZE 8\n";
            if(l.type=="LINE")m<<"        WIDTH 2\n";
            if(l.type=="POLYGON")m<<"        OUTLINECOLOR 60 60 60\n        WIDTH 1\n";
            m<<"      END\n    END\n";
        }
        m<<"  END\n";
        if(s.wmts)
            cache<<"  <source name=\"src_"<<X(l.name)<<"\" type=\"wms\">\n    <getmap><params><FORMAT>image/png</FORMAT><LAYERS>"<<X(l.name)<<"</LAYERS><MAP>"<<X(s.mapPath)<<"</MAP><TRANSPARENT>true</TRANSPARENT></params></getmap>\n    <http><url>"<<X(s.internalUrl)<<"</url></http>\n  </source>\n  <tileset name=\""<<X(l.name)<<"\"><source>src_"<<X(l.name)<<"</source><cache>disk</cache><grid>GoogleMapsCompatible</grid><format>PNG</format><metatile>4 4</metatile><metabuffer>10</metabuffer></tileset>\n";
        if(s.sld)
            sld<<"  <NamedLayer><Name>"<<X(l.name)<<"</Name><UserStyle><Name>default</Name><Title>"<<X(l.relative.generic_u8string())<<"</Title><IsDefault>1</IsDefault><FeatureTypeStyle><Rule>"<<Symbolizer(l,RgbHex(layerRgb))<<"</Rule></FeatureTypeStyle></UserStyle></NamedLayer>\n";
        if(!l.sldFile.empty())manifest<<"SLD: "<<l.sldFile<<" (requires MapServer 8.2+)\n";
        manifest<<l.name<<" | "<<l.type<<" | EPSG:"<<l.epsg<<" | WFS="<<(s.wfs&&vector)<<" | ID="<<l.featureId<<"\n  local: "<<l.local.u8string()<<"\n  server: "<<data<<"\n";
    }
    m<<"END\n";cache<<"  <service type=\"wmts\" enabled=\"true\"/>\n</mapcache>\n";
    sld<<"</StyledLayerDescriptor>\n";
    std::vector<Artifact> out{{"mapserver.map",m.str()}};
    if(s.wmts)out.push_back({"mapcache.xml",PrettyXml(cache.str())});
    if(s.sld)out.push_back({"styles.sld",PrettyXml(sld.str())});
    // C4IMap profiles are separate so the user chooses one service explicitly.
    for(const auto& service:std::vector<std::string>{"WMS","WFS","WMTS"})
    {
        if((service=="WMS"&&!s.wms)||(service=="WFS"&&!s.wfs)||(service=="WMTS"&&!s.wmts))continue;
        std::ostringstream xml;
        xml<<"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<MapSources version=\"1\">\n";
        for(const auto& l:layers)
        {
            if(service=="WFS"&&l.type=="RASTER")continue;
            const auto title=l.displayName.empty()?l.name:l.displayName;
            xml<<"  <Layer id=\""<<Lower(service)<<"_"<<X(l.name)<<"\" name=\""<<X(title)
               <<"\" type=\""<<service<<"\" family=\""<<X(l.family.empty()?"지도":l.family)<<"\">\n";
            auto tag=[&](const char* name,const std::string& value){xml<<"    <"<<name<<">"<<X(value)<<"</"<<name<<">\n";};
            tag("Category",l.category.empty()?"일반":l.category);
            tag("Scale",std::to_string(l.displayScale));
            tag("visual",l.visible?"true":"false");
            tag("Endpoint",service=="WMTS"?s.wmtsUrl+"?SERVICE=WMTS&REQUEST=GetCapabilities":online);
            tag("LayerName",l.name);tag("Version","1.1.1");tag("Format","image/png");
            tag("MinScale","0");tag("MaxScale","0");
            if(!l.localStyleXml.empty())tag("LocalStyleXML",l.localStyleXml);
            if(service=="WMTS")
            {
                tag("Style","default");tag("TileMatrixSet","GoogleMapsCompatible");
                if(l.type!="RASTER"){
                    tag("IdentifyType",s.wfs?"WFS":"WMS");
                    tag("IdentifyEndpoint",online);tag("IdentifyLayer",l.name);
                }
            }
            xml<<"  </Layer>\n";
        }
        xml<<"</MapSources>\n";
        out.push_back({Lower(service)+"-sources.xml",PrettyXml(xml.str())});
    }
    out.push_back({"README-client-xml.txt",
        "C4IMap: choose wms-sources.xml, wfs-sources.xml or wmts-sources.xml and copy it beside C4IMap.exe as ogc-sources.xml.\n"
        "Only selected services are exported. WFS excludes raster layers. WMTS uses WFS identify when enabled, otherwise WMS GetFeatureInfo.\n"
        "Deploy mapserver.map to the configured server map path and mapcache.xml to MapCache. Configure the WMTS URL in Apache; this tool does not configure Apache.\n"
        "Keep map-sources.xml/local-sources.xml/tile-sources.xml separate. Import a profile from XML map source settings for a temporary switch.\n"});
    out.push_back({fs::u8path("입력란-설명.txt"),OgcInputHelp()});
    out.push_back({"generation-report.txt",manifest.str()});
    return out;
}
fs::path SaveBatch(const fs::path& output,const std::vector<Artifact>& artifacts) 
{
    if(output.empty()||artifacts.empty())
    throw 
        std::runtime_error("Output folder and artifacts are required.");
    fs::create_directories(output);
    auto stamp=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    auto base="mapMake-"+std::to_string(stamp);
    fs::path staging,final;
    for(int i=0;;++i) 
    {
        final=output/(base+"-"+std::to_string(i));
        staging=output/("."+base+"-"+std::to_string(i)+".tmp");
        if(fs::exists(final))
            continue;
        if(fs::create_directory(staging))
            break;
    }
    try 
    {
        for(const auto& a:artifacts) 
        {
            if(a.name.empty()||a.name.is_absolute()||a.name.has_root_name())
            throw 
                std::runtime_error("Invalid artifact filename.");
            for(const auto& part:a.name)
                if(part==".."||part==".")
                throw 
                    std::runtime_error("Invalid artifact path.");
            fs::create_directories((staging/a.name).parent_path());
            std::ofstream out(staging/a.name,std::ios::binary);
            out<<a.content;out.close();
            if(!out)
            throw 
                std::runtime_error("Cannot write output: "+a.name.u8string());
        }
        fs::rename(staging,final);
    }catch(...)
    {
        std::error_code ec;fs::remove_all(staging,ec);throw;
    }
    return final;
}
}
