#include <afxwin.h>
#include <afxdlgs.h>
#include <afxdialogex.h>
#include <afxcmn.h>
#include <atlimage.h>
#include "StyleStudio.h"
#include "StandardDialog.h"
#include "StandardXml.h"
#include "StyleStudioModel.h"
#include "SldImport.h"
#include "resource.h"
#include <fstream>
#include <memory>
namespace {
CString W(const std::string& s){int n=MultiByteToWideChar(CP_UTF8,0,s.data(),(int)s.size(),nullptr,0);CString r;if(n){MultiByteToWideChar(CP_UTF8,0,s.data(),(int)s.size(),r.GetBuffer(n),n);r.ReleaseBuffer(n);}return r;}
std::string U(const CString& s){int n=WideCharToMultiByte(CP_UTF8,0,s,s.GetLength(),nullptr,0,nullptr,nullptr);std::string r(n,'\0');if(n)WideCharToMultiByte(CP_UTF8,0,s,s.GetLength(),&r[0],n,nullptr,nullptr);return r;}
COLORREF Color(std::string s,COLORREF fallback=RGB(40,110,190)){if(!s.empty()&&s[0]=='#')s.erase(0,1);if(s.size()!=6||s.find_first_not_of("0123456789abcdefABCDEF")!=s.npos)return fallback;auto v=std::stoul(s,nullptr,16);return RGB((v>>16)&255,(v>>8)&255,v&255);}
class Preview:public CStatic {
public:
 std::function<void(CDC&,CRect)> render;
 void DrawSurface(CDC& dc){CRect r;GetClientRect(r);dc.FillSolidRect(r,RGB(250,250,250));if(render)render(dc,r);}
 afx_msg void OnPaint(){CPaintDC dc(this);DrawSurface(dc);}
 afx_msg LRESULT PrintClient(WPARAM hdc,LPARAM){auto dc=CDC::FromHandle((HDC)hdc);DrawSurface(*dc);return 0;}
 DECLARE_MESSAGE_MAP()
};
BEGIN_MESSAGE_MAP(Preview,CStatic)
 ON_WM_PAINT()
 ON_MESSAGE(WM_PRINTCLIENT,&Preview::PrintClient)
END_MESSAGE_MAP()

class StudioDialog:public CDialogEx 
{
 CComboBox profilesBox_,value_;
 CTabCtrl tabs_;
 CListCtrl items_,properties_;
 Preview preview_;
 std::unique_ptr<mm::StudioModel> model_;
 std::vector<mm::StudioNode*> nodes_;
 std::vector<mm::StudioProperty> props_;
 const std::vector<mm::Layer>& data_;
 CString output_;
 int profileIndex_=-1,propertyIndex_=-1;
 mm::StudioNode* selected_=nullptr;
 bool loading_=false,dirty_=false,pending_=false;
 std::map<std::string,mm::fs::path> images_;
 CString Text(int id)
 {
	 CString s;GetDlgItemText(id,s);
	 return s;
 }
 void Status(const CString& s)
 {
	 SetDlgItemText(IDC_ST_STATUS,s);
 }
 void Error(const std::exception& e)
 {
	 Status(W(e.what()));
	 AfxMessageBox(W(e.what()),MB_ICONERROR);
 }
 int Tab()
 {
	 return tabs_.GetCurSel();
 }
 void Commit()
 {
	 if(model_&&profileIndex_>=0)
		 profiles[profileIndex_]=model_->Snapshot();
 }
 bool ApplyPending()
 {
	 if(!pending_||!selected_||propertyIndex_<0)
		 return true;
	 try
	 {
		 auto key=props_[propertyIndex_].key;model_->Set(selected_,key,U(Text(IDC_ST_VALUE)));
		 dirty_=true;exported.clear();
		 pending_=false;
		 props_[propertyIndex_].value=U(Text(IDC_ST_VALUE));
		 properties_.SetItemText(propertyIndex_,1,Text(IDC_ST_VALUE));
		 Commit();
		 preview_.Invalidate();
		 return true;
	 }
	 catch(const std::exception& e)
	 {
		 Error(e);
		 return false;
	 }
 }
 void IndexImages()
 {
	 images_.clear();
	 if(!options.gssRoot.empty())
	 {
		 std::error_code ec;
		 mm::fs::recursive_directory_iterator it(options.gssRoot,mm::fs::directory_options::skip_permission_denied,ec),end;
		 for(;it!=end&&!ec;it.increment(ec))
			 if(it->is_regular_file(ec)&&(mm::SldKey(it->path().extension().u8string())==".bmp"||mm::SldKey(it->path().extension().u8string())==".png"))
				 images_.emplace(mm::SldKey(it->path().stem().u8string()),it->path());
	 }
 }
 void RefreshProfiles(int index)
 {
	 loading_=true;profilesBox_.ResetContent();
	 for(const auto& p:profiles)
		 {tinyxml2::XMLDocument d;d.Parse(p.xml.c_str());auto r=d.RootElement();std::string label=p.file.filename().u8string();if(r&&r->Attribute("StandardMap"))label=mm::SA(r,"StandardFamily")+" / "+mm::SA(r,"StandardMap")+" - "+label;profilesBox_.AddString(W(label));}
	 loading_=false;LoadProfile(index);
 }
 void LoadProfile(int index)
 {
	profileIndex_=index;
	model_.reset();
	selected_=nullptr;
	propertyIndex_=-1;
	pending_=false;
	if(index>=0&&index<(int)profiles.size())
	{
		model_=std::make_unique<mm::StudioModel>();
		model_->Load(profiles[index]);
		profilesBox_.SetCurSel(index);
	}
	RefreshItems();
 }
 void RefreshItems(mm::StudioNode* pick=nullptr)
 {
	loading_=true;
	items_.DeleteAllItems();
	nodes_=model_?model_->Items(Tab()):std::vector<mm::StudioNode*>{};
	int select=0;
	for(size_t i=0;i<nodes_.size();++i)
	{
		auto n=nodes_[i];
		items_.InsertItem((int)i,W(model_->Title(n)));
		if(Tab()==0)
		{
			auto p=model_->Snapshot();
			items_.SetItemText((int)i,1,W(p.layers[i].target));
		}
		else items_.SetItemText((int)i,1,W(mm::SA(n,"type")));
		if(n==pick)
			select=(int)i;
	}
	selected_=nodes_.empty()?nullptr:nodes_[select];
	if(!nodes_.empty())
		items_.SetItemState(select,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
	loading_=false;RefreshProperties();
 }
 void RefreshProperties(int select=0)
 {
	loading_=true;
	properties_.DeleteAllItems();
	props_=selected_?model_->Properties(selected_):std::vector<mm::StudioProperty>{};
	for(size_t i=0;i<props_.size();++i)
	{
		properties_.InsertItem((int)i,W(props_[i].label));
		properties_.SetItemText((int)i,1,W(props_[i].value));
	}
	propertyIndex_=props_.empty()?-1:std::min(select,(int)props_.size()-1);
	if(propertyIndex_>=0)
		properties_.SetItemState(propertyIndex_,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
	loading_=false;LoadValue();
	preview_.Invalidate();
 }
 void LoadValue()
 {
	loading_=true;value_.ResetContent();
	pending_=false;
	if(propertyIndex_<0)
	{
		SetDlgItemText(IDC_ST_VALUE,L"");
		loading_=false;return;
	}
	auto key=props_[propertyIndex_].key;
	if(key=="$target")
		for(const auto& d:data_)
			if(d.type!="RASTER")
				value_.AddString(W(d.name));
	if(key=="Visible"||key=="UseScale"||key=="UseStyleTable"||key=="$enabled")
	{
		value_.AddString(L"TRUE");
		value_.AddString(L"FALSE");
	}
	if(key=="DisplayType")
		for(auto s:{L"GEOMETRY",L"LABEL",L"BOTH"})
			value_.AddString(s);
	if(key=="@type")
		for(auto s:{L"CHARACTER",L"INTEGER",L"FLOATING"})
			value_.AddString(s);
	if(key=="Font")
		for(auto s:{L"맑은 고딕",L"굴림",L"Arial",L"Tahoma"})
			value_.AddString(s);
	int styleTab=key=="LabelStyleName"||key=="@label"?5:key=="@point"?2:key=="@line"?3:key=="@polygon"?4:0;
	if(key=="GeometryStyleName")
		for(int t=2;t<=4;++t)
			for(auto n:model_->Items(t))
				value_.AddString(W(mm::SA(n,"name")));
	if(styleTab)
		for(auto n:model_->Items(styleTab))
			value_.AddString(W(mm::SA(n,"name")));
	if(key=="StyleTableName")
		for(auto n:model_->Items(1))
			if(std::string(n->Name())=="StyleTable")
				value_.AddString(W(mm::SA(n,"name")));
	if(key=="LabelColumnName"||key=="StyleColumnName"||key=="@labelcolumn"||key=="@Column")
	{
		std::set<std::string> fields;
		for(auto& d:data_)
			for(auto& f:d.fields)fields.insert(f);
		for(auto& f:fields)
			value_.AddString(W(f));
	}
	SetDlgItemText(IDC_ST_VALUE,W(props_[propertyIndex_].value));
	loading_=false;
 }
 mm::StudioNode* PreviewNode()
 {
	 if(!selected_)
		 return nullptr;
	 if(Tab()>=2)
		 return selected_;
	 auto node=selected_;
     if(Tab()==0&&mm::STrue(mm::ST(node,"UseStyleTable"))){for(auto t:model_->Items(1))if(std::string(t->Name())=="StyleTable"&&mm::SA(t,"name")==mm::ST(node,"StyleTableName")){node=t;break;}}
     if(std::string(node->Name())=="StyleTable"){node=node->FirstChildElement("Element");while(node&&mm::STrue(mm::SA(node,"disabled")))node=node->NextSiblingElement("Element");if(!node)return nullptr;}
     std::string name=std::string(node->Name())=="Layer"?mm::ST(node,"GeometryStyleName"):mm::SA(node,"point");
	 if(name.empty())
		 name=mm::SA(node,"line");
	 if(name.empty())
		 name=mm::SA(node,"polygon");
	 if(name.empty())
		 name=std::string(node->Name())=="Layer"?mm::ST(node,"LabelStyleName"):mm::SA(node,"label");
	 for(int t=2;t<=5;++t)
		 for(auto n:model_->Items(t))
			 if(mm::SA(n,"name")==name)
				 return n;
	 return nullptr;
 }
 void Draw(CDC& dc,CRect r)
 {
	auto n=PreviewNode();
	dc.SetBkMode(TRANSPARENT);
	dc.SetTextColor(RGB(50,50,50));
	if(!n)
	{
		dc.DrawText(L"스타일을 선택하세요",r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
		return;
	}
	auto type=mm::SA(n,"type");
	auto num=[&](const char* key,int fallback)
	{
		try
		{
			return std::clamp((int)std::stod(mm::ST(n,key)),0,100);
		}
		catch(...)
		{
			return fallback;
		}
	};
	auto color=Color(mm::ST(n,type=="POLYGON"?"FillColor":"Color"));
	std::string reference=mm::ST(n,type=="POLYGON"?"PatternName":"IconName");
	if(!reference.empty()&&type!="LABEL")
	{
		auto path=mm::fs::u8path(reference);
		if(!mm::fs::is_regular_file(path))
		{
			auto it=images_.find(mm::SldKey(path.stem().u8string()));
			if(it!=images_.end())
				path=it->second;
		}
		CImage image;
		if(SUCCEEDED(image.Load(path.c_str())))
		{
			int w=std::max(1,image.GetWidth()),h=std::max(1,image.GetHeight());
			double scale=std::min(1.0,std::min((r.Width()-20.0)/w,(r.Height()-35.0)/h));
			int dw=(int)(w*scale),dh=(int)(h*scale);
			image.Draw(dc.GetSafeHdc(),r.CenterPoint().x-dw/2,r.CenterPoint().y-dh/2,dw,dh);
			return;
		}
		dc.DrawText(L"이미지를 찾을 수 없습니다",r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
		return;
  }
  CPen pen(PS_SOLID,std::max(1,num(type=="POLYGON"?"LineWidth":"Width",2)),type=="POLYGON"?Color(mm::ST(n,"LineColor"),RGB(60,60,60)):color);
  CBrush brush(color);
  auto oldPen=dc.SelectObject(&pen);
  auto oldBrush=dc.SelectObject(&brush);
  int cx=r.CenterPoint().x, cy=r.CenterPoint().y;
  if(type=="POINT")
  {
	  int size=std::max(3,num("Size",10));
	  if(mm::ST(n,"Type")=="1")
		  dc.Rectangle(cx-size/2,cy-size/2,cx+size/2+1,cy+size/2+1);
	  else 
		  dc.Ellipse(cx-size/2,cy-size/2,cx+size/2+1,cy+size/2+1);
  }
  else if(type=="LINE")
  {
	  if(mm::ST(n,"Type")=="0")
	  {
		  dc.MoveTo(r.left+18,cy);
		  dc.LineTo(r.right-18,cy);
	  }
	  else 
		  for(int x=r.left+18;x<r.right-18;x+=18)
		  {
			  dc.MoveTo(x,cy);
			  dc.LineTo(std::min(x+10,(int)r.right-18),cy);
		  }
  }
  else if(type=="POLYGON")
  {
	  if(mm::ST(n,"FillType")=="1")
		  dc.SelectStockObject(NULL_BRUSH);
	  dc.Rectangle(r.left+25,cy-40,r.right-25,cy+40);
  }
  else if(type=="LABEL")
  {
	  CFont font;
	  auto face=W(mm::ST(n,"Font"));
	  font.CreatePointFont(std::max(60,num("Size",10)*10),face.IsEmpty()?L"Arial":face);
	  auto old=dc.SelectObject(&font);
	  dc.SetTextColor(color);
	  dc.DrawText(L"지도 이름 Abc 123",r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
	  dc.SelectObject(old);}
	  dc.SelectObject(oldPen);
	  dc.SelectObject(oldBrush);
 }

 bool Discard()
 {
	 return !dirty_&&!pending_||AfxMessageBox(L"저장하지 않은 편집 내용이 있습니다. 현재 편집을 닫을까요?",MB_YESNO|MB_DEFBUTTON2|MB_ICONQUESTION)==IDYES;
 }

 void Write(const mm::fs::path& path,const std::string& text)
 {
	 auto temp=path;temp+=L".writing";
	 if(mm::fs::exists(temp))
	 throw std::runtime_error("임시 저장 파일이 이미 있습니다: "+temp.u8string());
	 {
		 std::ofstream out(temp,std::ios::binary);
		 out<<text;out.close();
		 if(!out)
		 {
			 mm::fs::remove(temp);
			 throw std::runtime_error("파일 저장 실패");
		 
		 }
	 }
	 if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
	 {
		 mm::fs::remove(temp);
		 throw 
			 std::runtime_error("파일 교체 실패");
	 }
 }
public:
 std::vector<mm::CopProfile> profiles;mm::CopOptions options;std::vector<mm::fs::path> exported;
 StudioDialog(CWnd* parent,const std::vector<mm::CopProfile>& p,const mm::CopOptions& o,const std::vector<mm::Layer>& d,const CString& output):CDialogEx(IDD_STUDIO,parent),data_(d),output_(output),profiles(p),options(o){}
 BOOL OnInitDialog() override 
 {
  CDialogEx::OnInitDialog();
  profilesBox_.SubclassDlgItem(IDC_ST_PROFILE,this);
  tabs_.SubclassDlgItem(IDC_ST_TABS,this);
  items_.SubclassDlgItem(IDC_ST_ITEMS,this);
  properties_.SubclassDlgItem(IDC_ST_PROPS,this);
  value_.SubclassDlgItem(IDC_ST_VALUE,this);
  preview_.SubclassDlgItem(IDC_ST_PREVIEW,this);
  preview_.render=[this](CDC& dc,CRect r)
  {
	  try{
		  Draw(dc,r);
	  }
	  catch(...)
	  {
		  dc.DrawText(L"미리보기 실패: 이미지/속성을 확인하세요",r,DT_CENTER|DT_WORDBREAK);
	  }
  };
  for(auto s:{L"레이어",L"스타일 테이블",L"점",L"선",L"면",L"라벨"})
	  tabs_.InsertItem(tabs_.GetItemCount(),s);
  tabs_.SetCurSel(0);
  for(auto list:{&items_,&properties_})
	  list->SetExtendedStyle(LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES|LVS_EX_DOUBLEBUFFER);
  items_.InsertColumn(0,L"항목",LVCFMT_LEFT,215);
  items_.InsertColumn(1,L"데이터 연결 / 유형",LVCFMT_LEFT,190);
  properties_.InsertColumn(0,L"속성",LVCFMT_LEFT,215);
  properties_.InsertColumn(1,L"값",LVCFMT_LEFT,240);
  if(profiles.empty())
	  profiles.push_back(mm::StudioModel::New(data_));
  if(options.gssRoot.empty()&&mm::fs::is_directory(L"D:/map/styles/GSS"))options.gssRoot=L"D:/map/styles/GSS";
  SetDlgItemText(IDC_ST_GSS,options.gssRoot.c_str());
  SetDlgItemText(IDC_ST_OUTPUT,output_);
  IndexImages();
  RefreshProfiles(0);
  Status(L"항목 선택 → 속성 선택 → 값 편집 → 적용. 변경 후 프로젝트를 저장하세요.");
  return TRUE;
 }
 afx_msg void NewProject()
 {
	 if(!Discard())
		 return;
	 profiles={mm::StudioModel::New(data_)};
	 exported.clear();dirty_=true;
	 exported.clear();
	 RefreshProfiles(0);
 }
 afx_msg void Open()
 {
	 if(!Discard())
		 return;
	 CFileDialog d(TRUE,L"mmstyle",nullptr,OFN_FILEMUSTEXIST,L"스타일 프로젝트 (*.mmstyle)|*.mmstyle||",this);
	 if(d.DoModal()!=IDOK)
		 return;
	 try
	 {
		 mm::ReadStudioProject(mm::ReadSldFile(mm::fs::path(d.GetPathName().GetString())),profiles,options);
		 exported.clear();
		 dirty_=false;
		 if(options.gssRoot.empty()&&mm::fs::is_directory(L"D:/map/styles/GSS"))options.gssRoot=L"D:/map/styles/GSS";
  SetDlgItemText(IDC_ST_GSS,options.gssRoot.c_str());
		 IndexImages();
		 RefreshProfiles(profiles.empty()?-1:0);
		 Status(L"스타일 프로젝트를 열었습니다.");
	 }
	 catch(const std::exception& e)
	 {
		 Error(e);
	 }
 }
 afx_msg void Save()
 {
	 if(!ApplyPending())
		 return;Commit();
	 CFileDialog d(FALSE,L"mmstyle",L"styles.mmstyle",OFN_OVERWRITEPROMPT,L"스타일 프로젝트 (*.mmstyle)|*.mmstyle||",this);
	 if(d.DoModal()!=IDOK)
		 return;
	 try
	 {
		 Write(mm::fs::path(d.GetPathName().GetString()),mm::StudioProject(profiles,options));
		 dirty_=false;
		 Status(L"프로젝트 저장 완료: "+d.GetPathName());
	 }
	 catch(const std::exception& e)
	 {
		 Error(e);
	 }
 }
 afx_msg void Import()
 {
	 if(!ApplyPending())
		 return;
	 Commit();
	 CFileDialog d(TRUE,L"xml",nullptr,OFN_FILEMUSTEXIST,L"MapX XML (*.xml)|*.xml||",this);
	 if(d.DoModal()!=IDOK)
		 return;
	 try
	 {
		 auto p=mm::LoadCop(mm::fs::path(d.GetPathName().GetString()));
		 mm::MatchCop(p,data_);
		 profiles.push_back(p);
		 dirty_=true;
		 exported.clear();
		 RefreshProfiles((int)profiles.size()-1);
	 }
	 catch(const std::exception& e)
	 {
		 Error(e);
	 }
 }
 afx_msg void Standard(){
     if(!ApplyPending())return;Commit();std::vector<mm::CopProfile> created;
     if(!CreateStandardProfiles(this,created,options,Text(IDC_ST_OUTPUT)))return;
     int first=(int)profiles.size();for(auto& p:created){mm::MatchCop(p,data_);profiles.push_back(std::move(p));}
     SetDlgItemText(IDC_ST_GSS,options.gssRoot.c_str());IndexImages();dirty_=true;exported.clear();RefreshProfiles(first);
     Status(L"축척별 XML을 추가했습니다. 편집 문서에서 지도/축척을 선택하세요. 표준 조건은 스타일 테이블에서 편집합니다.");
 }
 afx_msg void ApplyC4(){
     if(!ApplyPending()||profileIndex_<0)return;Commit();
     try{auto output=Text(IDC_ST_OUTPUT);if(output.IsEmpty())throw std::runtime_error("출력 폴더를 선택하세요.");auto p=profiles[profileIndex_];tinyxml2::XMLDocument check;check.Parse(p.xml.c_str());if(!check.RootElement()||!check.RootElement()->Attribute("StandardMap"))throw std::runtime_error("표준에서 생성한 지도/축척 문서를 선택하세요.");
         mm::SetST(check.RootElement(),"StandardGssRoot",options.gssRoot.u8string());tinyxml2::XMLPrinter print;check.Print(&print);p.xml=mm::PrettyXml(print.CStr());
         auto folder=mm::SaveBatch(mm::fs::path(output.GetString()),{{p.file.filename(),p.xml}});p.file=folder/p.file.filename();mm::PublishStandardCatalog({p},options.gssRoot,L"D:/map/styles/mapmake-style-catalog.xml");
         Status(L"C4IMap 연결 완료: "+CString(p.file.c_str())+L"\r\nC4IMap에서 로컬 SHP를 다시 불러오거나 프로그램을 다시 실행하세요. 기존 축척의 등록 파일은 백업했습니다.");
     }catch(const std::exception& e){Error(e);}
 }
 afx_msg void SaveXml()
 {
	 if(!ApplyPending()||!model_)
		 return;
	 Commit();
	 CFileDialog d(FALSE,L"xml",profiles[profileIndex_].file.filename().c_str(),OFN_OVERWRITEPROMPT,L"MapX XML (*.xml)|*.xml||",this);
	 if(d.DoModal()!=IDOK)
		 return;
	 try
	 {
		 Write(mm::fs::path(d.GetPathName().GetString()),profiles[profileIndex_].xml);
		 Status(L"선택 문서의 XML을 저장했습니다. 데이터 연결은 프로젝트 저장으로 보관하세요.");
	 }
	 catch(const std::exception& e)
	 {
		 Error(e);
	 }
 }
 afx_msg void Profile()
 {
	 if(loading_)
		 return;
	 if(!ApplyPending())
	 {
		 profilesBox_.SetCurSel(profileIndex_);
		 return;
	 }
	 Commit();
	 exported.clear();
	 LoadProfile(profilesBox_.GetCurSel());
 }
 afx_msg void TabChanging(NMHDR*,LRESULT* result){*result=ApplyPending()?0:1;}
 afx_msg void TabChanged(NMHDR*,LRESULT* result){*result=0;RefreshItems();}
 afx_msg void ItemChanging(NMHDR* h,LRESULT* result)
 {
	 *result=0;
	 auto n=(NMLISTVIEW*)h;
	 if(!loading_&&(n->uOldState&LVIS_SELECTED)&&!(n->uNewState&LVIS_SELECTED)&&!ApplyPending())
		 *result=1;
 }
 afx_msg void ItemChanged(NMHDR* h,LRESULT* result)
 {
	 *result=0;auto n=(NMLISTVIEW*)h;
	 if(loading_||!(n->uNewState&LVIS_SELECTED)||n->iItem<0||n->iItem>=(int)nodes_.size())
		 return;
	 selected_=nodes_[n->iItem];
	 RefreshProperties();
 }
 afx_msg void PropChanged(NMHDR* h,LRESULT* result)
 {
	 *result=0;
	 auto n=(NMLISTVIEW*)h;
	 if(loading_||!(n->uNewState&LVIS_SELECTED)||n->iItem<0||n->iItem>=(int)props_.size())
		 return;
	 propertyIndex_=n->iItem;
	 LoadValue();
 }
 afx_msg void EditChanged(){if(!loading_)pending_=true;}
 afx_msg void Apply()
 {
	 if(ApplyPending())
	 {
		 RefreshItems(selected_);
		 Status(L"속성 변경을 적용했습니다. 프로젝트 저장으로 보관하세요.");
	 }
 }
 afx_msg void PickColor()
 {
	 if(propertyIndex_<0)
		 return;
	 if(props_[propertyIndex_].key.find("Color")==std::string::npos)
	 {
		 AfxMessageBox(L"색상 속성을 선택하세요.");
		 return;
	 }
	 CColorDialog d(Color(U(Text(IDC_ST_VALUE))),CC_FULLOPEN,this);
	 if(d.DoModal()==IDOK)
	 {
		 auto c=d.GetColor();
		 CString s;
		 s.Format(L"%02X%02X%02X",GetRValue(c),GetGValue(c),GetBValue(c));
		 SetDlgItemText(IDC_ST_VALUE,s);
		 pending_=true;
		 Apply();
	 }
 }
 afx_msg void PickImage()
 {
	 if(propertyIndex_<0)
		 return;
	 auto k=props_[propertyIndex_].key;
	 if(k.find("IconName")==k.npos&&k!="PatternName")
	 {
		 AfxMessageBox(L"IconName 또는 PatternName 속성을 선택하세요.");
		 return;
	 }
	 CFileDialog d(TRUE,L"bmp",nullptr,OFN_FILEMUSTEXIST,L"심볼 이미지 (*.bmp;*.png)|*.bmp;*.png||",this);
	 if(d.DoModal()==IDOK)
	 {
		 SetDlgItemText(IDC_ST_VALUE,d.GetPathName());
		 pending_=true;
		 Apply();
	 }
 }
 afx_msg void Add()
 {
	 if(!ApplyPending()||!model_)
		 return;
	 try
	 {
		 auto n=model_->Add(Tab());
		 dirty_=true;
		 exported.clear();
		 RefreshItems(n);
	 }
	 catch(const std::exception& e)
	 {
		 Error(e);
	 }
 }
 afx_msg void Copy()
 {
	 if(!ApplyPending()||!selected_)
		 return;
	 try
	 {
		 auto n=model_->Add(Tab(),selected_,true);
		 dirty_=true;
		 exported.clear();
		 RefreshItems(n);
	 }
	 catch(const std::exception& e)
	 {
		 Error(e);
	 }
 }
 afx_msg void Remove()
 {
	 if(!selected_||!ApplyPending())
		 return;
	 if(AfxMessageBox(L"선택 항목을 삭제할까요? 이 항목을 참조하는 레이어/규칙은 다시 지정해야 합니다.",MB_YESNO|MB_DEFBUTTON2)!=IDYES)
		 return;
	 model_->Remove(selected_);
	 dirty_=true;
	 exported.clear();
	 RefreshItems();
 }
 afx_msg void Rule()
 {
	 if(!selected_||!ApplyPending())
		 return;
	 try
	 {
		 auto n=model_->AddRule(selected_);
		 dirty_=true;
		 exported.clear();
		 RefreshItems(n);
	 }
	 catch(const std::exception& e)
	 {
		 Error(e);
	 }
 }
 void Move(int direction)
 {
	 if(!selected_||!ApplyPending()||Tab()!=0)
		 return;
	 auto n=model_->Move(selected_,direction);
	 dirty_=true;
	 exported.clear();
	 RefreshItems(n);
 }
 afx_msg void Up(){Move(-1);}
 afx_msg void Down(){Move(1);}
 afx_msg void Gss()
 {
	 CFolderPickerDialog d(options.gssRoot.c_str(),0,this);
	 if(d.DoModal()==IDOK)
	 {
		 options.gssRoot=mm::fs::path(d.GetPathName().GetString());
		 SetDlgItemText(IDC_ST_GSS,d.GetPathName());
		 IndexImages();
		 dirty_=true;
		 exported.clear();
		 preview_.Invalidate();
	 }
 }
 afx_msg void Output()
 {
	 CFolderPickerDialog d(Text(IDC_ST_OUTPUT),0,this);
	 if(d.DoModal()==IDOK)
		 SetDlgItemText(IDC_ST_OUTPUT,d.GetPathName());
 }
 afx_msg void Export()
 {
	 if(!ApplyPending())
		 return;
	 Commit();
	 try
	 {
		 CWaitCursor wait;
		 auto output=Text(IDC_ST_OUTPUT);
		 if(output.IsEmpty())
		 throw std::runtime_error("출력 폴더를 선택하세요.");
		 if(profileIndex_<0)throw std::runtime_error("편집 문서를 선택하세요.");
         auto files=mm::BuildCop({profiles[profileIndex_]},options);
		 files.push_back(mm::StudioSld(files));
		 auto folder=mm::SaveBatch(mm::fs::path(output.GetString()),files);
		 exported.clear();
		 exported.push_back(folder/L"studio-styles.sld");
		 Status(L"SLD 생성 완료: "+CString(folder.c_str())+L"\r\n변환 보고서를 확인하고 '메인에 적용'을 누르면 생성한 SLD가 선택됩니다.");
	 }
	 catch(const std::exception& e)
	 {
		 Error(e);
	 }
 }
 void OnOK() override 
 {
	 if(!ApplyPending())
		 return;
	 Commit();
	 CDialogEx::OnOK();
 }
 void OnCancel() override 
 {
	 if(Discard())
		 CDialogEx::OnCancel();
 }
 DECLARE_MESSAGE_MAP()
};
BEGIN_MESSAGE_MAP(StudioDialog,CDialogEx)
 ON_BN_CLICKED(IDC_ST_NEW,&StudioDialog::NewProject)
 ON_BN_CLICKED(IDC_ST_OPEN,&StudioDialog::Open)
 ON_BN_CLICKED(IDC_ST_SAVE,&StudioDialog::Save)
 ON_BN_CLICKED(IDC_ST_IMPORT,&StudioDialog::Import)
 ON_BN_CLICKED(IDC_ST_C4,&StudioDialog::ApplyC4)
 ON_BN_CLICKED(IDC_ST_STANDARD,&StudioDialog::Standard)
 ON_BN_CLICKED(IDC_ST_XML,&StudioDialog::SaveXml)
 ON_BN_CLICKED(IDC_ST_EXPORT,&StudioDialog::Export)
 ON_BN_CLICKED(IDC_ST_ADD,&StudioDialog::Add)
 ON_BN_CLICKED(IDC_ST_COPY,&StudioDialog::Copy)
 ON_BN_CLICKED(IDC_ST_REMOVE,&StudioDialog::Remove)
 ON_BN_CLICKED(IDC_ST_RULE,&StudioDialog::Rule)
 ON_BN_CLICKED(IDC_ST_UP,&StudioDialog::Up)
 ON_BN_CLICKED(IDC_ST_DOWN,&StudioDialog::Down)
 ON_BN_CLICKED(IDC_ST_APPLY,&StudioDialog::Apply)
 ON_BN_CLICKED(IDC_ST_COLOR,&StudioDialog::PickColor)
 ON_BN_CLICKED(IDC_ST_IMAGE,&StudioDialog::PickImage)
 ON_BN_CLICKED(IDC_ST_GSS_PICK,&StudioDialog::Gss)
 ON_BN_CLICKED(IDC_ST_OUTPUT_PICK,&StudioDialog::Output)
 ON_CBN_SELCHANGE(IDC_ST_PROFILE,&StudioDialog::Profile)
 ON_CBN_EDITCHANGE(IDC_ST_VALUE,&StudioDialog::EditChanged)
 ON_CBN_SELCHANGE(IDC_ST_VALUE,&StudioDialog::EditChanged)
 ON_NOTIFY(TCN_SELCHANGING,IDC_ST_TABS,&StudioDialog::TabChanging)
 ON_NOTIFY(TCN_SELCHANGE,IDC_ST_TABS,&StudioDialog::TabChanged)
 ON_NOTIFY(LVN_ITEMCHANGING,IDC_ST_ITEMS,&StudioDialog::ItemChanging)
 ON_NOTIFY(LVN_ITEMCHANGING,IDC_ST_PROPS,&StudioDialog::ItemChanging)
 ON_NOTIFY(LVN_ITEMCHANGED,IDC_ST_ITEMS,&StudioDialog::ItemChanged)
 ON_NOTIFY(LVN_ITEMCHANGED,IDC_ST_PROPS,&StudioDialog::PropChanged)
END_MESSAGE_MAP()
}
bool ConfigureStyleStudio(CWnd* parent,std::vector<mm::CopProfile>& profiles,mm::CopOptions& options,const std::vector<mm::Layer>& data,const CString& output,std::vector<mm::fs::path>& exported){StudioDialog d(parent,profiles,options,data,output);if(d.DoModal()!=IDOK)return false;profiles=std::move(d.profiles);options=std::move(d.options);exported=std::move(d.exported);return true;}
