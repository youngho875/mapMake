#include <afxwin.h>
#include <afxdlgs.h>
#include <afxdialogex.h>
#include <afxcmn.h>
#include "CopDialog.h"
#include "resource.h"
#include <algorithm>
#include <stdexcept>
namespace 
{
CString Wide(const std::string& s)
{
    int n=MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0);
    CString r;
    if(n)
    {
        MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),r.GetBuffer(n),n);
        r.ReleaseBuffer(n);
    }
    return r;
}
std::string Utf8(const CString& s)
{
    int n=WideCharToMultiByte(CP_UTF8,0,s,s.GetLength(),nullptr,0,nullptr,nullptr);
    std::string r(n,'\0');
    if(n)WideCharToMultiByte(CP_UTF8,0,s,s.GetLength(),&r[0],n,nullptr,nullptr);
    return r;
}
class CopDialog:public CDialogEx
{
    CListCtrl list_;
    const std::vector<mm::Layer>& data_;
    CString output_;
    std::vector<std::pair<size_t,size_t>> rows_;
    CString Value(int id)
    {
        CString s;GetDlgItemText(id,s);
        s.Trim();return s;
    }
    void Sync()
    {
        for(int i=0;i<list_.GetItemCount();++i)
        {
            auto index=rows_[static_cast<size_t>(i)];
            profiles[index.first].layers[index.second].enabled=list_.GetCheck(i)!=FALSE;
        }
    }
    void Options()
    {
        options.gssRoot=std::filesystem::path(Value(IDC_COP_GSS).GetString());
        options.symbolBaseUrl=Utf8(Value(IDC_COP_URL));
        options.transparency=static_cast<CComboBox*>(GetDlgItem(IDC_COP_ALPHA))->GetCurSel();
    }
    void Refresh()
    {
        list_.SetRedraw(FALSE);
        list_.DeleteAllItems();
        rows_.clear();
        for(size_t p=0;p<profiles.size();++p)
            for(size_t l=0;l<profiles[p].layers.size();++l)
            {
            auto& layer=profiles[p].layers[l];
            int row=list_.InsertItem(static_cast<int>(rows_.size()),Wide(profiles[p].file.filename().u8string()));
            rows_.emplace_back(p,l);
            list_.SetItemText(row,1,Wide(layer.alias));
            list_.SetItemText(row,2,Wide(layer.source));
            list_.SetItemText(row,3,Wide(layer.target));
            list_.SetItemText(row,4,Wide(layer.style));
            list_.SetItemText(row,5,Wide(layer.scale));
            list_.SetItemText(row,6,layer.visible?L"TRUE":L"FALSE");
            const bool connected=std::any_of(data_.begin(),data_.end(),[&](const mm::Layer& d){return d.name==layer.target&&d.type!="RASTER";});
            list_.SetItemText(row,7,connected?L"연결됨":L"미연결");
            list_.SetItemText(row,8,layer.rgb.empty()?L"XML 원본":Wide(layer.rgb));
            list_.SetCheck(row,layer.enabled);
        }
        list_.SetRedraw(TRUE);
        list_.Invalidate();CString text;
        text.Format(L"XML %d개 / 레이어 %d개. 원본 Visible=TRUE 항목이 기본 체크됩니다.",static_cast<int>(profiles.size()),static_cast<int>(rows_.size()));
        SetDlgItemText(IDC_COP_STATUS,text);
    }
public:
    std::vector<mm::CopProfile> profiles;
    mm::CopOptions options;
    CopDialog(CWnd* parent,const std::vector<mm::CopProfile>& p,const mm::CopOptions& o,const std::vector<mm::Layer>& d,const CString& output):CDialogEx(IDD_COP,parent),data_(d),output_(output),profiles(p),options(o){}
    BOOL OnInitDialog() override 
    {
        CDialogEx::OnInitDialog();
        list_.SubclassDlgItem(IDC_COP_LIST,this);list_.SetExtendedStyle(LVS_EX_CHECKBOXES|LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES|LVS_EX_DOUBLEBUFFER);
        const wchar_t* heads[]={L"XML 파일",L"레이어",L"Source",L"대상 .map 레이어",L"스타일 / 테이블",L"축척",L"원본 표시",L"연결 상태",L"RGB"};
        int widths[]={170,145,170,180,200,150,75,100,110};
        for(int i=0;i<9;++i)
            list_.InsertColumn(i,heads[i],LVCFMT_LEFT,widths[i]);
        SetDlgItemText(IDC_COP_GSS,options.gssRoot.c_str());
        SetDlgItemText(IDC_COP_URL,Wide(options.symbolBaseUrl));
        SetDlgItemText(IDC_COP_OUTPUT,output_);
        auto combo=static_cast<CComboBox*>(GetDlgItem(IDC_COP_ALPHA));
        combo->AddString(L"원본 배경 유지");combo->AddString(L"자홍색(FF00FF) 투명");combo->AddString(L"흰색(FFFFFF) 투명");
        combo->SetCurSel(options.transparency);
        for(auto& profile:profiles)mm::MatchCop(profile,data_);
        Refresh();
        return TRUE;
    }
    afx_msg void Add()
    {
        CFileDialog dlg(TRUE,L"xml",nullptr,OFN_FILEMUSTEXIST|OFN_ALLOWMULTISELECT|OFN_EXPLORER,L"COP XML (*.xml)|*.xml|All files (*.*)|*.*||",this);
        std::vector<wchar_t> buffer(65536,L'\0');
        dlg.m_ofn.lpstrFile=buffer.data();
        dlg.m_ofn.nMaxFile=static_cast<DWORD>(buffer.size());
        if(dlg.DoModal()!=IDOK)
            return;Sync();
        CWaitCursor wait;
        CString errors;
        POSITION position=dlg.GetStartPosition();
        while(position)
        {
            auto path=std::filesystem::path(dlg.GetNextPathName(position).GetString());
            if(std::any_of(profiles.begin(),profiles.end(),[&](const mm::CopProfile& p){return p.file==path;})) 
                continue;
            try
            {
                auto profile=mm::LoadCop(path);
                mm::MatchCop(profile,data_);
                profiles.push_back(std::move(profile));
            }
            catch(const std::exception& e)
            {
                errors+=Wide(path.filename().u8string()+": "+e.what())+L"\r\n";
            }
        }
        Refresh();
        if(!errors.IsEmpty())
            AfxMessageBox(errors,MB_ICONERROR);
    }
    afx_msg void Clear(){ profiles.clear(); Refresh(); }
    afx_msg void All()
    { 
        for(int i=0;i<list_.GetItemCount();++i)
            list_.SetCheck(i,TRUE);
    }
    afx_msg void None()
    {
        for(int i=0;i<list_.GetItemCount();++i)
            list_.SetCheck(i,FALSE);
    }
    afx_msg void Visible()
    {
        for(int i=0;i<list_.GetItemCount();++i)
        {
            auto ix=rows_[static_cast<size_t>(i)];
            list_.SetCheck(i,profiles[ix.first].layers[ix.second].visible);
        }
    }
    afx_msg void Match()
    {
        Sync();
        for(auto& p:profiles)
            mm::MatchCop(p,data_);
        Refresh();SetDlgItemText(IDC_COP_STATUS,L"파일 이름과 XML Source가 유일하게 일치하는 항목을 연결했습니다. 나머지는 Source 이름을 유지합니다.");
    }
    afx_msg void Target()
    {
        auto target=Utf8(Value(IDC_COP_TARGET));
        if(target.empty())
        {
            AfxMessageBox(L"대상 레이어 이름을 입력하세요.");
            return;
        }
        POSITION p=list_.GetFirstSelectedItemPosition();
        if(!p)
        {
            AfxMessageBox(L"대상 이름을 바꿀 행을 먼저 선택하세요.");
            return;
        }
        Sync();
        while(p)
        {
            int i=list_.GetNextSelectedItem(p);
            auto ix=rows_[static_cast<size_t>(i)];
            profiles[ix.first].layers[ix.second].target=target;
            profiles[ix.first].layers[ix.second].manualTarget=true;
        }
        Refresh();
    }
    afx_msg void Selection(NMHDR* header,LRESULT* result)
    {
        *result=0;
        auto n=reinterpret_cast<NMLISTVIEW*>(header);
        if((n->uNewState&LVIS_SELECTED)&&!(n->uOldState&LVIS_SELECTED)&&n->iItem>=0&&static_cast<size_t>(n->iItem)<rows_.size())
        {
            SetDlgItemText(IDC_COP_TARGET,list_.GetItemText(n->iItem,3));
            auto ix=rows_[static_cast<size_t>(n->iItem)];SetDlgItemText(IDC_COP_RGB,Wide(profiles[ix.first].layers[ix.second].rgb));
        }
    }
    void SetColors(bool reset){
        try{
            auto rgb=reset?std::string{}:mm::NormalizeRgb(Utf8(Value(IDC_COP_RGB)));
            POSITION p=list_.GetFirstSelectedItemPosition();if(!p){AfxMessageBox(L"색상을 바꿀 XML 레이어 행을 선택하세요.");return;}Sync();
            while(p){int i=list_.GetNextSelectedItem(p);auto ix=rows_[static_cast<size_t>(i)];profiles[ix.first].layers[ix.second].rgb=rgb;}
            Refresh();SetDlgItemText(IDC_COP_STATUS,L"선택 레이어의 기본 벡터 색상을 변경했습니다. 이미지 심볼과 라벨 색상은 유지됩니다.");
        }catch(const std::exception& e){AfxMessageBox(Wide(e.what()),MB_ICONERROR);}
    }
    afx_msg void ApplyColor(){SetColors(false);}
    afx_msg void ResetColor(){SetColors(true);}
    afx_msg void Gss()
    {
        CFolderPickerDialog d(Value(IDC_COP_GSS),0,this);
        if(d.DoModal()==IDOK)
            SetDlgItemText(IDC_COP_GSS,d.GetPathName());
    }
    afx_msg void Output()
    {
        CFolderPickerDialog d(Value(IDC_COP_OUTPUT),0,this);
        if(d.DoModal()==IDOK)
            SetDlgItemText(IDC_COP_OUTPUT,d.GetPathName());
    }
    afx_msg void Export()
    {
        try{
            Sync();
            Options();
            auto folder=Value(IDC_COP_OUTPUT);
            if(folder.IsEmpty())
            throw 
                std::runtime_error("Choose an output folder");
            CWaitCursor wait;
            auto artifacts=mm::BuildCop(profiles,options);
            auto result=mm::SaveBatch(std::filesystem::path(folder.GetString()),artifacts);
            CString message=L"SLD 생성 완료\r\n";message+=result.c_str();
            message+=L"\r\n";
            int png=0;
            for(const auto& a:artifacts)
                if(a.name.extension()==L".png")
                    ++png;
                else
                {
                    message+=(result/a.name).c_str();
                    message+=L"\r\n";
                }
            CString count;
            count.Format(L"PNG 심볼 %d개 (symbols 폴더)\r\n변환 보고서에서 누락·근사 표현을 확인하세요.",png);
            message+=count;SetDlgItemText(IDC_COP_RESULTS,message);
        }catch(const std::exception& e)
        {
            AfxMessageBox(Wide(e.what()),MB_ICONERROR);
        }
    }
    void OnOK() override 
    {
        Sync();
        Options();
        CDialogEx::OnOK();
    }
    DECLARE_MESSAGE_MAP()
};

BEGIN_MESSAGE_MAP(CopDialog,CDialogEx)
 ON_BN_CLICKED(IDC_COP_COLOR_APPLY,&CopDialog::ApplyColor)
 ON_BN_CLICKED(IDC_COP_COLOR_RESET,&CopDialog::ResetColor)
 ON_BN_CLICKED(IDC_COP_ADD,&CopDialog::Add)
 ON_BN_CLICKED(IDC_COP_CLEAR,&CopDialog::Clear)
 ON_BN_CLICKED(IDC_COP_ALL,&CopDialog::All)
 ON_BN_CLICKED(IDC_COP_NONE,&CopDialog::None)
 ON_BN_CLICKED(IDC_COP_VISIBLE,&CopDialog::Visible)
 ON_BN_CLICKED(IDC_COP_MATCH,&CopDialog::Match)
 ON_BN_CLICKED(IDC_COP_APPLY,&CopDialog::Target)
 ON_BN_CLICKED(IDC_COP_GSS_PICK,&CopDialog::Gss)
 ON_BN_CLICKED(IDC_COP_OUTPUT_PICK,&CopDialog::Output)
 ON_BN_CLICKED(IDC_COP_EXPORT,&CopDialog::Export)
 ON_NOTIFY(LVN_ITEMCHANGED,IDC_COP_LIST,&CopDialog::Selection)
END_MESSAGE_MAP()
}
bool ConfigureCop(CWnd* parent,std::vector<mm::CopProfile>& profiles,mm::CopOptions& options,const std::vector<mm::Layer>& layers,const CString& outputFolder)
{
    CopDialog d(parent,profiles,options,layers,outputFolder);
    if(d.DoModal()!=IDOK)
        return false;
    profiles=std::move(d.profiles);
    options=std::move(d.options);

    return true;
}
