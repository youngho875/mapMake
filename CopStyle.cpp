#include "CopStyle.h"
#include "XmlOutput.h"
#include "StandardRules.h"
#include "vendor/tinyxml2/tinyxml2.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <map>
#include <memory>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#ifdef _WIN32
#include <Windows.h>
#else
#include <iconv.h>
#endif
namespace mm 
{
    namespace 
    {
    using Node=tinyxml2::XMLElement;
    std::string Lower(std::string s)
    {
        for(auto& c:s)
            if(c>='A'&&c<='Z')
                c=char(c+32);
        return s;
    }

    std::string Trim(std::string s)
    {
        auto a=s.find_first_not_of(" \r\n\t");
        return a==s.npos?"":s.substr(a,s.find_last_not_of(" \r\n\t")-a+1);
    }
    std::string A(const Node* n,const char* key)
    {
        const char* v=n?n->Attribute(key):nullptr;
        return v?v:"";
    }
    std::string T(const Node* n,const char* key,const std::string& fallback="")
    {
        auto x=n?n->FirstChildElement(key):nullptr;
        return x&&x->GetText()?Trim(x->GetText()):fallback;
    }
    std::string X(const std::string& s)
    {
        std::string r;
        for(unsigned char c:s)
        {
            switch(c)
            {
            case '&':
            r+="&amp;";
            break;
            case '<':
            r+="&lt;";
            break;
            case '>':
            r+="&gt;";
            break;
            case '"':
            r+="&quot;";
            break;
            case '\'':
            r+="&apos;";
            break;
            default:
                if(c<32&&c!=9&&c!=10&&c!=13)
                throw
                    std::runtime_error("Invalid XML text");
                r+=char(c);
            }
        }
        return r;
    }

    bool Yes(const std::string& s)
    {
        return Lower(s)=="true"||s=="1";
    }

    std::string ReadXml(const fs::path& p)
    {
        if(fs::file_size(p)>32*1024*1024)
        throw 
            std::runtime_error("XML file exceeds 32 MB limit");

        std::ifstream in(p,std::ios::binary);
        if(!in)
        throw 
            std::runtime_error("Cannot open XML: "+p.u8string());
        std::string s((std::istreambuf_iterator<char>(in)),{});
        auto decl=Lower(s.substr(0,250));
        bool kr=decl.find("euc-kr")!=decl.npos||decl.find("cp949")!=decl.npos||decl.find("ks_c_5601")!=decl.npos;
        if(kr)
        {
#ifdef _WIN32
        int n=MultiByteToWideChar(949,MB_ERR_INVALID_CHARS,
            s.data(),static_cast<int>(s.size()),nullptr,0);
        if(!n)
            throw std::runtime_error("Invalid EUC-KR/CP949 XML");

        std::wstring w(n,L'\0');
        MultiByteToWideChar(949,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),&w[0],n);
        int m=WideCharToMultiByte(CP_UTF8,0,w.data(),n,nullptr,0,nullptr,nullptr);
        std::string u(m,'\0');
        WideCharToMultiByte(CP_UTF8,0,w.data(),n,&u[0],m,nullptr,nullptr);
        s=std::move(u);
#else
        iconv_t cd=iconv_open("UTF-8","CP949");
        if(cd==(iconv_t)-1)
            throw std::runtime_error("CP949 converter unavailable");
        std::string out(s.size()*4+4,'\0');
        char* src=&s[0];
        char* dst=&out[0];
        size_t left=s.size(),space=out.size();
        auto result=iconv(cd,&src,&left,&dst,&space);
        iconv_close(cd);
        if(result==size_t(-1))
            throw std::runtime_error("Invalid CP949 XML");
        out.resize(out.size()-space);s=std::move(out);
#endif
        }
        if(s.rfind("\xef\xbb\xbf",0)==0)
            s.erase(0,3);
        if(s.rfind("<?xml",0)==0)
        {
            auto end=s.find("?>");
            if(end!=s.npos)
                s.erase(0,end+2);
        }
        if(s.find("<!DOCTYPE")!=s.npos||s.find("<!ENTITY")!=s.npos)
            throw std::runtime_error("DTD/entity declarations are not supported");

        return s;
    }

    std::unique_ptr<tinyxml2::XMLDocument> Parse(const std::string& s)
    {
        auto d=std::make_unique<tinyxml2::XMLDocument>();
        if(d->Parse(s.data(),s.size())!=tinyxml2::XML_SUCCESS)
            throw std::runtime_error(std::string("XML parse error: ")+d->ErrorStr());
        return d;
    }
    std::string Num(const std::string& v,const std::string& fallback="1",bool zero=true)
    {
        if(v.empty())
            return fallback;
        size_t p=0;
        double d=std::stod(v,&p);
        if(p!=v.size()||!std::isfinite(d)||d<0||(!zero&&d==0))
            throw std::runtime_error("Invalid style number: "+v);
        return v;
    }
    std::string Color(std::string s)
    {
        if(s.empty())
            return "#000000";
        if(s.front()=='#')
            s.erase(0,1);
        if(!std::regex_match(s,std::regex("[0-9a-fA-F]{6}")))
        throw 
            std::runtime_error("Invalid color: "+s);
        return "#"+s;
    }
    std::string Css(const std::string& key,const std::string& val)
    {
        return "<CssParameter name=\""+key+"\">"+X(val)+"</CssParameter>";
    }
    std::string Code(const std::string& name)
    {
        if(name.size()>2&&name[0]=='[')
        {
            auto p=name.find(']');
            if(p!=name.npos)
                return name.substr(1,p-1);
        }
        return name;
    }
    struct Context 
    {
        CopOptions options;std::map<std::string,fs::path> symbols;std::map<std::string,std::string> generated;
        std::map<std::string,const Node*> gss;std::unique_ptr<tinyxml2::XMLDocument> gssDoc;
        std::vector<Artifact> assets;std::set<std::string> warnings;

        void Warn(const std::string& s)
        {
            warnings.insert(s);
        }
        // Content-versioned names are independent of scale, input order and GSS location.
        // Existing files are reused only after comparing their full contents.
        std::string Publish(const fs::path& source,const std::string& bytes)
        {
            if(!options.symbolBaseUrl.empty() || options.sharedSymbolRoot.empty())
            {
                std::string relative="symbols/symbol_"+std::to_string(generated.size()+1)+".png";
                assets.push_back({fs::u8path(relative),bytes});
                return relative;
            }
            unsigned long long hash=14695981039346656037ULL;
            for(unsigned char ch:bytes){hash^=ch;hash*=1099511628211ULL;}
            std::string stem=Lower(source.stem().u8string());
            for(char& ch:stem)
                if(!((ch>='a'&&ch<='z')||(ch>='A'&&ch<='Z')||(ch>='0'&&ch<='9')||ch=='_'||ch=='-'))ch='_';
            if(stem.empty())stem="symbol";
            std::ostringstream filename;
            filename<<stem<<"_"<<std::hex<<std::setw(16)<<std::setfill('0')<<hash<<".png";
            auto root=fs::absolute(options.sharedSymbolRoot);
            fs::create_directories(root);
            auto target=root/fs::u8path(filename.str());
            if(fs::exists(target))
            {
                std::ifstream in(target,std::ios::binary);
                std::string existing((std::istreambuf_iterator<char>(in)),{});
                if(existing!=bytes)throw std::runtime_error("Shared symbol content conflict: "+target.u8string());
                return target.generic_u8string();
            }
            // Publish a complete PNG, never a partially written image.
            auto temp=target;
#ifdef _WIN32
            temp+=L"."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(GetCurrentThreadId())+L".tmp";
#else
            temp+=".tmp";
#endif
            try
            {
                {std::ofstream out(temp,std::ios::binary|std::ios::trunc);
                 out.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));out.close();
                 if(!out)throw std::runtime_error("Cannot write shared symbol: "+temp.u8string());}
                std::error_code error;
                fs::rename(temp,target,error);
                if(error)
                {
                    std::ifstream in(target,std::ios::binary);
                    std::string existing((std::istreambuf_iterator<char>(in)),{});
                    if(existing!=bytes)throw std::runtime_error("Cannot publish shared symbol: "+error.message());
                    fs::remove(temp);
                }
            }
            catch(...){std::error_code ignored;fs::remove(temp,ignored);throw;}
            return target.generic_u8string();
        }
        std::string Graphic(const std::string& name,const std::string& size="")
        {
            if(name.empty())
                return "";
            auto source=fs::u8path(name);
            bool direct=source.is_absolute()&&fs::is_regular_file(source);
            auto key=direct?source.generic_u8string():Lower(source.stem().u8string());
            if(direct)symbols[key]=source;
            auto i=symbols.find(key);
            if(i==symbols.end())
            {
                Warn("MISSING SYMBOL: "+name);
                return "";
            }
            auto j=generated.find(key);std::string path;
            if(j!=generated.end())path=j->second;else
            {
                try
                {
                    if(Lower(i->second.extension().u8string())==".png"){
                        std::ifstream in(i->second,std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(in)),{});
                        if(bytes.size()<8||bytes.compare(0,8,"\x89PNG\r\n\x1a\n",8)!=0)
                            throw std::runtime_error("Invalid PNG signature");
                        path=Publish(i->second,bytes);
                        if(options.transparency)
                            Warn("PNG alpha preserved; color-key option applies only to BMP: "+name);
                    }else
                        path=Publish(i->second,BmpToPng(i->second,options.transparency));
                    generated[key]=path;
                }
                catch(const std::exception& e)
                {
                    Warn("SYMBOL ERROR: "+name+": "+e.what());
                    return "";
                }
            }
            std::string base=options.symbolBaseUrl;
            while(!base.empty()&&base.back()=='/')
                base.pop_back();
            auto url=base.empty()?path:base+"/"+path;
            return "<Graphic><ExternalGraphic><OnlineResource xlink:type=\"simple\" xlink:href=\""+X(url)+"\"/><Format>image/png</Format></ExternalGraphic>"+(size.empty()?"":"<Size>"+Num(size)+"</Size>")+"</Graphic>";
        }
    };
    std::string Stroke(const std::string& color,const std::string& width,const std::string& dash="",const std::string& graphic="")
    {
        return "<Stroke>"+(graphic.empty()?"":"<GraphicStroke>"+graphic+"</GraphicStroke>")+
            Css("stroke",Color(color))+Css("stroke-width",Num(width))+(dash.empty()?"":Css("stroke-dasharray",dash))+"</Stroke>";
    }
    // Use semantic GSS dash definitions instead of guessing proprietary numeric line codes.
    std::string Dash(const Node* style,Context& c)
    {
        auto custom=T(style,"DashArray");
        if(!custom.empty())
        {
            std::istringstream input(custom);
            std::string token,out;
            while(input>>token)
            {
                if(!out.empty())
                    out+=' ';
                out+=Num(token,"1",false);
            }
            return out;
        }
        auto it=c.gss.find(Code(A(style,"name")));
        if(it==c.gss.end())
            return "";
        const Node* line=it->second->FirstChildElement("LineLayer");
        if(!line)
            return "";
        const Node* dash=line->FirstChildElement("Dash");
        if(!dash)
            return "";
        std::string out;
        for(auto e=dash->FirstChildElement("DashItem"); e; e=e->NextSiblingElement("DashItem"))
        {
            if(!e->GetText())
                continue;
            if(!out.empty())
                out+=' ';
            out+=Num(Trim(e->GetText()),"1",false);
        }
        return out;
    }

    std::string Symbol(const Node* style,const Node* layer,Context& c)
    {
        if(!style)
            return "";
        auto type=A(style,"type"),name=A(style,"name");
        if(type=="POLYGON")
        {
            std::string fill;
            auto mode=T(style,"FillType","0");
            auto icon=T(style,"PatternName");
            if(icon.empty())
                icon=T(style,"IconName");
            if(mode=="0")
                fill="<Fill>"+Css("fill",Color(T(style,"FillColor")))+"</Fill>";
            else if(!icon.empty())
            {
                auto g=c.Graphic(icon);
                if(!g.empty())
                    fill="<Fill><GraphicFill>"+g+"</GraphicFill></Fill>";
                else 
                    c.Warn("OMITTED polygon fill: "+name);
            }
            else if(mode!="1")
            {
                c.Warn("APPROXIMATED polygon fill type "+mode+": "+name);
                fill="<Fill>"+Css("fill",Color(T(style,"FillColor")))+"</Fill>";
            }
            auto width=Num(T(style,"LineWidth","0"));
            std::string stroke;
            if(std::stod(width)>0)
                stroke=Stroke(T(style,"LineColor"),width,T(style,"LineDashArray"));
            if(T(style,"LineType","0")!="0")
                c.Warn("APPROXIMATED polygon outline type: "+name);
            if(mode=="1")
                c.Warn("REVIEW polygon FillType=1 treated as no fill: "+name);

            return "<PolygonSymbolizer>"+fill+stroke+"</PolygonSymbolizer>";
        }
        if(type=="LINE")
        {
            auto code=T(style,"Type","0"),dash=Dash(style,c),icon=T(style,"IconName");
            std::string graphic;
            if(!icon.empty())
                graphic=c.Graphic(icon);
            if(code!="0")
                c.Warn("REVIEW line type "+code+": "+name+"; GSS dash/icon used when available; spacing/offset/compound strokes may differ");
            return "<LineSymbolizer>"+Stroke(T(style,"Color"),T(style,"Width","1"),dash,graphic)+"</LineSymbolizer>";
        }
        if(type=="POINT")
        {
            auto icon=T(style,"IconName");
            std::string graphic;
            if(!icon.empty())
            {
                auto iconSize=T(style,"Size","8");
                if(std::atof(iconSize.c_str())<=4)
                    iconSize.clear();
                graphic=c.Graphic(icon,iconSize);

                if(graphic.empty())
                    return "";
            }
            else
            {
                auto code=T(style,"Type","0");
                std::string mark=code=="1"?"square":"circle";
                if(code!="0"&&code!="1")
                    c.Warn("APPROXIMATED point type "+code+" as circle: "+name);
                graphic="<Graphic><Mark><WellKnownName>"+mark+"</WellKnownName><Fill>"+
                    Css("fill",Color(T(style,"Color")))+"</Fill></Mark><Size>"+Num(T(style,"Size","8"))+"</Size></Graphic>";}
            if(T(style,"OutSize","0")!="0"||T(style,"TripleSize","0")!="0")
                c.Warn("REVIEW point outline/triple-color fields: "+name);
            if(T(style,"xmove","0")!="0"||T(style,"ymove","0")!="0"||!T(style,"AngleColumn").empty())
                c.Warn("OMITTED point offset/rotation: "+name);

            return "<PointSymbolizer>"+graphic+"</PointSymbolizer>";
        }
        if(type=="LABEL"){
            c.Warn("REVIEW label background, size units and placement against original engine: "+name);
            auto field=T(layer,"LabelColumnName");
            if(field.empty())
            {
                c.Warn("OMITTED label without LabelColumnName: "+name);
                return "";
            }
            auto offset=[](const std::string& s)
            {
                size_t p=0;
                double d=std::stod(s,&p);
                if(p!=s.size()||!std::isfinite(d))
                throw 
                    std::runtime_error("Invalid label offset");
                return s;
            };
            if(T(style,"Type","0")!="0"||T(style,"Align","4")!="4"||Yes(T(style,"UseSuffix"))||Yes(T(style,"UseIcon")))
                c.Warn("REVIEW label alignment/type/suffix/icon: "+name);
            return "<TextSymbolizer><Label><ogc:PropertyName>"+X(field)+"</ogc:PropertyName></Label><Font>"+
                Css("font-family",T(style,"Font","Arial"))+Css("font-size",Num(T(style,"Size","10")))+
                "</Font><LabelPlacement><PointPlacement><AnchorPoint><AnchorPointX>0.5</AnchorPointX><AnchorPointY>0.5</AnchorPointY></AnchorPoint><Displacement><DisplacementX>"+offset(T(style,"xmove","0"))+"</DisplacementX><DisplacementY>"+offset(T(style,"ymove","0"))+"</DisplacementY></Displacement></PointPlacement></LabelPlacement><Fill>"+Css("fill",Color(T(style,"Color")))+"</Fill></TextSymbolizer>";
        }
        c.Warn("UNSUPPORTED style type: "+type+" / "+name);

        return "";
    }
    }

    std::string ReadCopXml(const fs::path& path)
    {
        return ReadXml(path);
    }

    CopProfile LoadCop(const fs::path& path)
    {
        CopProfile p;
        p.file=path;
        p.xml=ReadXml(path);
        auto doc=Parse(p.xml);
        auto root=doc->RootElement();
        if(!root||std::string(root->Name())!="InSIDEMapX3")
        throw 
            std::runtime_error("Expected an InSIDEMapX3 COP XML file");
        auto layers=root->FirstChildElement("Layers");
        if(!layers)
        throw 
            std::runtime_error("XML has no Layers section");
        for(auto e=layers->FirstChildElement("Layer"); e; e=e->NextSiblingElement("Layer"))
        {
            CopLayer l;
            l.alias=A(e,"alias");l.category=T(e,"Category");
            l.source=T(e,"Source");
            l.target=l.source;
            l.display=T(e,"DisplayType");
            l.style=Yes(T(e,"UseStyleTable"))?T(e,"StyleTableName"):T(e,"GeometryStyleName");
            l.scale=Yes(T(e,"UseScale"))?T(e,"MinScale")+" ~ "+T(e,"MaxScale"):"all";
            l.visible=Yes(T(e,"visual",T(e,"Visible","TRUE")));
            l.enabled=l.visible;
            p.layers.push_back(l);
        }
        return p;
    }

    void MatchCop(CopProfile& profile,const std::vector<Layer>& data) 
    {
        for(auto& layer:profile.layers)
        {
            if(layer.manualTarget)
                continue;
            std::vector<std::string> matches;
            auto source=layer.source;std::replace(source.begin(),source.end(),'\\','/');
            source=Lower(fs::u8path(source).filename().u8string());
            if(source.size()>4&&source.substr(source.size()-4)==".shp")
                source.resize(source.size()-4);
            auto canonical=[](std::string name){
                name=Lower(name);
                if(name.size()>4&&name.substr(name.size()-4)=="_tmk")name.resize(name.size()-4);
                return name;
            };
            source=canonical(source);
            for(const auto& d:data)
                if(d.type!="RASTER"&&canonical(d.local.stem().u8string())==source)
                    matches.push_back(d.name);
            layer.target=matches.size()==1?matches.front():layer.source;
        }
    }

    CopSelection SelectCop(const std::vector<CopProfile>& profiles,const std::vector<Layer>& data) 
    {
        CopSelection result;result.profiles=profiles;
        for(auto& profile:result.profiles) 
        {
            MatchCop(profile,data);
            for(auto& layer:profile.layers)
                if(layer.enabled) 
                {
                    auto found=std::find_if(data.begin(),data.end(),[&](const Layer& d)
                        {
                            return d.name==layer.target&&d.type!="RASTER";
                        });

                    if(found==data.end()) 
                    {
                        layer.enabled=false;
                        result.unmatched.push_back(profile.file.filename().u8string()+" | "+layer.source+" -> "+layer.target);
                    }
                    else 
                    {
                        ++result.matched;
                        if(!found->rgb.empty())
                            layer.rgb=NormalizeRgb(found->rgb);
                    }
            }
        }
        return result;
    }

    std::vector<Artifact> BuildCop(const std::vector<CopProfile>& profiles,const CopOptions& options)
    {
        if(profiles.empty())
            throw std::runtime_error("Add a COP XML file first");
        if(!options.symbolBaseUrl.empty()&&!std::regex_match(options.symbolBaseUrl,std::regex("https?://[^?# \\t\\r\\n]+")))
            throw std::runtime_error("Symbol base URL must be an http(s) URL without query or spaces");
        if(options.transparency<0||options.transparency>2)
            throw std::runtime_error("Invalid transparency mode");

        Context c;c.options=options;
        if(!options.gssRoot.empty())
        {
            if(!fs::is_directory(options.gssRoot))
                throw std::runtime_error("GSS folder does not exist");

            for(const auto& e:fs::recursive_directory_iterator(options.gssRoot))
                if(e.is_regular_file()&&(Lower(e.path().extension().u8string())==".bmp"||Lower(e.path().extension().u8string())==".png"))
                {
                    auto key=Lower(e.path().stem().u8string());
                    if(!c.symbols.emplace(key,e.path()).second)
                    {
                        c.Warn("DUPLICATE symbol basename: "+key);
                        c.symbols.emplace(e.path().generic_u8string(),e.path());
                    }
            }
            auto semantic=options.gssRoot/"GSSXML"/"GSS_STYLE_v1.1.xml";
            if(fs::exists(semantic))
            {
                c.gssDoc=Parse(ReadXml(semantic));
                auto root=c.gssDoc->RootElement();
                for(auto n=root->FirstChildElement("Style");
                    n; n=n->NextSiblingElement("Style"))
                    c.gss[A(n,"name")]=n;
            }
        }
        // Build the complete common image library, including symbols unused at this scale.
        if(options.symbolBaseUrl.empty()&&!options.sharedSymbolRoot.empty())
            for(const auto& entry:c.symbols)c.Graphic(entry.first);
        std::vector<Artifact> out;std::ostringstream report;
        if(options.symbolBaseUrl.empty()&&!options.sharedSymbolRoot.empty())
            report<<"SHARED SYMBOL DIRECTORY: "<<fs::absolute(options.sharedSymbolRoot).generic_u8string()<<"\n";
        size_t totalRules=0;
        std::set<std::string> usedFiles;
        report<<"COP XML to SLD conversion\nScale bounds and style numbers are taken from XML; validate visual output on the target server.\nOnly checked layers are exported. Duplicate Source values are grouped into one NamedLayer with multiple rules.\nOriginal provider/network settings are not used.\nTransparency mode="<<options.transparency<<" (0 preserve, 1 magenta, 2 white)\n\n";
        for(const auto& profile:profiles)
        {
            auto doc=Parse(profile.xml);
            auto root=doc->RootElement();
            if(auto notes=root->FirstChildElement("StandardWarnings"))
                for(auto item=notes->FirstChildElement("Item");item;item=item->NextSiblingElement("Item"))
                    c.Warn("STANDARD: "+T(item,"Text"));

            std::map<std::string,const Node*> styles,tables;
            for(auto n=root->FirstChildElement("style"); n; n=n->NextSiblingElement("style"))
            {
                if(!styles.emplace(A(n,"name"),n).second)
                    c.Warn("DUPLICATE style; first used: "+A(n,"name"));
            }
            for(const auto& entry:styles)
                for(const auto* key: {"IconName","IconNameAdd","IconNameEx","PatternName"})
                {
                    auto ref=T(entry.second,key);
                    if(!ref.empty()&&!fs::is_regular_file(fs::u8path(ref))&&c.symbols.find(Lower(fs::u8path(ref).stem().u8string()))==c.symbols.end())
                        c.Warn("MISSING SYMBOL REFERENCE (may be unused): "+ref);
            }
            for(auto n=root->FirstChildElement("StyleTable");n;n=n->NextSiblingElement("StyleTable"))
                tables[A(n,"name")]=n;
            std::vector<std::string> order;
            std::map<std::string,std::string> groups;
            size_t idx=0;
            size_t ruleCount=0;
            for(auto layer=root->FirstChildElement("Layers")->FirstChildElement("Layer");layer;layer=layer->NextSiblingElement("Layer"),++idx)
            {
                if(idx>=profile.layers.size())
                    throw std::runtime_error("Profile layer state mismatch");

                const auto& chosen=profile.layers[idx];
                if(!chosen.enabled)
                    continue;
                if(chosen.target.empty())
                    throw 
                    std::runtime_error("Enter a target layer name for "+chosen.alias);
                std::string scale;
                try
                {
                    if(Yes(T(layer,"UseScale")))
                    {
                        auto low=Num(T(layer,"MinScale"),"0"),high=Num(T(layer,"MaxScale"),"0");
                        if(std::stod(high)>0&&std::stod(low)>=std::stod(high))
                        throw
                            std::runtime_error("Invalid scale interval");
                        if(std::stod(low)>0)
                            scale+="<MinScaleDenominator>"+low+"</MinScaleDenominator>";
                        if(std::stod(high)>0)
                            scale+="<MaxScaleDenominator>"+high+"</MaxScaleDenominator>";
                    }
                }
                catch(const std::exception& e)
                {
                    c.Warn("SKIPPED layer "+chosen.alias+": "+e.what());
                    continue;
                }
                auto resolve=[&](const std::string& name)
                {
                    if(name.empty())
                        return std::string{};
                    auto it=styles.find(name);
                    if(it==styles.end())
                    {
                        c.Warn("MISSING style: "+name);
                        return std::string{};
                    }
                    try
                    {
                        if(!chosen.rgb.empty()&&A(it->second,"type")!="LABEL") {
                            tinyxml2::XMLDocument copy;
                            auto styled=it->second->DeepClone(&copy)->ToElement();copy.InsertEndChild(styled);
                            auto key=A(styled,"type")=="POLYGON"?"FillColor":"Color";
                            auto color=styled->FirstChildElement(key);
                            if(!color)
                            {
                                color=copy.NewElement(key);
                                styled->InsertEndChild(color);
                            }
                            color->SetText(RgbHex(chosen.rgb).substr(1).c_str());
                            return Symbol(styled,layer,c);
                        }
                        return Symbol(it->second,layer,c);
                    }
                    catch(const std::exception& e)
                    {
                        c.Warn("SKIPPED style "+name+": "+e.what());
                        return std::string{};
                    }
                };
                auto display=Lower(chosen.display);
                bool geometry=display=="geometry"||display=="both",label=display=="label"||display=="both";
                if(!geometry&&!label)
                {
                    c.Warn("UNSUPPORTED DisplayType: "+chosen.display);
                    continue;
                }
                auto add=[&](const std::string& filter,const std::string& symbols)
                {
                    if(symbols.empty())
                        return;
                    if(groups.find(chosen.target)==groups.end())
                        order.push_back(chosen.target);
                    groups[chosen.target]+="<Rule><Name>rule_"+std::to_string(++ruleCount)+"</Name><Title>"+X(chosen.alias)+"</Title>"+filter+scale+symbols+"</Rule>\n";
                };
                if(Yes(T(layer,"UseStyleTable")))
                {
                    auto table=tables.find(T(layer,"StyleTableName"));
                    if(table==tables.end())
                    {
                        c.Warn("MISSING style table: "+T(layer,"StyleTableName"));
                        continue;
                    }
                    bool standard=A(table->second,"ConditionMode")=="STANDARD";
                    auto field=T(layer,"StyleColumnName");
                    if(field.empty()&&!standard)
                    {
                        c.Warn("SKIPPED table without StyleColumnName: "+chosen.alias);
                        continue;
                    }
                    std::set<std::string> duplicates;
                    for(auto e=table->second->FirstChildElement("Element"); e; e=e->NextSiblingElement("Element"))
                    {
                        if(Yes(A(e,"disabled"))){c.Warn("DISABLED standard rule: "+chosen.alias+" / "+A(e,"name"));continue;}
                        std::string body;
                        if(geometry)
                            for(auto key:{"polygon","line","point"})
                                body+=resolve(A(e,key));
                        if(label)
                            body+=resolve(A(e,"label"));
                        if(standard)
                        {
                            try
                            {
                                add(StandardFilter(T(e,"Condition")),body);
                            }
                            catch(const std::exception& ex)
                            {
                                c.Warn("SKIPPED invalid condition: "+chosen.alias+": "+ex.what());
                            }
                            continue;
                        }
                        auto value=A(e,"value");
                        if(!duplicates.insert(value+"\n"+body).second)
                            continue;
                        auto filter="<ogc:Filter><ogc:PropertyIsEqualTo><ogc:PropertyName>"+X(field)+"</ogc:PropertyName><ogc:Literal>"+X(value)+"</ogc:Literal></ogc:PropertyIsEqualTo></ogc:Filter>";add(filter,body);
                    }
                }else
                {
                    std::string body;
                    if(geometry)
                        body+=resolve(T(layer,"GeometryStyleName"));
                    if(label)
                        body+=resolve(T(layer,"LabelStyleName"));
                    add("",body);
                }
                report<<profile.file.filename().u8string()<<" | "<<chosen.source<<" -> "<<chosen.target<<" | "<<chosen.alias<<"\n";
            }
            if(groups.empty())
            {
                c.Warn("NO OUTPUT RULES: "+profile.file.filename().u8string());
                continue;
            }
            std::string s="<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<StyledLayerDescriptor version=\"1.0.0\" xmlns=\"http://www.opengis.net/sld\" xmlns:ogc=\"http://www.opengis.net/ogc\" xmlns:xlink=\"http://www.w3.org/1999/xlink\">\n";
            for(const auto& name:order)
                s+="<NamedLayer><Name>"+X(name)+"</Name><UserStyle><Title>"+ 
                X(name)+"</Title><FeatureTypeStyle>\n"+groups[name]+"</FeatureTypeStyle></UserStyle></NamedLayer>\n";
            s+="</StyledLayerDescriptor>\n";

            auto stem=profile.file.stem().u8string();
            auto file=stem+".sld";
            int n=2;
            while(!usedFiles.insert(Lower(file)).second)
                file=stem+"_"+std::to_string(n++)+".sld";
            out.push_back({fs::u8path(file),PrettyXml(s)});
            report<<"OUTPUT "<<file<<": "<<groups.size()<<" named layers, "<<ruleCount<<" rules\n";
            totalRules+=ruleCount;
        }
        if(out.empty())
        throw
            std::runtime_error("No convertible checked layers. Check styles, source XML and layer selection.");

        report<<"\nTOTAL RULES: "<<totalRules<<"\nSYMBOL FILES: "<<c.generated.size()<<"\n\nREVIEW ITEMS ("<<c.warnings.size()<<")\n";
        for(const auto& w:c.warnings)
            report<<w<<'\n';

        report<<"\nSYMBOL MAPPING\n";
        for(const auto& pair:c.generated)
            report<<pair.first<<" -> "<<pair.second<<'\n';

        out.insert(out.end(),c.assets.begin(),c.assets.end());
        out.push_back({"sld-conversion-report.txt",report.str()});

        return out;
    }
}
