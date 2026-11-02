#pragma once
#include "vendor/tinyxml2/tinyxml2.h"
#include <stdexcept>
#include <string>
namespace mm 
{
    inline std::string PrettyXml(const std::string& xml) 
    {
        tinyxml2::XMLDocument doc;
        if(doc.Parse(xml.data(),xml.size())!=tinyxml2::XML_SUCCESS)
            throw std::runtime_error(std::string("Cannot format XML: ")+doc.ErrorStr());
        tinyxml2::XMLPrinter printer(nullptr,false);
        doc.Print(&printer);
        std::string result;
        for(const char* p=printer.CStr();*p;++p)
        {
            if(*p=='\n')
                result+='\r';
            result+=*p;
        }
        return result;
    }
}
