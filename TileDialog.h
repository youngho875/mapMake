#pragma once
#include "Generator.h"
class CWnd;
void ShowTileDialog(CWnd* parent,const std::vector<mm::Layer>& layers,const mm::fs::path& output);
