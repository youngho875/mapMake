#pragma once
#include <afxwin.h>
#include "CopStyle.h"
// Returns true when the user applies the XML configuration to the main generator.
bool ConfigureCop(CWnd* parent, std::vector<mm::CopProfile>& profiles,mm::CopOptions& options
	,const std::vector<mm::Layer>& layers, const CString& outputFolder);
