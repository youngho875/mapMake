#pragma once
#include "CopDialog.h"
bool ConfigureStyleStudio(CWnd* parent,std::vector<mm::CopProfile>& profiles,mm::CopOptions& options,
	const std::vector<mm::Layer>& data,const CString& output,std::vector<mm::fs::path>& exported);
