#include <afxwin.h>
#include <afxdlgs.h>
#include <afxdialogex.h>
#include <afxcmn.h>
#include <afxole.h>
#include <thread>
#include <algorithm>
#include <stdexcept>
#include <atomic>
#include "Generator.h"
#include "OgcMetadata.h"
#include "CopDialog.h"
#include "SldImport.h"
#include "GeneratedSld.h"
#include "StyleStudio.h"
#include "TileDialog.h"
#include <set>
#include "resource.h"

namespace fs=std::filesystem;

namespace {
constexpr UINT ScanDone=WM_APP+10;
CString Wide(const std::string& s) 
{
    int n=MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0);

    CString result;
    if(n)
    {
        MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),result.GetBuffer(n),n);
        result.ReleaseBuffer(n);
    }
    return result;
}

std::string Utf8(const CString& s) 
{
    int n=WideCharToMultiByte(CP_UTF8,0,s.GetString(),s.GetLength(),nullptr,0,nullptr,nullptr);
    
    std::string result(n,'\0');
    if(n)WideCharToMultiByte(CP_UTF8,0,s.GetString(),s.GetLength(),&result[0],n,nullptr,nullptr);

    return result;
}
}
class MainDialog : public CDialogEx 
{
    CListCtrl list_;
    std::vector<fs::path> sldFiles_;
    std::vector<mm::CopProfile> copProfiles_;
    mm::CopOptions copOptions_;
    std::vector<mm::Layer> layers_;
    std::thread worker_;
    std::atomic_bool cancel_{false};
    mm::ScanResult pending_;
    std::string scanError_, scannedExtensions_;
    fs::path scannedRoot_;
    bool busy_=false, recursive_=true;

    CString Value(int id) 
    {
        CString s;GetDlgItemText(id,s);
        s.Trim();
        return s;
    }
    std::string Text(int id) 
    {
        return Utf8(Value(id));
    }

    bool Checked(int id) 
    {
        return IsDlgButtonChecked(id)==BST_CHECKED;
    }

    void Busy(bool value) 
    {
        busy_=value;
        for(int id:
        {
            IDC_INPUT,
                IDC_INPUT_PICK,
                IDC_EXTENSIONS,
                IDC_RECURSIVE,
                IDC_SCAN,
                IDC_GENERATE,
                IDC_RASTER_TILES,
                IDC_APPLY,
                IDC_ALL,
                IDC_NONE,
                IDC_SOURCE_EPSG
        })GetDlgItem(id)->EnableWindow(!value);

        GetDlgItem(IDC_STOP)->EnableWindow(value);
        GetDlgItem(IDC_RECURSIVE)->EnableWindow(FALSE);
    }
    void RefreshRow(int i) 
    {
        const auto& l=layers_[static_cast<size_t>(i)];
        list_.SetItemText(i,6,l.type=="RASTER"?L"영상 원본":Wide(l.rgb.empty()?"40 110 190":l.rgb));
        list_.SetItemText(i,1,Wide(l.type));list_.SetItemText(i,2,Wide(l.name));
        list_.SetItemText(i,3,Wide(l.epsg));list_.SetItemText(i,4,Wide(l.featureId));
        list_.SetItemText(i,5,l.issue.empty()?(l.type=="RASTER"?L"래스터 후보 / 형식 확인 필요":L"벡터 / 좌표계 확인 필요"):Wide(l.issue));
    }
public:
    MainDialog():CDialogEx(IDD_MAIN) {}

    BOOL OnInitDialog() override 
    {
        CDialogEx::OnInitDialog();
        list_.SubclassDlgItem(IDC_FILES,this);
        list_.SetExtendedStyle(LVS_EX_CHECKBOXES|LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES|LVS_EX_DOUBLEBUFFER);
        const wchar_t* titles[]={L"파일 (입력 폴더 기준)",L"종류",L"레이어 이름",L"원본 EPSG",L"WFS ID 필드",L"상태",L"RGB"};
        const int widths[]={245,75,180,85,100,240,110};
        for(int i=0;i<7;++i)list_.InsertColumn(i,titles[i],LVCFMT_LEFT,widths[i]);
        SetDlgItemText(IDC_EXTENSIONS,L"tif");
        SetDlgItemText(IDC_SOURCE_EPSG,L"4326");SetDlgItemText(IDC_MAP_EPSG,L"4326");
        auto units=static_cast<CComboBox*>(GetDlgItem(IDC_UNITS));
        units->AddString(L"DD");
        units->AddString(L"METERS");
        units->AddString(L"FEET");
        units->SetCurSel(0);
        SetDlgItemText(IDC_EXTENT,L"124 33 132 39");SetDlgItemText(IDC_RGB,L"40 110 190");
        SetDlgItemText(IDC_SERVER_ROOT,L"/srv/maps/data");SetDlgItemText(IDC_MAP_PATH,L"/srv/maps/mapserver.map");
        SetDlgItemText(IDC_INTERNAL,L"http://127.0.0.1/cgi-bin/mapserv");SetDlgItemText(IDC_PUBLIC,L"http://localhost/cgi-bin/mapserv");
        SetDlgItemText(IDC_CACHE,L"/var/cache/mapcache");
        SetDlgItemText(IDC_WMTS_URL,L"http://localhost/mapcache/wmts");
        SetDlgItemText(IDC_OUTPUT,(fs::current_path()/L"generated").c_str());
        for(int id:{IDC_RECURSIVE,IDC_WMS,IDC_WMTS,IDC_SLD})CheckDlgButton(id,BST_CHECKED);
        GetDlgItem(IDC_WMS)->EnableWindow(FALSE);Busy(false);
        SetDlgItemText(IDC_STATUS,L"입력 폴더를 선택하고 검색하세요. WMTS는 공급원으로 WMS를 함께 사용합니다.");
        return TRUE;
    }
    afx_msg void RasterTiles(){
        if(busy_)return;std::vector<mm::Layer> selected;
        for(int i=0;i<list_.GetItemCount();++i)if(list_.GetCheck(i)&&(layers_[i].type=="RASTER"||layers_[i].local.extension()==L".shp"||layers_[i].local.extension()==L".SHP"))selected.push_back(layers_[i]);
        if(selected.empty()){AfxMessageBox(L"검색 목록에서 래스터 또는 SHP 파일을 체크하세요.");return;}
        bool raster=selected.front().type=="RASTER";for(const auto& l:selected)if((l.type=="RASTER")!=raster){AfxMessageBox(L"래스터와 SHP는 별도로 선택하여 생성하세요.");return;}
        ShowTileDialog(this,selected,fs::path(Value(IDC_OUTPUT).GetString()));
    }
    afx_msg void InputFolder() 
    {
        CFolderPickerDialog dlg(Value(IDC_INPUT),0,this);
        if(dlg.DoModal()==IDOK){SetDlgItemText(IDC_INPUT,dlg.GetPathName());ScanFiles();}
    }
    afx_msg void OutputFolder() 
    {
        CFolderPickerDialog dlg(Value(IDC_OUTPUT),0,this);
        if(dlg.DoModal()==IDOK)SetDlgItemText(IDC_OUTPUT,dlg.GetPathName());
    }
    afx_msg void ScanFiles() 
    {
        if(busy_)   return;
        try {
            auto input=Value(IDC_INPUT);
            if(input.IsEmpty())
            throw 
                std::runtime_error("Choose an input folder first.");

            auto root=fs::absolute(fs::path(input.GetString())).lexically_normal();
            auto ext=Text(IDC_EXTENSIONS),epsg=Text(IDC_SOURCE_EPSG);
            mm::ParseExtensions(ext);
            mm::ValidateEpsg(epsg);
            if(!fs::is_directory(root))
            throw 
                std::runtime_error("Input folder does not exist.");

            if(worker_.joinable())
                worker_.join();

            layers_.clear();list_.DeleteAllItems();pending_={};scanError_.clear();SetDlgItemText(IDC_RESULTS,L"");
            scannedRoot_=root;scannedExtensions_=ext;recursive_=true;CheckDlgButton(IDC_RECURSIVE,BST_CHECKED);
            cancel_=false;Busy(true);SetDlgItemText(IDC_STATUS,L"검색 중... 중지 버튼으로 취소할 수 있습니다.");
            const HWND window=GetSafeHwnd();const bool recursive=recursive_;
            worker_=std::thread([this,root,ext,epsg,window,recursive] {
                try {pending_=mm::Scan(root,ext,recursive,&cancel_);for(auto& l:pending_.layers)l.epsg=epsg;}
                catch(const std::exception& e){scanError_=e.what();}
                ::PostMessage(window,ScanDone,0,0);
            });
        }catch(const std::exception& e){Busy(false);AfxMessageBox(Wide(e.what()),MB_ICONERROR);}
    }
    afx_msg LRESULT ScanComplete(WPARAM,LPARAM) {
        if(worker_.joinable())worker_.join();Busy(false);
        if(cancel_){layers_.clear();pending_={};SetDlgItemText(IDC_STATUS,L"검색을 취소했습니다. 다시 검색하세요.");return 0;}
        if(!scanError_.empty()){SetDlgItemText(IDC_STATUS,L"검색 실패");AfxMessageBox(Wide(scanError_),MB_ICONERROR);return 0;}
        layers_=std::move(pending_.layers);list_.SetRedraw(FALSE);
        int valid=0;
        for(size_t i=0;i<layers_.size();++i){int row=list_.InsertItem(static_cast<int>(i),Wide(layers_[i].relative.generic_u8string()));RefreshRow(row);if(layers_[i].issue.empty()){list_.SetCheck(row,TRUE);++valid;}}
        list_.SetRedraw(TRUE);list_.Invalidate();
        CString status;status.Format(L"검색 %d개 / 기본 체크 %d개 / 접근 경고 %d개. 체크된 파일을 생성하며, 파란색 선택 행에는 EPSG·ID 설정을 적용합니다.",static_cast<int>(layers_.size()),valid,static_cast<int>(pending_.warnings.size()));SetDlgItemText(IDC_STATUS,status);
        if(!pending_.warnings.empty()){
            CString warnings=L"일부 폴더 또는 파일을 읽지 못했습니다.\r\n";
            for(const auto& w:pending_.warnings)warnings+=Wide(w)+L"\r\n";
            SetDlgItemText(IDC_RESULTS,warnings);
        }
        return 0;
    }
    afx_msg void StopScan(){cancel_=true;SetDlgItemText(IDC_STATUS,L"검색 중지 중...");}
    afx_msg void All(){for(int i=0;i<list_.GetItemCount();++i)list_.SetCheck(i,layers_[static_cast<size_t>(i)].issue.empty());}
    afx_msg void None(){for(int i=0;i<list_.GetItemCount();++i)list_.SetCheck(i,FALSE);}
    afx_msg void SelectionChanged(NMHDR* header,LRESULT* result) {
        *result=0;auto change=reinterpret_cast<NMLISTVIEW*>(header);
        if(!(change->uNewState&LVIS_SELECTED)||(change->uOldState&LVIS_SELECTED)||change->iItem<0||static_cast<size_t>(change->iItem)>=layers_.size())return;
        const auto& l=layers_[static_cast<size_t>(change->iItem)];
        SetDlgItemText(IDC_SOURCE_EPSG,Wide(l.epsg));
        SetDlgItemText(IDC_RGB,Wide(l.rgb.empty()?"40 110 190":l.rgb));
        auto combo=static_cast<CComboBox*>(GetDlgItem(IDC_FEATURE_ID));combo->ResetContent();combo->AddString(L"");
        for(const auto& f:l.fields)combo->AddString(Wide(f));
        combo->SetWindowText(Wide(l.featureId));
    }
    afx_msg void Apply(){
        try {
            auto epsg=Text(IDC_SOURCE_EPSG),id=Text(IDC_FEATURE_ID);mm::ValidateEpsg(epsg);
            std::vector<int> selected;POSITION p=list_.GetFirstSelectedItemPosition();
            while(p){int i=list_.GetNextSelectedItem(p);selected.push_back(i);const auto& l=layers_[static_cast<size_t>(i)];
                if(l.type!="RASTER"&&!id.empty()&&std::find(l.fields.begin(),l.fields.end(),id)==l.fields.end())throw std::runtime_error(l.relative.u8string()+": ID field does not exist in the DBF.");}
            if(selected.empty())throw std::runtime_error("Highlight one or more rows before applying settings (Ctrl/Shift for multiple selection).");
            for(int i:selected){auto& l=layers_[static_cast<size_t>(i)];l.epsg=epsg;if(l.type!="RASTER")l.featureId=id;RefreshRow(i);}
            SetDlgItemText(IDC_STATUS,L"선택 행에 EPSG·ID 필드를 적용했습니다. 좌표계는 원본 데이터와 일치해야 합니다.");
        }catch(const std::exception& e){AfxMessageBox(Wide(e.what()),MB_ICONERROR);}
    }
    afx_msg void ApplyColor(){
        try {
            auto rgb=mm::NormalizeRgb(Text(IDC_RGB));int count=0;
            POSITION p=list_.GetFirstSelectedItemPosition();
            while(p){int i=list_.GetNextSelectedItem(p);auto& layer=layers_[static_cast<size_t>(i)];
                if(layer.type!="RASTER"){layer.rgb=rgb;RefreshRow(i);++count;}}
            if(!count){AfxMessageBox(L"색상을 적용할 벡터 행을 선택하세요. Ctrl/Shift로 여러 행을 선택할 수 있습니다.");return;}
            SetDlgItemText(IDC_STATUS,L"선택한 벡터 레이어의 색상을 저장했습니다. .map과 SLD에 반영됩니다. 이미지 심볼·래스터의 픽셀 색상은 유지됩니다.");
        }catch(const std::exception& e){AfxMessageBox(Wide(e.what()),MB_ICONERROR);}
    }
    afx_msg void PickColor(){
        CColorDialog dialog(RGB(40,110,190),CC_FULLOPEN,this);
        if(dialog.DoModal()==IDOK){auto c=dialog.GetColor();CString value;value.Format(L"%u %u %u",GetRValue(c),GetGValue(c),GetBValue(c));SetDlgItemText(IDC_RGB,value);ApplyColor();}
    }
    afx_msg void Services(){
        if(Checked(IDC_WMTS))CheckDlgButton(IDC_WMS,BST_CHECKED);
        GetDlgItem(IDC_WMS)->EnableWindow(!Checked(IDC_WMTS));
        SetDlgItemText(IDC_STATUS,Checked(IDC_WMTS)?L"WMTS를 선택하면 공급원 WMS도 활성화됩니다. WFS는 벡터 파일에만 적용됩니다.":L"서비스를 복수 선택할 수 있습니다. WFS는 벡터 파일에만 적용됩니다.");
    }
    afx_msg void XmlStyles(){
        std::vector<mm::Layer> checked;
        for(int i=0;i<list_.GetItemCount();++i)if(list_.GetCheck(i))checked.push_back(layers_[static_cast<size_t>(i)]);
        if(ConfigureCop(this,copProfiles_,copOptions_,checked,Value(IDC_OUTPUT))){
            CheckDlgButton(IDC_SLD,BST_CHECKED);
            SetDlgItemText(IDC_STATUS,copProfiles_.empty()?L"기본 SLD 생성 모드입니다.":L"XML 기반 SLD 모드입니다. 메인 생성 시 체크된 데이터의 레이어 이름과 연결되어야 합니다.");
        }
    }
    afx_msg void StyleStudio(){
        std::vector<mm::Layer> checked;
        for(int i=0;i<list_.GetItemCount();++i)if(list_.GetCheck(i))checked.push_back(layers_[static_cast<size_t>(i)]);
        std::vector<fs::path> exported;
        if(ConfigureStyleStudio(this,copProfiles_,copOptions_,checked,Value(IDC_OUTPUT),exported)){
            CheckDlgButton(IDC_SLD,BST_CHECKED);
            sldFiles_=std::move(exported);CString paths;for(auto& path:sldFiles_){if(!paths.IsEmpty())paths+=L"; ";paths+=path.c_str();}SetDlgItemText(IDC_SLD_PATH,paths);
            SetDlgItemText(IDC_STATUS,sldFiles_.empty()?L"편집 내용을 적용했습니다. 스타일 편집기에서 SLD 생성 후 메인에 적용하면 .map과 연결됩니다.":L"편집기에서 생성한 SLD가 선택되었습니다. 선택 파일로 생성을 누르면 스타일이 연결된 MAP을 만듭니다.");
        }
    }
    afx_msg void ChooseSld(){
        CFileDialog dlg(TRUE,L"sld",nullptr,OFN_FILEMUSTEXIST|OFN_ALLOWMULTISELECT|OFN_EXPLORER,L"SLD 파일 (*.sld;*.xml)|*.sld;*.xml|모든 파일 (*.*)|*.*||",this);
        std::vector<wchar_t> buffer(65536);dlg.m_ofn.lpstrFile=buffer.data();dlg.m_ofn.nMaxFile=static_cast<DWORD>(buffer.size());
        if(dlg.DoModal()!=IDOK)return;
        sldFiles_.clear();CString display;
        auto pos=dlg.GetStartPosition();while(pos){auto path=dlg.GetNextPathName(pos);sldFiles_.push_back(fs::path(path.GetString()));if(!display.IsEmpty())display+=L"; ";display+=path;}
        SetDlgItemText(IDC_SLD_PATH,display);
        SetDlgItemText(IDC_STATUS,L"선택한 SLD를 우선 적용합니다. MapServer 8.2 이상이 필요하며 레이어 이름 또는 중복 없는 데이터 파일명으로 연결합니다.");
    }
    afx_msg void InputHelp()
    {
        AfxMessageBox(L"입력 폴더: PC에서 검색할 원본 데이터 폴더\n"
            L"서버 데이터 루트: MapServer에서 같은 원본을 읽는 폴더입니다. 입력 폴더 기준 상대경로가 뒤에 붙습니다.\n"
            L"예: 입력=D:/map/shp/EAR_100M, 서버 루트=D:/map/shp/EAR_100M → LAP030.shp\n\n"
            L"서버 .map 경로: 생성한 mapserver.map을 서버에 배치할 경로\n"
            L"내부 WMS URL: MapCache가 MapServer에 접속하는 주소\n"
            L"외부 서비스 URL: C4IMap이 WMS/WFS에 접속하는 주소\n"
            L"외부 WMTS URL: C4IMap이 MapCache에 접속하는 주소(예: http://localhost/mapcache/wmts)\n"
            L"URL에는 ?map= 등 요청 인수를 넣지 않습니다.\n"
            L"서버 캐시 경로: MapCache가 생성 타일을 저장하는 서버 폴더\n"
            L"출력 폴더: 이번 MAP/XML/SLD 생성 결과를 저장할 PC 폴더\n\n"
            L"WFS ID 필드: 비워 두면 원본 변경 없이 레코드 ID를 자동 사용합니다.\n"
            L"SLD 생성 체크: 기본 SLD 또는 가져온 XML 기반 SLD를 자동 생성합니다.\n"
            L"기존 SLD 선택 시 해당 파일을 우선 적용합니다. XML 기반 SLD도 MAP에 자동 연결합니다(MapServer 8.2+).\n"
            L"생성한 wms/wfs/wmts-sources.xml 중 하나를 ogc-sources.xml로 복사해 사용하세요.",MB_ICONINFORMATION);
    }
    afx_msg void ClearSld(){sldFiles_.clear();SetDlgItemText(IDC_SLD_PATH,L"");SetDlgItemText(IDC_STATUS,L"기존 SLD 선택을 해제했습니다.");}
    afx_msg void Generate()
    {
        if(busy_)   return;
        try
        {
            if(Value(IDC_INPUT).IsEmpty()||fs::absolute(fs::path(Value(IDC_INPUT).GetString())).lexically_normal()!=scannedRoot_||Text(IDC_EXTENSIONS)!=scannedExtensions_||Checked(IDC_RECURSIVE)!=recursive_)
            throw 
                std::runtime_error("Input folder or scan options changed. Scan again before generating.");
            mm::Settings s;
            s.wms=Checked(IDC_WMS);
            s.wfs=Checked(IDC_WFS);
            s.wmts=Checked(IDC_WMTS);
            s.sld=Checked(IDC_SLD);
            s.mapEpsg=Text(IDC_MAP_EPSG);
            s.mapUnits=Text(IDC_UNITS);
            s.extent=Text(IDC_EXTENT);
            s.rgb="40 110 190"; // Default only; each layer keeps its explicitly applied color.
            s.serverRoot=Text(IDC_SERVER_ROOT);
            s.mapPath=Text(IDC_MAP_PATH);
            s.internalUrl=Text(IDC_INTERNAL);
            s.publicUrl=Text(IDC_PUBLIC);
            s.cache=Text(IDC_CACHE);
            s.wmtsUrl=Text(IDC_WMTS_URL);
            std::vector<mm::Layer> chosen;
            for(int i=0;i<list_.GetItemCount();++i)
                if(list_.GetCheck(i))
                    chosen.push_back(layers_[static_cast<size_t>(i)]);
            mm::ApplyOgcMetadata(chosen,copProfiles_);
            auto output=Value(IDC_OUTPUT);
            if(output.IsEmpty())
            throw 
                std::runtime_error("Choose an output folder.");
            CWaitCursor wait;
            bool externalSld=!sldFiles_.empty();
            mm::SldImport imported;
            if(externalSld)
            {
                imported=mm::ImportSld(sldFiles_,chosen);
                s.sld=false;
                if(!imported.unmatched.empty())
                {
                    CString detail;for(const auto& item:imported.unmatched)detail+=Wide(item)+L"\r\n";SetDlgItemText(IDC_RESULTS,detail);
                    if(AfxMessageBox(L"SLD와 데이터가 연결되지 않은 항목이 있습니다. 아래 목록을 확인하세요.\n연결된 항목만 SLD를 적용하고 " \
                        "나머지 데이터는 기본 스타일로 생성할까요?",MB_YESNO|MB_DEFBUTTON2|MB_ICONQUESTION)!=IDYES)
                        return;
                }
            }
            bool xmlSld=!externalSld&&s.sld&&!copProfiles_.empty();
            if(xmlSld&&copProfiles_.size()>1){
                bool standard=false;for(const auto& p:copProfiles_){tinyxml2::XMLDocument d;d.Parse(p.xml.c_str());if(d.RootElement()&&d.RootElement()->Attribute("StandardMap"))standard=true;}
                if(standard)throw std::runtime_error("여러 축척의 XML이 열려 있습니다. 스타일 편집기에서 사용할 문서를 선택하고 '선택 문서 SLD' 생성 후 '메인에 적용'을 눌러주세요.");
            }
            if(xmlSld)
                s.sld=false;
            auto artifacts=mm::Build(s,chosen);
            if(xmlSld)
            {
                auto selection=mm::SelectCop(copProfiles_,chosen);
                CString detail;
                for(const auto& entry:selection.unmatched)
                    detail+=Wide(entry)+L"\r\n";
                SetDlgItemText(IDC_RESULTS,detail);
                if(!selection.matched)
                {
                    AfxMessageBox(L"체크된 데이터와 연결되는 XML 레이어가 없습니다.\n데이터 폴더를 검색하고 해당 벡터 파일을 체크한 뒤 XML 화면의 " \
                        "대상 레이어 이름을 확인하세요.\nXML만 변환하려면 XML 화면의 'SLD만 생성'을 사용하세요.\n미연결 목록은 화면 하단에 표시했습니다.",MB_ICONINFORMATION);return;
                }
                if(!selection.unmatched.empty())
                {
                    CString question;question.Format(L"연결된 XML 레이어: %d개\n미연결 XML 레이어: %d개\n\n미연결 항목을 제외하고 생성할까요? 전체 목록은 " \
                        "화면 하단에서 확인할 수 있습니다.",static_cast<int>(selection.matched),static_cast<int>(selection.unmatched.size()));
                    if(AfxMessageBox(question,MB_YESNO|MB_DEFBUTTON2|MB_ICONQUESTION)!=IDYES)
                        return;
                }
                auto styleArtifacts=mm::BuildCop(selection.profiles,copOptions_);
                auto generatedImport=mm::ImportGeneratedSld(styleArtifacts,chosen);
                artifacts=mm::Build(s,chosen);
                artifacts.insert(artifacts.end(),generatedImport.files.begin(),generatedImport.files.end());
                if(!selection.unmatched.empty())
                {
                    std::string skipped="XML layers excluded after user confirmation:\r\n";
                    for(const auto& entry:selection.unmatched)
                        skipped+=entry+"\r\n";
                    artifacts.push_back({"xml-unmatched-layers.txt",skipped});
                }
                artifacts.insert(artifacts.end(),styleArtifacts.begin(),styleArtifacts.end());
                for(auto& artifact:artifacts)
                    if(artifact.name==L"generation-report.txt")
                        artifact.content+="\nSLD MODE: imported COP XML. See sld-conversion-report.txt. Generated SLD is automatically linked to the MAP (MapServer 8.2+).\n";
            }
            if(externalSld){
                artifacts.insert(artifacts.end(),imported.files.begin(),imported.files.end());
                std::string report="SLD applied from existing files. Requires MapServer 8.2+. Deploy this entire folder together.\r\nSLD colors " \
                    "take precedence over main RGB.\r\n";
                for(const auto& mapping:imported.mappings)
                    report+="MATCH: "+mapping+"\r\n";
                for(const auto& path:sldFiles_)
                    report+="SOURCE: "+path.u8string()+"\r\n";
                for(const auto& item:imported.unmatched)
                    report+=item+"\r\n";
                artifacts.push_back({"sld-application-report.txt",report});
            }
            auto folder=mm::SaveBatch(fs::absolute(fs::path(output.GetString())),artifacts);
            CString report=L"생성 완료: ";
            report+=folder.c_str();
            report+=L"\r\n";
            for(const auto& file:artifacts)
            {
                report+=(folder/file.name).c_str();report+=L"\r\n";
            }
            SetDlgItemText(IDC_RESULTS,report);
            SetDlgItemText(IDC_STATUS,L"완료. 아래 생성 파일 목록에서 경로를 확인하세요. 서버 배치는 README를 참고하세요.");
            AfxMessageBox(L"설정 파일 생성을 완료했습니다.\n선택한 출력 폴더 안에 새 mapMake-... 폴더를 만들었습니다.",MB_ICONINFORMATION);
        }
        catch(const std::exception& e)
        {
            CString details=Wide(e.what());
            CString display=details;
            display.Replace(L"\n",L"\r\n");SetDlgItemText(IDC_RESULTS,display);
            AfxMessageBox(details.GetLength()>800?L"생성 중 오류가 발생했습니다. 상세 내역은 메인 화면 하단 목록에서 스크롤하여 확인하세요.":details,MB_ICONERROR);
        }
    }
    void OnOK() override { Generate(); }
    void OnCancel() override 
    {
        cancel_=true;
        if(worker_.joinable())
            worker_.join();
        CDialogEx::OnCancel();
    }
    DECLARE_MESSAGE_MAP()
};
BEGIN_MESSAGE_MAP(MainDialog,CDialogEx)
 ON_BN_CLICKED(IDC_RASTER_TILES,&MainDialog::RasterTiles)
 ON_BN_CLICKED(IDC_INPUT_PICK,&MainDialog::InputFolder)
 ON_BN_CLICKED(IDC_OUTPUT_PICK,&MainDialog::OutputFolder)
 ON_BN_CLICKED(IDC_SCAN,&MainDialog::ScanFiles)
 ON_BN_CLICKED(IDC_STOP,&MainDialog::StopScan)
 ON_BN_CLICKED(IDC_ALL,&MainDialog::All)
 ON_BN_CLICKED(IDC_NONE,&MainDialog::None)
 ON_BN_CLICKED(IDC_APPLY,&MainDialog::Apply)
 ON_BN_CLICKED(IDC_WMTS,&MainDialog::Services)
 ON_BN_CLICKED(IDC_WFS,&MainDialog::Services)
 ON_BN_CLICKED(IDC_STYLE_STUDIO,&MainDialog::StyleStudio)
 ON_BN_CLICKED(IDC_INPUT_HELP,&MainDialog::InputHelp)
 ON_BN_CLICKED(IDC_SLD_PICK,&MainDialog::ChooseSld)
 ON_BN_CLICKED(IDC_SLD_CLEAR,&MainDialog::ClearSld)
 ON_BN_CLICKED(IDC_COLOR_PICK,&MainDialog::PickColor)
 ON_BN_CLICKED(IDC_COLOR_APPLY,&MainDialog::ApplyColor)
 ON_BN_CLICKED(IDC_XML_STYLE,&MainDialog::XmlStyles)
 ON_BN_CLICKED(IDC_GENERATE,&MainDialog::Generate)
 ON_NOTIFY(LVN_ITEMCHANGED,IDC_FILES,&MainDialog::SelectionChanged)
 ON_MESSAGE(ScanDone,&MainDialog::ScanComplete)
END_MESSAGE_MAP()
class MapMakeApp:public CWinApp {
    BOOL InitInstance() override {
        INITCOMMONCONTROLSEX controls{sizeof(INITCOMMONCONTROLSEX),ICC_LISTVIEW_CLASSES|ICC_STANDARD_CLASSES|ICC_TAB_CLASSES};
        InitCommonControlsEx(&controls);
        CWinApp::InitInstance();
        if(!AfxOleInit())
        {
            AfxMessageBox(L"폴더 선택 창을 위한 COM 초기화에 실패했습니다.");
            return FALSE;
        }
        AfxEnableControlContainer();
        MainDialog dialog;
        m_pMainWnd=&dialog;
        if(dialog.DoModal()==-1)
            AfxMessageBox(L"대화상자를 생성하지 못했습니다. 리소스와 MFC 설치를 확인하세요.");
        m_pMainWnd=nullptr;
        return FALSE;
    }
};
MapMakeApp theApp;
