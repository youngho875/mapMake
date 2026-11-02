#include "StandardXml.h"
#include "XmlOutput.h"
#include <fstream>
#include <algorithm>
#include <map>
#include <chrono>
#ifdef _WIN32
#include <windows.h>
#endif
namespace mm {
void PublishStandardCatalog(const std::vector<CopProfile>& profiles,const fs::path& gss,const fs::path& destination){
 if(profiles.empty()||!fs::is_directory(gss))throw std::runtime_error("Choose a valid GSS image folder before applying to C4IMap");
 tinyxml2::XMLDocument catalog;auto path=fs::absolute(destination);if(fs::exists(path)){auto text=ReadCopXml(path);if(catalog.Parse(text.c_str())!=tinyxml2::XML_SUCCESS||!catalog.RootElement()||std::string(catalog.RootElement()->Name())!="MapMakeStyleCatalog")throw std::runtime_error("C4IMap style catalog is invalid; existing file preserved");}
 else{catalog.InsertEndChild(catalog.NewDeclaration("xml version=\"1.0\" encoding=\"UTF-8\""));catalog.InsertEndChild(catalog.NewElement("MapMakeStyleCatalog"));}
 auto root=catalog.RootElement();for(auto& p:profiles){tinyxml2::XMLDocument d;if(d.Parse(p.xml.c_str())!=tinyxml2::XML_SUCCESS||!d.RootElement())throw std::runtime_error("Invalid profile XML");auto r=d.RootElement();auto family=r->Attribute("StandardFamily"),code=r->Attribute("StandardMap");if(!family||!code)throw std::runtime_error("표준에서 생성한 축척별 XML을 선택하세요.");
  auto found=std::find_if(StandardScales().begin(),StandardScales().end(),[&](const StandardScale& s){return s.family==family&&s.code==code;});if(found==StandardScales().end())throw std::runtime_error("Unknown standard scale");if(!fs::is_regular_file(p.file))throw std::runtime_error("Save XML before applying it to C4IMap");
  for(auto e=root->FirstChildElement("Entry");e;){auto next=e->NextSiblingElement("Entry");auto file=e->Attribute("file");if(file&&found->file==file)root->DeleteChild(e);e=next;}
  auto entry=catalog.NewElement("Entry");entry->SetAttribute("file",found->file.c_str());entry->SetAttribute("xml",fs::absolute(p.file).u8string().c_str());entry->SetAttribute("gss",fs::absolute(gss).u8string().c_str());root->InsertEndChild(entry);
 }
 fs::create_directories(path.parent_path());tinyxml2::XMLPrinter printer;catalog.Print(&printer);auto temp=path;temp+=".tmp-"+std::to_string(std::chrono::system_clock::now().time_since_epoch().count());{std::ofstream out(temp,std::ios::binary);out<<PrettyXml(printer.CStr());out.close();if(!out){fs::remove(temp);throw std::runtime_error("Cannot write C4IMap style catalog");}}
 if(fs::exists(path)){auto backup=path;backup+=".backup-"+std::to_string(std::chrono::system_clock::now().time_since_epoch().count());fs::copy_file(path,backup);}
#ifdef _WIN32
 if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){fs::remove(temp);throw std::runtime_error("Cannot replace C4IMap style catalog");}
#else
 fs::rename(temp,path);
#endif
}
}
