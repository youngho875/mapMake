#include <afxwin.h>
#include <afxcmn.h>
#include <afxdlgs.h>
#include <afxdialogex.h>
#include <thread>
#include <atlconv.h>
#include <mutex>
#include <fstream>
#include "TileDialog.h"
#include "RasterTiles.h"
#include "resource.h"
namespace {
CString W(const std::string& s){return CString(CA2W(s.c_str(),CP_UTF8));}
constexpr UINT TileDone=WM_APP+71;
class Tiles:public CDialogEx {
 std::vector<mm::Layer> layers_;mm::fs::path output_;std::thread worker_;std::atomic_bool cancel_{false};bool busy_=false;std::mutex mutex_;std::string progress_,error_;mm::TileResult result_;
 mm::fs::path runLog_; unsigned long long runId_=0; void LogRun(const char* event){std::ofstream log(runLog_,std::ios::app);SYSTEMTIME t;GetLocalTime(&t);log<<t.wYear<<"-"<<t.wMonth<<"-"<<t.wDay<<" "<<t.wHour<<":"<<t.wMinute<<":"<<t.wSecond<<" pid="<<GetCurrentProcessId()<<" run="<<runId_<<" "<<event<<" folder="<<result_.folder.u8string()<<" failed="<<result_.failed<<" cancelled="<<result_.cancelled<<" error="<<error_<<"\n";}
 size_t file_=0,totalFiles_=0;uint64_t done_=0,total_=0;
 CString Text(int id){CString s;GetDlgItemText(id,s);s.Trim();return s;}
 void Busy(bool value){busy_=value;for(int id:{IDC_T_START,IDC_T_OUTPUT,IDC_T_PICK,IDC_T_DLL,IDC_T_DLL_PICK,IDC_T_MIN,IDC_T_MAX,IDC_T_FORMAT,IDC_T_EPSG,IDC_T_LIMIT,IDC_T_AUTO,IDC_T_SCALE,IDC_T_MERGE,IDCANCEL})GetDlgItem(id)->EnableWindow(!value);GetDlgItem(IDC_T_STOP)->EnableWindow(value);if(!value)AutoZoom();}
public:
 Tiles(CWnd* parent,const std::vector<mm::Layer>& layers,const mm::fs::path& output):CDialogEx(IDD_TILES,parent),layers_(layers),output_(output){}
 ~Tiles(){cancel_=true;if(worker_.joinable())worker_.join();}
 afx_msg void AutoZoom(){GetDlgItem(IDC_T_MAX)->EnableWindow(!busy_&&IsDlgButtonChecked(IDC_T_AUTO)!=BST_CHECKED);GetDlgItem(IDC_T_SCALE)->EnableWindow(!busy_&&IsDlgButtonChecked(IDC_T_AUTO)==BST_CHECKED);}
 BOOL OnInitDialog()override{CDialogEx::OnInitDialog();SetDlgItemText(IDC_T_OUTPUT,output_.c_str());SetDlgItemText(IDC_T_DLL,L"D:\\work\\상황도\\gdal_sdk\\bin\\gdal.dll");SetDlgItemInt(IDC_T_MIN,0);SetDlgItemInt(IDC_T_MAX,12);SetDlgItemInt(IDC_T_LIMIT,50000);SetDlgItemInt(IDC_T_SCALE,50000);CheckDlgButton(IDC_T_AUTO,BST_CHECKED);CheckDlgButton(IDC_T_MERGE,BST_CHECKED);((CProgressCtrl*)GetDlgItem(IDC_T_PROGRESS))->SetRange32(0,1000);auto box=(CComboBox*)GetDlgItem(IDC_T_FORMAT);box->AddString(L"PNG (투명 배경)");box->AddString(L"JPG (빈 영역 검정)");box->AddString(L"고도 GeoTIFF (원본 높이값 보존)");box->SetCurSel(0);Busy(false);if(layers_.front().type!="RASTER"){box->ResetContent();box->AddString(L"MVT (.pbf) 도형 + 속성");box->SetCurSel(0);SetWindowText(L"SHP 벡터 타일 생성 (MVT)");}CString info;info.Format(L"선택 파일 %u개 / 래스터: XYZ 이미지, SHP: MVT",(UINT)layers_.size());SetDlgItemText(IDC_T_STATUS,info);return TRUE;}
 void OnCancel()override{if(busy_){cancel_=true;SetDlgItemText(IDC_T_STATUS,L"중지 중...");return;}CDialogEx::OnCancel();}
 afx_msg void Pick(){CFolderPickerDialog d(Text(IDC_T_OUTPUT),0,this);if(d.DoModal()==IDOK)SetDlgItemText(IDC_T_OUTPUT,d.GetPathName());}
 afx_msg void Dll(){CFileDialog d(TRUE,L"dll",L"gdal.dll",OFN_FILEMUSTEXIST,L"GDAL (gdal.dll)|gdal.dll||",this);if(d.DoModal()==IDOK)SetDlgItemText(IDC_T_DLL,d.GetPathName());}
 void OnOK()override{}
 BOOL PreTranslateMessage(MSG* message)override{if((message->message==WM_KEYDOWN||message->message==WM_KEYUP)&&message->wParam==VK_RETURN)return TRUE;return CDialogEx::PreTranslateMessage(message);}
 afx_msg void Start(){
  if(busy_||worker_.joinable())return;
  mm::TileOptions options;options.elevation=((CComboBox*)GetDlgItem(IDC_T_FORMAT))->GetCurSel()==2;options.merge=IsDlgButtonChecked(IDC_T_MERGE)==BST_CHECKED;BOOL validMin=FALSE,validMax=FALSE,validLimit=FALSE;options.minZoom=GetDlgItemInt(IDC_T_MIN,&validMin);options.maxZoom=GetDlgItemInt(IDC_T_MAX,&validMax);options.maxTiles=GetDlgItemInt(IDC_T_LIMIT,&validLimit);options.jpeg=((CComboBox*)GetDlgItem(IDC_T_FORMAT))->GetCurSel()==1;
  options.autoZoom=IsDlgButtonChecked(IDC_T_AUTO)==BST_CHECKED;
  if(!validMin||!validLimit||options.minZoom<0||options.minZoom>24||(!options.autoZoom&&(!validMax||options.maxZoom>24||options.minZoom>options.maxZoom))||!options.maxTiles||Text(IDC_T_OUTPUT).IsEmpty()){AfxMessageBox(L"출력 폴더, 줌 범위(0~24), 타일 수 제한을 확인하세요.");return;}
  options.autoZoom=IsDlgButtonChecked(IDC_T_AUTO)==BST_CHECKED;
  if(options.autoZoom){BOOL valid=FALSE;options.targetScale=GetDlgItemInt(IDC_T_SCALE,&valid);if(!valid||options.targetScale<=0){AfxMessageBox(L"목표 축척의 분모를 입력하세요. 예: 50000");return;}}
  if(!Text(IDC_T_EPSG).IsEmpty()){BOOL valid=FALSE;options.fallbackEpsg=GetDlgItemInt(IDC_T_EPSG,&valid);if(!valid||options.fallbackEpsg<=0){AfxMessageBox(L"원본 EPSG를 확인하세요.");return;}}
  options.output=Text(IDC_T_OUTPUT).GetString();options.gdalDll=Text(IDC_T_DLL).GetString();cancel_=false;file_=0;totalFiles_=layers_.size();done_=total_=0;progress_.clear();((CProgressCtrl*)GetDlgItem(IDC_T_PROGRESS))->SetPos(0);error_.clear();result_={};Busy(true);SetTimer(1,200,nullptr);HWND window=GetSafeHwnd();
  runLog_=options.output/L"tile-runs.log";runId_=GetTickCount64();std::error_code logError;mm::fs::create_directories(options.output,logError);LogRun("START");
  worker_=std::thread([this,options,window]{try{result_=mm::MakeTiles(layers_,options,cancel_,[this](const std::string& p){std::lock_guard<std::mutex> lock(mutex_);progress_=p;},[this](size_t f,size_t files,uint64_t d,uint64_t t){std::lock_guard<std::mutex> lock(mutex_);file_=f;totalFiles_=files;done_=d;total_=t;});}catch(const std::exception& e){error_=e.what();}::PostMessage(window,TileDone,0,0);});
 }
 afx_msg void Stop(){cancel_=true;SetDlgItemText(IDC_T_STATUS,L"중지 중...");}
 afx_msg void OnTimer(UINT_PTR id){if(id==1){std::lock_guard<std::mutex> lock(mutex_);SetDlgItemText(IDC_T_STATUS,W(progress_));double fraction=totalFiles_&&file_?((file_-1)+(total_?double(done_)/total_:0))/totalFiles_:0;
 ((CProgressCtrl*)GetDlgItem(IDC_T_PROGRESS))->SetPos((int)(fraction*1000));CString label;label.Format(L"전체 %.1f%% (파일별 균등 비중) / 파일 %llu/%llu / 현재 처리 %llu/%llu",fraction*100,(unsigned long long)file_,(unsigned long long)totalFiles_,done_,total_);SetDlgItemText(IDC_T_PERCENT,label);}CDialogEx::OnTimer(id);}
 afx_msg LRESULT Done(WPARAM,LPARAM){if(worker_.joinable())worker_.join();LogRun("FINISH");OnTimer(1);KillTimer(1);Busy(false);if(!error_.empty()){SetDlgItemText(IDC_T_STATUS,W(error_));AfxMessageBox(W(error_));return 0;}
  CString text;text.Format(L"%s: 생성 %llu / 빈 타일 %llu / 실패 파일 %d\r\n%s",result_.cancelled?L"중지":result_.failed?L"일부 실패":L"완료",result_.written,result_.empty,result_.failed,result_.folder.c_str());SetDlgItemText(IDC_T_STATUS,text);CString detail;for(auto& m:result_.messages)detail+=W(m)+L"\r\n";SetDlgItemText(IDC_T_LOG,detail);if(result_.failed)AfxMessageBox(L"일부 파일이 생성되지 않았습니다. 아래 로그와 출력 폴더의 result.txt를 확인하세요.");return 0;}
 DECLARE_MESSAGE_MAP()
};
BEGIN_MESSAGE_MAP(Tiles,CDialogEx)
 ON_BN_CLICKED(IDC_T_AUTO,&Tiles::AutoZoom)
 ON_BN_CLICKED(IDC_T_START,&Tiles::Start)
 ON_BN_CLICKED(IDC_T_STOP,&Tiles::Stop)
 ON_BN_CLICKED(IDC_T_PICK,&Tiles::Pick)
 ON_BN_CLICKED(IDC_T_DLL_PICK,&Tiles::Dll)
 ON_WM_TIMER()
 ON_MESSAGE(TileDone,&Tiles::Done)
END_MESSAGE_MAP()
}
void ShowTileDialog(CWnd* parent,const std::vector<mm::Layer>& layers,const mm::fs::path& output){Tiles dialog(parent,layers,output);dialog.DoModal();}
