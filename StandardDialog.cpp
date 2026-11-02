#include <afxwin.h>
#include <afxdlgs.h>
#include <afxdialogex.h>
#include <afxcmn.h>
#include <atlconv.h>
#include "StandardDialog.h"
#include "StandardXml.h"
#include "resource.h"
namespace {
CString W(const std::string& s){return CString(CA2W(s.c_str(),CP_UTF8));}
class StandardDialog:public CDialogEx {
 CListCtrl scales_;std::vector<mm::CopProfile>& profiles_;mm::CopOptions& options_;CString output_;
 CString Text(int id){CString t;GetDlgItemText(id,t);t.Trim();return t;}
public:
 StandardDialog(CWnd* p,std::vector<mm::CopProfile>& profiles,mm::CopOptions& options,const CString& output):CDialogEx(IDD_STANDARD,p),profiles_(profiles),options_(options),output_(output){}
 BOOL OnInitDialog()override{CDialogEx::OnInitDialog();scales_.SubclassDlgItem(IDC_STD_SCALES,this);scales_.SetExtendedStyle(LVS_EX_CHECKBOXES|LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);scales_.InsertColumn(0,L"지도 / 축척",LVCFMT_LEFT,350);for(size_t i=0;i<mm::StandardScales().size();++i){scales_.InsertItem((int)i,W(mm::StandardScales()[i].title));scales_.SetCheck((int)i,TRUE);}wchar_t exe[MAX_PATH];GetModuleFileNameW(nullptr,exe,MAX_PATH);auto root=mm::fs::path(exe).parent_path()/L"../../../standards/MND-STD-0023A/GSS";if(mm::fs::exists(options_.gssRoot/L"GSS_STYLE.xml"))root=options_.gssRoot;SetDlgItemText(IDC_STD_ROOT,root.lexically_normal().c_str());SetDlgItemText(IDC_STD_OUTPUT,output_);return TRUE;}
 void OnOK()override{
  try{if(Text(IDC_STD_OUTPUT).IsEmpty())throw std::runtime_error("출력 폴더를 선택하세요.");std::vector<size_t> chosen;for(int i=0;i<scales_.GetItemCount();++i)if(scales_.GetCheck(i))chosen.push_back(i);CWaitCursor wait;auto root=mm::fs::path(Text(IDC_STD_ROOT).GetString());auto result=mm::GenerateStandardXml(root,chosen);auto folder=mm::SaveBatch(mm::fs::path(Text(IDC_STD_OUTPUT).GetString()),result.artifacts);for(auto& p:result.profiles)p.file=folder/p.file;profiles_=std::move(result.profiles);options_.gssRoot=root;CString message;message.Format(L"축척별 XML %u개 생성 완료\r\n%s\r\n\r\n검토 항목 %u개: standard-generation-report.txt\r\n선택한 축척 문서를 편집한 뒤 XML 저장 또는 SLD 생성을 사용하세요.",(UINT)profiles_.size(),folder.c_str(),(UINT)result.warnings.size());AfxMessageBox(message,MB_ICONINFORMATION);CDialogEx::OnOK();}catch(const std::exception& e){AfxMessageBox(W(e.what()),MB_ICONERROR);}
 }
 afx_msg void Root(){CFolderPickerDialog d(Text(IDC_STD_ROOT),0,this);if(d.DoModal()==IDOK)SetDlgItemText(IDC_STD_ROOT,d.GetPathName());}
 afx_msg void Output(){CFolderPickerDialog d(Text(IDC_STD_OUTPUT),0,this);if(d.DoModal()==IDOK)SetDlgItemText(IDC_STD_OUTPUT,d.GetPathName());}
 afx_msg void All(){for(int i=0;i<scales_.GetItemCount();++i)scales_.SetCheck(i,TRUE);}
 afx_msg void None(){for(int i=0;i<scales_.GetItemCount();++i)scales_.SetCheck(i,FALSE);}
 DECLARE_MESSAGE_MAP()
};
BEGIN_MESSAGE_MAP(StandardDialog,CDialogEx)
 ON_BN_CLICKED(IDC_STD_ROOT_PICK,&StandardDialog::Root)
 ON_BN_CLICKED(IDC_STD_OUTPUT_PICK,&StandardDialog::Output)
 ON_BN_CLICKED(IDC_STD_ALL,&StandardDialog::All)
 ON_BN_CLICKED(IDC_STD_NONE,&StandardDialog::None)
END_MESSAGE_MAP()
}
bool CreateStandardProfiles(CWnd* parent,std::vector<mm::CopProfile>& profiles,mm::CopOptions& options,const CString& output){StandardDialog dialog(parent,profiles,options,output);return dialog.DoModal()==IDOK;}
