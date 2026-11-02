#include "StandardXml.h"
#include "StandardRules.h"
#include "StyleStudioModel.h"
#include <set>
#include <iomanip>
namespace mm {
const std::vector<StandardScale>& StandardScales()
{
    static const std::vector<StandardScale> scales=
    {
         {"LAND","25K","육도 1:25,000","cop_land_2_5.xml","GSS_GROUND_LARGE_SCALE_LAYER.xml"},
         {"LAND","50K","육도 1:50,000","cop_land_5.xml","GSS_GROUND_LARGE_SCALE_LAYER.xml"},
         {"LAND","100K","육도 1:100,000","cop_land_10.xml","GSS_GROUND_LARGE_SCALE_LAYER.xml"},
         {"LAND","250K","육도 1:250,000","cop_land_25.xml","GSS_GROUND_SMALL_SCALE_LAYER.xml"},
         {"LAND","500K","육도 1:500,000","cop_land_50.xml","GSS_GROUND_SMALL_SCALE_LAYER.xml"},
         {"LAND","1M","육도 1:1,000,000","cop_land_100.xml","GSS_GROUND_SMALL_SCALE_LAYER.xml"},
         {"AIR","250K","공도 1:250,000 (합동작전)","cop_air_25.xml","GSS_AIR_JOGA_LAYER.xml"},
         {"AIR","500K","공도 1:500,000","cop_air_50.xml","GSS_AIR_NPC_LAYER.xml"},
         {"AIR","1M","공도 1:1,000,000","cop_air_100.xml","GSS_AIR_NPC_LAYER.xml"},
         {"AIR","2M","공도 1:2,000,000","cop_air_200.xml","GSS_AIR_NPC_LAYER.xml"},
         {"NAVY","KR1","해도 KR1","cop_kr_1.xml","GSS_NAVY_LAYER.xml"},
         {"NAVY","KR2","해도 KR2","cop_kr_2.xml","GSS_NAVY_LAYER.xml"},
         {"NAVY","KR3","해도 KR3","cop_kr_3.xml","GSS_NAVY_LAYER.xml"},
         {"NAVY","KR4","해도 KR4","cop_kr_4.xml","GSS_NAVY_LAYER.xml"},
         {"NAVY","KR5","해도 KR5","cop_kr_5.xml","GSS_NAVY_LAYER.xml"}
    };
    return scales;
}

namespace 
{
    using N=tinyxml2::XMLElement;

    N* Add(N* parent,const char* tag)
    {
        auto n=parent->GetDocument()->NewElement(tag);
        parent->InsertEndChild(n);
        return n;
    }

    std::string Upper(std::string s)
    {
        for(auto& c:s)
            if(c>='a'&&c<='z')
                c-=32;
        return s;
    }

    std::string Lower(std::string s)
    {
        for(auto& c:s)
            if(c>='A'&&c<='Z')
                c+=32;
        return s;
    }

    std::string Color(const std::string& text)
    {
        auto parts=RuleSplit(text,",");
        if(parts.size()!=4)
            throw std::runtime_error("Invalid ARGB: "+text);
    
        std::ostringstream out;out<<std::hex<<std::setfill('0');
        for(size_t i=0;i<4;++i)
        {
            size_t used;
            int v=std::stoi(parts[i],&used);
            if(used!=parts[i].size()||v<0||v>255)
                throw std::runtime_error("Invalid ARGB: "+text);
            if(i)
                out<<std::setw(2)<<v;
        }
        return out.str();
    }

    void Read(tinyxml2::XMLDocument& d,const fs::path& p,const char* root)
    {
        auto s=ReadCopXml(p);
        if(d.Parse(s.c_str())!=tinyxml2::XML_SUCCESS||!d.RootElement()||std::string(d.RootElement()->Name())!=root)
            throw std::runtime_error("Invalid standard XML: "+p.u8string());
    }

    std::string Dump(tinyxml2::XMLDocument& d)
    {
        tinyxml2::XMLPrinter p;
        d.Print(&p);
        return PrettyXml(p.CStr());
    }

    void ConvertStyle(const N* input,N* output,std::vector<std::string>& warnings)
    {
         auto name=SA(input,"name"),type=Upper(SA(input,"type"));
         output->SetAttribute("name",name.c_str());
         output->SetAttribute("type",type.c_str());
         auto warn=[&](const std::string& why)
         {
             warnings.push_back("STYLE "+name+": "+why);
         };

         auto set=[&](const char* key,const std::string& value)
         {
             SetST(output,key,value);
         };

         auto text=[&](const N* n,const char* key,const std::string& fallback)
         {
             auto v=n?ST(n,key):"";
             return v.empty()?fallback:v;
         };

         auto color=[&](const N* n,const char* key)
         {
             auto s=text(n,"Color","255,0,0,0");
             auto p=RuleSplit(s,",");
             if(p.size()==4&&p[0]!="255")
                 warn("ARGB opacity requires review");
             set(key,Color(s));
         };

         auto line=[&](const N* n,bool polygon)
         {
             if(!n)
             {
                 if(polygon)
                     set("LineWidth","0");
                 return;
             }
             auto t=SA(n,"type");
             color(n,polygon?"LineColor":"Color");
             set(polygon?"LineWidth":"Width",text(n,"Width","1"));
             set(polygon?"LineType":"Type","0");
             auto dash=n->FirstChildElement("Dash");
             if(dash)
             {
                 std::string v;
                 for(auto e=dash->FirstChildElement("DashItem");e;e=e->NextSiblingElement("DashItem"))
                 {
                     if(!v.empty())
                         v+=' ';
                     v+=e->GetText()?e->GetText():"";
                 }
                 set(polygon?"LineDashArray":"DashArray",v);
             }
              if(!polygon&&!ST(n,"Picture").empty())
                  set("IconName",ST(n,"Picture"));
              if(t!="SIMPLE"&&t!="DASH")
                  warn("line type "+t+" uses an approximate editable stroke; original retained");
              if(n->NextSiblingElement("LineLayer"))
                  warn("compound line: first stroke shown; original retained");
             };
             if(type=="POINT")
             {
              auto n=input->FirstChildElement("PointLayer");
              if(!n)
                  throw std::runtime_error("No PointLayer");
              auto t=SA(n,"type");
              set("Type","0");
              set("Size",text(n,"Size","16"));
              color(n,"Color");
              set("xmove",text(input,"OffsetX","0"));
              set("ymove",text(input,"OffsetY","0"));
              if(t=="PICTURE")
                  set("IconName",ST(n,"Picture"));
              else
              {
                  auto shape=ST(n,"Shape");
                  if(shape!="0")
                      warn("simple point Shape="+shape+" approximated as circle");
              }
              if(n->NextSiblingElement("PointLayer"))
                  warn("compound point: first symbol shown; original retained");
             }
             else if(type=="LINE")
                 line(input->FirstChildElement("LineLayer"),false);
             else if(type=="POLYGON")
             {
              auto n=input->FirstChildElement("PolygonLayer");
              if(!n)
                  throw std::runtime_error("No PolygonLayer");

              auto t=SA(n,"type");color(n,"FillColor");
              set("FillType",STrue(ST(n,"Transparent"))?"1":t=="PICTURE"?"2":"0");
              if(t=="PICTURE")
              {
                  set("PatternName",ST(n,"Picture"));
                  warn("picture fill placement/TextureFill requires review");
              }
              line(n->FirstChildElement("LineLayer"),true);
              if(n->NextSiblingElement("PolygonLayer"))
                  warn("compound polygon: first fill shown; original retained");
             }else if(type=="LABEL")
             {
                  set("Font",text(input,"Font","Arial"));
                  set("Size",text(input,"Size","10"));
                  color(input,"Color");
                  set("Type","0");
                  set("Align",text(input,"Align","4"));
                  set("xmove",text(input,"OffsetX","0"));set("ymove",text(input,"OffsetY","0"));
                  set("Suffix",ST(input,"Postfix"));
                  set("UseSuffix",ST(input,"Postfix").empty()?"FALSE":"TRUE");
                  warn("font effects, prefix/suffix and label placement require review");
             }else 
                 throw std::runtime_error("Unsupported style type: "+type);

             auto raw=Add(output,"StandardSource");
             tinyxml2::XMLPrinter original;
             input->Accept(&original);
             raw->SetText(original.CStr());
        }
    }

    StandardResult GenerateStandardXml(const fs::path& gss,const std::vector<size_t>& selected)
    {
         if(selected.empty())
             throw std::runtime_error("생성할 지도/축척을 체크하세요.");
         tinyxml2::XMLDocument styles;Read(styles,gss/"GSS_STYLE.xml","MapStyle");
         std::map<std::string,const N*> sourceStyles;
         std::vector<std::string> sourceWarnings;
         for(auto s=styles.RootElement()->FirstChildElement("Style");s;s=s->NextSiblingElement("Style"))
         {
             auto name=SA(s,"name");
             if(name.empty())
                 throw std::runtime_error("Empty standard style name");
             if(!sourceStyles.emplace(name,s).second)
                 sourceWarnings.push_back("DUPLICATE standard style; first definition retained: "+name);
         }
         StandardResult result;std::set<size_t> unique(selected.begin(),selected.end());
         for(size_t index:unique)
         {
             if(index>=StandardScales().size())
                 throw std::runtime_error("Invalid standard scale");

             const auto& spec=StandardScales()[index];

              tinyxml2::XMLDocument source;
              Read(source,gss/spec.sourceFile,"MapLayer");
              tinyxml2::XMLDocument out;
              out.InsertEndChild(out.NewDeclaration("xml version=\"1.0\" encoding=\"UTF-8\""));
              auto root=out.NewElement("InSIDEMapX3");
              out.InsertEndChild(root);
              root->SetAttribute("Standard","MND-STD-0023A");
              root->SetAttribute("StandardMap",spec.code.c_str());
              root->SetAttribute("StandardFamily",spec.family.c_str());
              root->SetAttribute("mapMakeConditions","1");
              SetST(root,"StandardGssRoot",fs::absolute(gss).u8string());
              auto layers=Add(root,"Layers");
              std::set<std::string> used;
              std::vector<std::string> warnings=sourceWarnings;
              int layerIndex=0;

              for(auto l=source.RootElement()->FirstChildElement("Layer");l;l=l->NextSiblingElement("Layer"))
              {
                   auto maps=RuleSplit(SA(l,"Map"),",");
                   if(SA(l,"Map").empty()||SA(l,"SHPSource").empty())
                   {
                       warnings.push_back("SKIPPED incomplete layer: "+SA(l,"Name")+" (Map/SHPSource missing)");
                       continue;
                   }
                   if(std::find(maps.begin(),maps.end(),spec.code)==maps.end())
                       continue;

                   auto layer=Add(layers,"Layer");++layerIndex;
                   layer->SetAttribute("alias",(SA(l,"Name")+"_"+SA(l,"GeometryType")).c_str());
                   SetST(layer,"Category",SA(l,"Category"));
                   SetST(layer,"Source",SA(l,"SHPSource"));
                   SetST(layer,"ProviderName","MXS");
                   SetST(layer,"Visible","TRUE");
                   SetST(layer,"DisplayType",Upper(SA(l,"DisplayType")));
                   SetST(layer,"LabelColumnName",SA(l,"LabelColumn"));
                   SetST(layer,"AngleColumn",SA(l,"AngleColumn"));
                   SetST(layer,"UseScale","FALSE");
                   SetST(layer,"MinScale","0");
                   SetST(layer,"MaxScale","0");
                   SetST(layer,"StandardMap",spec.code);
                   SetST(layer,"StandardOrder",std::to_string(layerIndex));
                   auto tableName="standard_rules_"+std::to_string(layerIndex);
                   SetST(layer,"UseStyleTable","TRUE");
                   SetST(layer,"StyleTableName",tableName);
                   SetST(layer,"StyleColumnName","__STANDARD_CONDITION__");
                   auto table=Add(root,"StyleTable");
                   table->SetAttribute("name",tableName.c_str());
                   table->SetAttribute("type","CHARACTER");
                   table->SetAttribute("ConditionMode","STANDARD");
                   int rule=0;
                   for(auto f=l->FirstChildElement("Feature"); f; f=f->NextSiblingElement("Feature"))
                   {
                        auto e=Add(table,"Element");
                        e->SetAttribute("value",std::to_string(++rule).c_str());
                        e->SetAttribute("name",SA(f,"Name").c_str());
                        auto geometry=SA(f,"GeometryStyle"),label=SA(f,"LabelStyle");
                        e->SetAttribute(Lower(SA(l,"GeometryType")).c_str(),geometry.c_str());
                        e->SetAttribute("label",label.c_str());
                        e->SetAttribute("disabled","FALSE");
                        if(!geometry.empty())
                            used.insert(geometry);
                        if(!label.empty())
                            used.insert(label);
                        std::string condition;
                        for(auto v=f->FirstChildElement("VVTStyle"); v; v=v->NextSiblingElement("VVTStyle"))
                        {
                             if(!v->FirstAttribute())
                             {
                                 condition.clear();
                                 break;
                             }
                             if(!condition.empty())
                                 condition+=" || ";
                             bool first=true;
                             for(auto a=v->FirstAttribute();a;a=a->Next())
                             {
                                 if(!first)
                                     condition+="; ";
                                 first=false;
                                 condition+=std::string(a->Name())+":"+a->Value();
                             }
                        }
                        SetST(e,"Condition",condition);
                        try
                        {
                            StandardFilter(condition);
                        }
                        catch(const std::exception& ex)
                        {
                            e->SetAttribute("disabled","TRUE");
                            warnings.push_back("DISABLED RULE "+SA(l,"Name")+" / "+SA(f,"Name")+": "+ex.what());
                        }
                   }
                   if(!rule)
                   {
                       SetST(layer,"Visible","FALSE");
                       warnings.push_back("NO RULES: "+SA(l,"Name"));
                   }

                   auto raw=Add(layer,"StandardSource");
                   tinyxml2::XMLPrinter original;
                   l->Accept(&original);
                   raw->SetText(original.CStr());
              }

              for(auto& name:used)
              {
                  auto found=sourceStyles.find(name);
                  if(found==sourceStyles.end())
                  {
                      warnings.push_back("MISSING STYLE: "+name);
                      continue;
                  }
                  auto output=Add(root,"style");
                  try
                  {
                      ConvertStyle(found->second,output,warnings);
                  }
                  catch(const std::exception& e)
                  {
                      root->DeleteChild(output);
                      warnings.push_back("OMITTED STYLE "+name+": "+e.what());
                  }
              }
              auto notes=Add(root,"StandardWarnings");
              for(auto& w:warnings)
                  SetST(Add(notes,"Item"),"Text",w);
              CopProfile p;
              p.file=fs::u8path(spec.file);
              p.xml=Dump(out);
              StudioModel model;
              model.Load(p);
              p=model.Snapshot();
              for(auto& l:p.layers)
                  l.enabled=l.visible;
              result.artifacts.push_back({p.file,p.xml});
              result.profiles.push_back(p);
              for(auto& w:warnings)
                  result.warnings.push_back(spec.title+": "+w);
         }

         std::ostringstream report;
         report<<"MND-STD-0023A standard XML generation\nSource: "<<gss.u8string()<<"\nLayer order is source XML order (bottom to top). Map attributes select nominal scales. No scale switch thresholds are invented; UseScale is FALSE until edited.\nCondition elements are mapMake extensions for SLD export, not legacy MapX VVT numeric codes. Raw source definitions are retained under StandardSource.\nUnmapped/approximate styles and disabled rules require review:\n";for(auto& w:result.warnings)report<<w<<"\n";
         result.artifacts.push_back({"standard-generation-report.txt",report.str()})
         ;return result;
    }
}
