#pragma once
#include "CopStyle.h"
#include "XmlOutput.h"
#include "StandardRules.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <set>
#include <sstream>
namespace mm 
{
using StudioNode=tinyxml2::XMLElement;
inline std::string SA(const StudioNode* n,const char* key)
{
    auto p=n->Attribute(key);
    return p?p:"";
}
inline std::string ST(const StudioNode* n,const char* key)
{
    auto e=n->FirstChildElement(key);
    return e&&e->GetText()?e->GetText():"";
}
inline void SetST(StudioNode* n,const std::string& key,const std::string& value)
{
    auto e=n->FirstChildElement(key.c_str());
    if(!e)
    {
        e=n->GetDocument()->NewElement(key.c_str());
        n->InsertEndChild(e);
    }
    e->SetText(value.c_str());
}
inline bool STrue(std::string s)
{
    for(auto& c:s)
        if(c>='a'&&c<='z')
            c-=32;
    return s=="TRUE"||s=="1";
}
struct StudioProperty 
{
    std::string key,label,value;
};
class StudioModel 
{
    tinyxml2::XMLDocument doc_;
    CopProfile profile_;
    void Walk(StudioNode* n,const std::function<void(StudioNode*)>& f)
    {
        f(n);
        for(auto c=n->FirstChildElement();c;c=c->NextSiblingElement())
            Walk(c,f);
    }
public:
    void Load(const CopProfile& p)
    {
        if(doc_.Parse(p.xml.c_str())!=tinyxml2::XML_SUCCESS||!doc_.RootElement()||std::string(doc_.RootElement()->Name())!="InSIDEMapX3"||!doc_.RootElement()->FirstChildElement("Layers"))
            throw std::runtime_error("Invalid style project XML");
        profile_=p;Sync();
    }
    std::vector<StudioNode*> Items(int tab)
    {
        std::vector<StudioNode*> out;
        auto root=doc_.RootElement();
        if(!root)
            return out;
        if(tab==0)
        {
            for(auto n=root->FirstChildElement("Layers")->FirstChildElement("Layer");n;n=n->NextSiblingElement("Layer"))
                out.push_back(n);
        }
        else if(tab==1)
        {
            for(auto n=root->FirstChildElement("StyleTable");n;n=n->NextSiblingElement("StyleTable"))
            {
                out.push_back(n);
                for(auto e=n->FirstChildElement("Element");e;e=e->NextSiblingElement("Element"))
                    out.push_back(e);
            }
        }
        else 
        {
            const char* types[]={"POINT","LINE","POLYGON","LABEL"};
            for(auto n=root->FirstChildElement("style");n;n=n->NextSiblingElement("style"))
                if(SA(n,"type")==types[tab-2])
                    out.push_back(n);
        }
        return out;
    }
    int LayerIndex(StudioNode* node)
    {
        auto layers=Items(0);
        auto it=std::find(layers.begin(),layers.end(),node);
        return it==layers.end()?-1:static_cast<int>(it-layers.begin());
    }
    std::string Title(StudioNode* n)
    {
        std::string tag=n->Name();if(tag=="Layer")
            return SA(n,"alias")+" | "+ST(n,"Source");if(tag=="Element")return "    ["+SA(n,"value")+"] "+SA(n,"point")+SA(n,"line")+SA(n,"polygon")+SA(n,"label");
        return SA(n,"name");
    }
    void Sync()
    {
        auto nodes=Items(0);
        profile_.layers.resize(nodes.size());
        for(size_t i=0;i<nodes.size();++i)
        {
            auto n=nodes[i];
            auto& l=profile_.layers[i];
            l.alias=SA(n,"alias");
            l.source=ST(n,"Source");
            if(l.target.empty())
                l.target=l.source;
            l.display=ST(n,"DisplayType");
            l.style=ST(n,STrue(ST(n,"UseStyleTable"))?"StyleTableName":"GeometryStyleName");
            l.visible=STrue(ST(n,"Visible"));
            l.scale=STrue(ST(n,"UseScale"))?ST(n,"MinScale")+" ~ "+ST(n,"MaxScale"):"all";
        }
    }
    CopProfile Snapshot()
    {
        Sync();
        tinyxml2::XMLPrinter p;
        doc_.Print(&p);
        profile_.xml=PrettyXml(p.CStr());
        return profile_;
    }
    std::vector<StudioProperty> Properties(StudioNode* n)
    {
        std::vector<StudioProperty> out;
        std::set<std::string> keys;
        auto add=[&](std::string key,std::string label)
        {
            if(!keys.insert(key).second)
                return;
            std::string value=key[0]=='@'?SA(n,key.substr(1).c_str()):ST(n,key.c_str());
            out.push_back({key,label,value});
        };
        std::string tag=n->Name(),type=SA(n,"type");
        if(tag=="Layer")
        {
            add("@alias","레이어 이름");
            int i=LayerIndex(n);
            out.push_back({"$target","연결할 데이터 / MAP 레이어",profile_.layers[i].target});
            out.push_back({"$rgb","레이어 RGB 덮어쓰기 (빈 값: 스타일 색상)",profile_.layers[i].rgb});
            out.push_back({"$enabled","출력 포함 (TRUE/FALSE)",profile_.layers[i].enabled?"TRUE":"FALSE"});
            for(auto pair:std::vector<std::pair<std::string,std::string>>{
                {"Source","원본 데이터 이름"},{"Visible","원본 표시 여부"},
                {"DisplayType","표시: GEOMETRY / LABEL / BOTH"},
                {"UseStyleTable","속성별 스타일 사용"},
                {"GeometryStyleName","기본 점/선/면 스타일"},
                {"StyleTableName","스타일 테이블"},
                {"StyleColumnName","분류 속성 필드"},
                {"LabelStyleName","라벨 스타일"},
                {"LabelColumnName","라벨 속성 필드"},
                {"UseScale","축척 범위 사용"},
                {"MinScale","최소 축척"},
                {"MaxScale","최대 축척"},
                {"ProviderName","원본 Provider 이름"},
                {"ResourcePath","심볼 경로"}})
                add(pair.first,pair.second);
        }
        else if(tag=="StyleTable") 
        {
            add("@name","테이블 이름");
            add("@type","속성 형식: CHARACTER / INTEGER / FLOATING");
            add("@Column","속성 필드");
            add("@FeatureList","지오메트리 유형");
        }
        else if(tag=="Element")
        {
            add("Condition","표준 조건: FIELD:값; FIELD:범위 (대안은 ||)");
            for(auto key:{"value","point","line","polygon","label","labelcolumn","disabled"})
                add(std::string("@")+key,key);
        }
        else 
        {
            add("@name","스타일 이름");
            if(type=="POINT")
            {
                for(auto key:{"Type","Size","Color","IconName","OutSize","OutColor","xmove","ymove","AngleColumn"})
                    add(key,key);
            }
            if(type=="LINE")
            {
                for(auto key:{"Type","Width","DashArray","Color","ColorEx","IconName","IconInterval","IconStartPos","VerticalInterval",
                    "VerticalWidth","VerticalLength","WidthEx"})
                    add(key,key);
            }
            if(type=="POLYGON")
            {
                for(auto key:{"FillType","FillColor","HatchColor","PatternName","LineType","LineWidth","LineColor","LineDashArray"})
                    add(key,key);
            }
            if(type=="LABEL")
            {
                for(auto key:{"Font","Size","Type","Color","BackColor","Align","xmove","ymove","Precision","Anotation",
                    "UseSuffix","Suffix","UseIcon","IconName"})
                    add(key,key);
            }
        }
        for(auto a=n->FirstAttribute();a;a=a->Next())
            if(std::string(a->Name())!="type"||tag=="StyleTable")
                add(std::string("@")+a->Name(),a->Name());
        for(auto e=n->FirstChildElement();e;e=e->NextSiblingElement())
            if(!e->FirstChildElement()&&std::string(e->Name())!="StandardSource")
                add(e->Name(),e->Name());
        for(auto& p:out)
        {
            static const std::map<std::string,std::string> labels={{"Type","종류 코드 (원본 XML)"},{"Size","크기"},{"Width","선 두께"},
                {"Color","색상 (RRGGBB)"},{"ColorEx","보조 색상"},{"FillType","채움 코드 (0: 단색 / 1: 없음)"},
                {"FillColor","채움 색상"},{"HatchColor","해치 색상"},{"PatternName","패턴 이미지"},
                {"LineType","외곽선 종류"},{"LineWidth","외곽선 두께"},{"LineColor","외곽선 색상"},
                {"Font","글꼴"},{"IconName","아이콘 이미지"},{"xmove","X 오프셋"},{"ymove","Y 오프셋"},{"BackColor","배경 색상"}};
            auto it=labels.find(p.key);
            if(it!=labels.end())
                p.label=it->second;
        }return out;
    }
    void Set(StudioNode* n,const std::string& key,std::string value)
    {
        if(key.empty()) return;
        if(key=="Condition")StandardFilter(value);
        if(key.find("Color")!=std::string::npos&&!value.empty())
        {
            if(value[0]=='#')
                value.erase(0,1);
            if(value.size()!=6||value.find_first_not_of("0123456789abcdefABCDEF")!=value.npos)
                throw std::runtime_error("색상은 RRGGBB 형식의 6자리 16진수로 입력하세요.");
        }
        if(key=="Visible"||key=="UseScale"||key=="UseStyleTable"||key=="$enabled")
        {
            if(value!="TRUE"&&value!="FALSE"&&value!="1"&&value!="0")
                throw std::runtime_error("TRUE 또는 FALSE를 선택하세요.");
        }
        if(key=="DisplayType"&&value!="GEOMETRY"&&value!="LABEL"&&value!="BOTH")
            throw std::runtime_error("GEOMETRY, LABEL, BOTH 중 선택하세요.");
        if(!value.empty()&&(key=="Width"||key=="Size"||key=="LineWidth"||key=="MinScale"||key=="MaxScale"||key=="xmove"||key=="ymove"))
        {
            size_t end=0;double v=std::stod(value,&end);
            if(end!=value.size()||!std::isfinite(v)||((key!="xmove"&&key!="ymove")&&v<0))
                throw std::runtime_error("유효한 숫자를 입력하세요.");
        }
        if(key=="DashArray"&&!value.empty())
        {
            std::istringstream in(value);std::string token;int count=0;
            while(in>>token)
            {
                size_t end=0;
                double v=std::stod(token,&end);
                if(end!=token.size()||!std::isfinite(v)||v<=0)
                    throw std::runtime_error("대시 길이는 양수 목록으로 입력하세요. 예: 8 4");
                ++count;
            }
            if(count<2||count%2)
                throw std::runtime_error("대시는 선/공백 쌍으로 입력하세요. 예: 8 4");
        }
        int index=LayerIndex(n);
        if(key=="$target")
        {
            if(value.empty())
                throw std::runtime_error("데이터 레이어를 선택하세요.");
            profile_.layers[index].target=value;
            profile_.layers[index].manualTarget=true;
            return;
        }
        if(key=="$rgb")
        {
            profile_.layers[index].rgb=value.empty()?value:NormalizeRgb(value);
            return;
        }
        if(key=="$enabled")
        {
            profile_.layers[index].enabled=STrue(value);
            return;
        }
        if(key=="@name")
        {
            if(value.empty())
                throw std::runtime_error("이름이 필요합니다.");
            auto old=SA(n,"name");
            auto tag=std::string(n->Name());
            for(auto e=doc_.RootElement()->FirstChildElement(n->Name());e;e=e->NextSiblingElement(n->Name()))
                if(e!=n&&SA(e,"name")==value)
                    throw std::runtime_error("이미 사용 중인 이름입니다.");
            if(!old.empty()&&old!=value)
                Walk(doc_.RootElement(),[&](StudioNode* e)
                    {
                        std::string t=e->Name();
                        if((tag=="style"&&(t=="GeometryStyleName"||t=="LabelStyleName"))||(tag=="StyleTable"&&t=="StyleTableName"))
                        {
                            if(e->GetText()&&old==e->GetText())
                                e->SetText(value.c_str());
                        }if(tag=="style"&&t=="Element")
                            for(auto k:{"point","line","polygon","label"})
                                if(SA(e,k)==old)
                                    e->SetAttribute(k,value.c_str());
                    });
        }
        if(key=="@value")
        {
            for(auto e=n->Parent()->FirstChildElement("Element");e;e=e->NextSiblingElement("Element"))
                if(e!=n&&SA(e,"value")==value)
                    throw std::runtime_error("같은 속성값의 규칙이 이미 있습니다.");
        }
        if(key[0]=='@')
            n->SetAttribute(key.substr(1).c_str(),value.c_str());
        else 
            SetST(n,key,value);
        if(key=="Visible"&&index>=0)profile_.layers[index].enabled=STrue(value);
        if(key=="Source"&&index>=0&&!profile_.layers[index].manualTarget)
            profile_.layers[index].target=value;
        Sync();
    }
    StudioNode* Add(int tab,StudioNode* selected=nullptr,bool copy=false)
    {
        auto root=doc_.RootElement();
        StudioNode* n=nullptr;std::string tag;
        if(copy&&selected)
        {
            n=selected->DeepClone(&doc_)->ToElement();
            selected->Parent()->InsertAfterChild(selected,n);
            tag=n->Name();
        }
        else if(tab==0)
        {
            n=doc_.NewElement("Layer");
            root->FirstChildElement("Layers")->InsertEndChild(n);
            tag="Layer";n->SetAttribute("alias","새 레이어");
            for(auto p:std::vector<std::pair<std::string,std::string>>{{"Source","new_layer"},{"Visible","TRUE"},
                {"DisplayType","GEOMETRY"},{"UseStyleTable","FALSE"},{"UseScale","FALSE"}})
                SetST(n,p.first,p.second);
        }
        else if(tab==1)
        {
            n=doc_.NewElement("StyleTable");
            root->InsertEndChild(n);
            tag="StyleTable";
            n->SetAttribute("type","CHARACTER");
        }
        else 
        {
            n=doc_.NewElement("style");
            root->InsertEndChild(n);
            tag="style";
            const char* types[]={"POINT","LINE","POLYGON","LABEL"};
            n->SetAttribute("type",types[tab-2]);
            for(auto p:std::vector<std::pair<std::string,std::string>>{{"Type","0"},{"Size","10"},{"Color","286EBE"}})
                SetST(n,p.first,p.second);
            if(tab==3)
                SetST(n,"Width","2");
            if(tab==4)
            {
                SetST(n,"FillType","0");
                SetST(n,"FillColor","BBDDEE");
                SetST(n,"LineWidth","1");
                SetST(n,"LineColor","404040");
            }
            if(tab==5)
                SetST(n,"Font","Arial");
        }
        if(tag=="Layer")
        {
            auto nodes=Items(0);
            auto it=std::find(nodes.begin(),nodes.end(),n);
            size_t i=it-nodes.begin();
            CopLayer state;
            if(copy&&i>0)
                state=profile_.layers[i-1];
            else 
                state.enabled=true;
            profile_.layers.insert(profile_.layers.begin()+i,state);
        }
        else if(tag=="style"||tag=="StyleTable")
        {
            auto base=copy?SA(n,"name")+"_copy":tag+"_new";
            auto name=base;
            int count=2;
            auto exists=[&]
            {
                for(auto e=root->FirstChildElement(tag.c_str());e;e=e->NextSiblingElement(tag.c_str()))
                    if(e!=n&&SA(e,"name")==name)
                        return true;
                return false;
            };
            while(exists())
                name=base+"_"+std::to_string(count++);
            n->SetAttribute("name",name.c_str());
        }
        else if(tag=="Element") 
        {
            auto value=SA(n,"value")+"_copy";
            while(true)
            {
                bool exists=false;
                for(auto e=n->Parent()->FirstChildElement("Element");e;e=e->NextSiblingElement("Element"))
                    if(e!=n&&SA(e,"value")==value)
                        exists=true;
                if(!exists)
                    break;
                value+="_copy";
            }
            n->SetAttribute("value",value.c_str());
        }
        Sync();return n;
    }
    StudioNode* AddRule(StudioNode* n)
    {
        if(std::string(n->Name())=="Element")
            n=n->Parent()->ToElement();
        if(std::string(n->Name())!="StyleTable")
            throw std::runtime_error("스타일 테이블을 선택하세요.");
        auto e=doc_.NewElement("Element");
        int value=1;
        while(true)
        {
            bool exists=false;
            for(auto c=n->FirstChildElement("Element");c;c=c->NextSiblingElement("Element"))
                if(SA(c,"value")==std::to_string(value))
                    exists=true;
            if(!exists)
                break;
            ++value;
        }
        e->SetAttribute("value",std::to_string(value).c_str());
        n->InsertEndChild(e);
        return e;
    }
    void Remove(StudioNode* n)
    {
        int i=LayerIndex(n);
        if(i>=0)
            profile_.layers.erase(profile_.layers.begin()+i);
        n->Parent()->DeleteChild(n);
        Sync();
    }
    StudioNode* Move(StudioNode* n,int direction)
    {
        int i=LayerIndex(n);
        auto nodes=Items(0);
        if(i<0||i+direction<0||i+direction>=static_cast<int>(nodes.size()))
            return n;
        auto parent=n->Parent();
        auto clone=n->DeepClone(&doc_)->ToElement();
        if(direction<0)
        {
            auto previous=nodes[i-1]->PreviousSibling();
            if(previous)
                parent->InsertAfterChild(previous,clone);
            else 
                parent->InsertFirstChild(clone);
        }
        else 
            parent->InsertAfterChild(nodes[i+1],clone);
        parent->DeleteChild(n);
        std::swap(profile_.layers[i],profile_.layers[i+direction]);
        Sync();
        return clone;
    }
    static CopProfile New(const std::vector<Layer>& data)
    {
        CopProfile p;
        p.file="new_styles.xml";
        p.xml="<InSIDEMapX3><Layers/></InSIDEMapX3>";
        StudioModel m;
        m.Load(p);
        for(const auto& d:data)
        {
            if(d.type=="RASTER")
                continue;
            auto n=m.Add(0);
            m.Set(n,"@alias",d.relative.stem().u8string());
            m.Set(n,"Source",d.relative.stem().u8string());
            m.Set(n,"$target",d.name);
            int tab=d.type=="POINT"?2:d.type=="LINE"?3:4;
            auto style=m.Add(tab);
            m.Set(style,"@name",d.name+"_style");
            m.Set(n,"GeometryStyleName",d.name+"_style");
            if(!d.rgb.empty())
                m.Set(style,tab==4?"FillColor":"Color",RgbHex(d.rgb).substr(1));
        }
        return m.Snapshot();
    }
};
// Merge COP-generated SLDs so scale profiles can share a data layer in the main MAP.
inline Artifact StudioSld(const std::vector<Artifact>& files)
{
    tinyxml2::XMLDocument out;
    auto root=out.NewElement("StyledLayerDescriptor");
    root->SetAttribute("version","1.0.0");
    root->SetAttribute("xmlns","http://www.opengis.net/sld");
    root->SetAttribute("xmlns:ogc","http://www.opengis.net/ogc");
    root->SetAttribute("xmlns:xlink","http://www.w3.org/1999/xlink");
    out.InsertEndChild(root);
    std::map<std::string,StudioNode*> styles;
    for(const auto& f:files)
        if(f.name.extension()==".sld")
        {
        tinyxml2::XMLDocument input;
        if(input.Parse(f.content.c_str())!=tinyxml2::XML_SUCCESS)
            throw std::runtime_error("Invalid generated SLD");
        for(auto n=input.RootElement()->FirstChildElement("NamedLayer");n;n=n->NextSiblingElement("NamedLayer"))
        {
            auto name=ST(n,"Name");
            auto found=styles.find(name);
            if(found==styles.end())
            {
                auto clone=n->DeepClone(&out)->ToElement();
                root->InsertEndChild(clone);
                styles[name]=clone->FirstChildElement("UserStyle");
            }
            else 
            {
                for(auto style=n->FirstChildElement("UserStyle");style;style=style->NextSiblingElement("UserStyle"))
                    for(auto fts=style->FirstChildElement("FeatureTypeStyle");fts;fts=fts->NextSiblingElement("FeatureTypeStyle"))
                        found->second->InsertEndChild(fts->DeepClone(&out));
            }
        }
    }
    if(styles.empty())
        throw std::runtime_error("No SLD layers to export");
    tinyxml2::XMLPrinter printer;
    out.Print(&printer);
    return {"studio-styles.sld",PrettyXml(printer.CStr())};
}
inline std::string StudioProject(const std::vector<CopProfile>& profiles,const CopOptions& options)
{
    tinyxml2::XMLDocument doc;
    auto root=doc.NewElement("MapMakeStyleProject");
    root->SetAttribute("version",1);
    doc.InsertEndChild(root);
    root->SetAttribute("gss",options.gssRoot.u8string().c_str());
    root->SetAttribute("symbolUrl",options.symbolBaseUrl.c_str());
    root->SetAttribute("transparency",options.transparency);
    for(const auto& p:profiles)
    {
        auto e=doc.NewElement("Profile");
        root->InsertEndChild(e);
        e->SetAttribute("file",p.file.u8string().c_str());
        auto xml=doc.NewElement("Xml");
        xml->SetText(p.xml.c_str());
        e->InsertEndChild(xml);
        for(const auto& l:p.layers)
        {
            auto st=doc.NewElement("LayerState");
            st->SetAttribute("target",l.target.c_str());
            st->SetAttribute("enabled",l.enabled);
            st->SetAttribute("manual",l.manualTarget);
            st->SetAttribute("rgb",l.rgb.c_str());
            e->InsertEndChild(st);
        }
    }
    tinyxml2::XMLPrinter printer;
    doc.Print(&printer);
    return PrettyXml(printer.CStr());
}
inline void ReadStudioProject(const std::string& xml,std::vector<CopProfile>& profiles,CopOptions& options)
{
    tinyxml2::XMLDocument doc;
    if(doc.Parse(xml.c_str())!=tinyxml2::XML_SUCCESS||!doc.RootElement()||
        std::string(doc.RootElement()->Name())!="MapMakeStyleProject"||doc.RootElement()->IntAttribute("version")!=1)
        throw std::runtime_error("지원하지 않는 스타일 프로젝트입니다.");
    auto root=doc.RootElement();
    std::vector<CopProfile> loaded;
    CopOptions o;
    o.gssRoot=fs::u8path(SA(root,"gss"));
    o.symbolBaseUrl=SA(root,"symbolUrl");
    o.transparency=root->IntAttribute("transparency");
    if(o.transparency<0||o.transparency>2)
        throw std::runtime_error("Invalid transparency");
    for(auto e=root->FirstChildElement("Profile");e;e=e->NextSiblingElement("Profile"))
    {
        CopProfile p;
        p.file=fs::u8path(SA(e,"file"));
        p.xml=ST(e,"Xml");
        StudioModel model;
        model.Load(p);
        p=model.Snapshot();
        size_t i=0;
        for(auto st=e->FirstChildElement("LayerState");st;st=st->NextSiblingElement("LayerState"))
        {
            if(i>=p.layers.size())
                throw std::runtime_error("Invalid layer state count");
            auto& l=p.layers[i++];
            l.target=SA(st,"target");
            l.enabled=st->BoolAttribute("enabled",true);
            l.manualTarget=st->BoolAttribute("manual");
            l.rgb=SA(st,"rgb");
        }
        if(i!=p.layers.size())
            throw std::runtime_error("Missing layer states");
        loaded.push_back(std::move(p));
    }
    profiles=std::move(loaded);
    options=std::move(o);
}
}
