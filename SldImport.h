#pragma once
#include "Generator.h"
#include "XmlOutput.h"
#include <fstream>
#include <algorithm>
#include <functional>
#include <iterator>
#include <set>
namespace mm {
struct SldImport 
{
    std::vector<Artifact> files;
    std::vector<std::string> unmatched; 
    std::vector<std::string> mappings; 
    size_t matched=0; 
};
inline std::string ReadSldFile(const fs::path& path)
{
    std::ifstream in(path,std::ios::binary);
    if(!in)
        throw std::runtime_error("Cannot read SLD/resource: "+path.u8string());
    std::string value((std::istreambuf_iterator<char>(in)),{});
    if(in.bad())
        throw std::runtime_error("Cannot read SLD/resource: "+path.u8string());
    return value;
}
inline std::string SldTag(const char* name) 
{
    std::string s=name?name:"";
    auto p=s.find(':');
    return p==s.npos?s:s.substr(p+1);
}
inline tinyxml2::XMLElement* SldChild(tinyxml2::XMLElement* e,const char* name)
{
    for(auto c=e->FirstChildElement();c;c=c->NextSiblingElement())
        if(SldTag(c->Name())==name)
            return c;
    return nullptr;
}
inline std::string SldKey(std::string s) 
{
    auto first=s.find_first_not_of(" \t\r\n");
    if(first==s.npos)
        return {};
    s=s.substr(first,s.find_last_not_of(" \t\r\n")-first+1);
    std::replace(s.begin(),s.end(),'\\','/');
    for(auto& c:s)
        if(c>='A'&&c<='Z')
            c+=('a'-'A');
    return s;
}
inline std::string SldStem(std::string s) 
{
    s=SldKey(s);
    auto slash=s.find_last_of('/');
    if(slash!=s.npos)
        s=s.substr(slash+1);
    auto dot=s.find_last_of('.');
    if(dot!=s.npos)
        s=s.substr(0,dot);
    return s;
}
inline SldImport ImportSld(const std::vector<fs::path>& paths,std::vector<Layer>& layers)
{
    SldImport result;
    auto updated=layers;
    std::set<std::string> bound;
    std::vector<std::string> order;
    for(size_t i=0;i<paths.size();++i)
    {
        auto text=ReadSldFile(paths[i]);
        tinyxml2::XMLDocument doc;
        if(doc.Parse(text.data(),text.size())!=tinyxml2::XML_SUCCESS||!doc.RootElement()||
            SldTag(doc.RootElement()->Name())!="StyledLayerDescriptor")
            throw std::runtime_error("Invalid SLD: "+paths[i].u8string());
        auto root=doc.RootElement();
        auto file=fs::path("imported-style_"+std::to_string(i+1)+".sld");
        bool used=false;
        for(auto node=root->FirstChildElement();node;node=node->NextSiblingElement())
        {
            if(SldTag(node->Name())!="NamedLayer")  continue;
            auto nameNode=SldChild(node,"Name");
            std::string name=nameNode&&nameNode->GetText()?nameNode->GetText():"";
            auto found=std::find_if(updated.begin(),updated.end(),[&](const Layer& l){return l.name==name;});
            bool ambiguous=false;
            if(found==updated.end()&&!SldKey(name).empty())
            {
                // Prefer normalized layer names and full relative paths before basename fallback.
                for(int pass=0;pass<3&&found==updated.end()&&!ambiguous;++pass)
                {
                    for(auto it=updated.begin();it!=updated.end();++it)
                    {
                        auto rel=it->relative;
                        rel.replace_extension();
                        bool match=pass==0?(SldKey(name)==SldKey(it->name)||SldKey(name)==SldKey(it->relative.generic_u8string())||
                            SldKey(name)==SldKey(rel.generic_u8string())):
                            pass==1?SldStem(name)==SldKey(it->relative.stem().u8string()):
                            (SldStem(name).size()>4&&SldStem(name).compare(SldStem(name).size()-4,4,"_tmk")==0&&
                             SldStem(name).substr(0,SldStem(name).size()-4)==SldKey(it->relative.stem().u8string()));
                        if(match)
                        {
                            if(found!=updated.end())
                            {
                                ambiguous=true;
                                found=updated.end();
                                break;
                            }found=it;
                        }
                    }
                }
            }
            if(found==updated.end())
            {
                result.unmatched.push_back(paths[i].filename().u8string()+" | "+name+(ambiguous?" [ambiguous filename]":" [no matching checked data]"));
                continue;
            }
            if(!SldChild(node,"UserStyle"))
                throw std::runtime_error("SLD has no UserStyle: "+name);
            if(!bound.insert(found->name).second)
                throw std::runtime_error("Multiple SLD styles target the same layer. Select one SLD for: "+name);
            order.push_back(found->name);
            result.mappings.push_back(name+" -> "+found->name);
            nameNode->SetText(found->name.c_str());
            found->sldFile=file.generic_u8string();
            ++result.matched;used=true;
        }
        if(!used)   continue;
        // Package local graphics and rewrite references relative to the mapfile.
        size_t resource=0;
        std::function<void(tinyxml2::XMLElement*)> visit=[&](tinyxml2::XMLElement* e){
            if(SldTag(e->Name())=="OnlineResource"){
                for(auto a=e->FirstAttribute();a;a=a->Next())if(SldTag(a->Name())=="href"){
                    std::string href=a->Value(),key=a->Name();
                    if(href.rfind("http://",0)==0||href.rfind("https://",0)==0)break;
                    if(href.find("://")!=href.npos||href.rfind("data:",0)==0)throw std::runtime_error("Unsupported SLD resource URI: "+href);
                    std::replace(href.begin(),href.end(),'\\','/');auto source=fs::u8path(href);
                    if(source.is_relative())
                        source=paths[i].parent_path()/source;
                    auto target=fs::path("symbols")/("sld_"+std::to_string(i+1)+"_"+std::to_string(++resource)+source.extension().u8string());
                    result.files.push_back({target,ReadSldFile(source)});
                    e->SetAttribute(key.c_str(),target.generic_u8string().c_str());
                    break;
                }
            }
            for(auto c=e->FirstChildElement();c;c=c->NextSiblingElement())
                visit(c);
        };
        visit(root);
        tinyxml2::XMLPrinter p;doc.Print(&p);result.files.push_back({file,PrettyXml(p.CStr())});
    }
    if(!result.matched)
    {
        std::string message="SLD 연결 실패: 체크된 데이터와 연결되는 레이어가 없습니다.\nSLD 이름과 데이터 파일명을 확인하세요. 같은 파일명이 여러 개면 " \
            "XML 대상 이름을 메인 레이어 이름으로 지정하세요.\n\nSLD:\n";
        for(const auto& item:result.unmatched)
            message+=item+"\n";
        message+="\n체크된 데이터 (레이어 이름 | 파일):\n";
        for(const auto& l:updated)
            message+=l.name+" | "+l.relative.generic_u8string()+"\n";
        throw std::runtime_error(message);
    }
    for(const auto& l:updated)
        if(!bound.count(l.name))
            result.unmatched.push_back("DATA (default style): "+l.name);
    // Preserve SLD layer drawing order; unmatched data retain their relative order afterwards.
    std::stable_sort(updated.begin(),updated.end(),[&](const Layer& a,const Layer& b)
        {return std::find(order.begin(),order.end(),a.name)<std::find(order.begin(),order.end(),b.name);});
    layers=std::move(updated);
    return result;
}
}
