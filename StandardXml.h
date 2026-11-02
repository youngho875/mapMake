#pragma once
#include "CopStyle.h"
namespace mm 
{
	struct StandardScale 
	{
		std::string family,code,title,file,sourceFile;
	}
	;
	const std::vector<StandardScale>& StandardScales();
	struct StandardResult 
	{
		std::vector<CopProfile> profiles;std::vector<Artifact> artifacts;std::vector<std::string> warnings;
	};

	StandardResult GenerateStandardXml(const fs::path& gss,const std::vector<size_t>& selected);
	void PublishStandardCatalog(const std::vector<CopProfile>& profiles,const fs::path& gss,const fs::path& destination);
	std::string ReadCopXml(const fs::path& path);
}
