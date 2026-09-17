#include "WorkspaceFrame.hpp"
#include "NeoViewState.hpp"
#include <neoshared/PathUtf8.hpp>
#include <wx/dnd.h>
#include <wx/menu.h>
#include <algorithm>
#include <cctype>
#include <utility>
namespace neotoolkit {
namespace {
enum { ID_Exit=wxID_HIGHEST+20000, ID_About, ID_Dark, ID_FontLarger, ID_FontSmaller, ID_FontReset, ID_ToggleExplorer, ID_Open };
class DropTarget final: public wxFileDropTarget {
public:
    explicit DropTarget(WorkspaceFrame& frame):frame_(frame){}
    bool OnDropFiles(wxCoord,wxCoord,const wxArrayString& files)override {
        for(const auto& name:files)frame_.openPath(neosettings::pathFromWx(name));
        return !files.empty();
    }
private:WorkspaceFrame& frame_;
};
void stripAccelerators(wxMenu& menu) {
    for(auto* item:menu.GetMenuItems()) {
        if(item->IsSeparator())continue;
        item->SetItemLabel(item->GetItemLabel().BeforeFirst('\t'));
        if(item->GetSubMenu())stripAccelerators(*item->GetSubMenu());
    }
}
}
std::vector<neomodules::Panel*> WorkspaceFrame::panels() const {return {tables_,soundsets_,talkTables_,structured_,dialogues_,journals_,archives_};}
WorkspaceFrame::WorkspaceFrame():wxFrame(nullptr,wxID_ANY,"NeoToolKit Test - Game Explorer") {
    static_assert(neomodules::kPanelApiVersion >= 2, "Update NeoShared: hosted output guard API required");
    splitter_=new wxSplitterWindow(this,wxID_ANY,wxDefaultPosition,wxDefaultSize,wxSP_LIVE_UPDATE);
    neomodules::Context browserContext;
    browserContext.embedded=true;browserContext.compact=true;
    browserContext.closeRequested=[this]{Close();};
    browserContext.titleChanged=[this](const wxString& title){SetTitle("NeoToolKit Test - "+title);};
    browser_=neobif::ui::createBrowserPanel(splitter_,std::move(browserContext));
    editors_=new wxNotebook(splitter_,wxID_ANY);
    editors_->SetName("NeoToolKit editors");
    neomodules::Context context;context.embedded=true;context.closeRequested=[this]{Close();};
    context.validateOutput=[this](const auto& path,const auto* owner){checkOutput(path,owner);};
    tables_=neo2da::ui::createEditorPanel(editors_,context);
    soundsets_=neossf::ui::createEditorPanel(editors_,context);
    talkTables_=neotlk::ui::createEditorPanel(editors_,context);
    structured_=neogff::ui::createEditorPanel(editors_,context);
    dialogues_=neodlg::ui::createEditorPanel(editors_,context);
    journals_=neojrl::ui::createEditorPanel(editors_,context);
    archives_=neoerf::ui::createEditorPanel(editors_,context);
    editors_->AddPage(tables_,"2DA tables",true);
    editors_->AddPage(soundsets_,"Soundsets");
    editors_->AddPage(talkTables_,"Talk tables");
    editors_->AddPage(structured_,"GFF resources");
    editors_->AddPage(dialogues_,"Dialogues");
    editors_->AddPage(journals_,"Journals");
    editors_->AddPage(archives_,"Archives");
    archives_->setMemberOpenHandler({[](std::uint16_t type){
        std::vector<neoerf::ui::MemberOpenTarget> result;
        for(const auto& choice:editorsForType(type))result.push_back({choice.id,choice.label});
        return result;
    },[this](neoshared::ResourceDocument resource,const std::string& editor){
        // A member is an independent snapshot. Its owning writable archive may
        // still be committed deliberately by NeoERF; member saves remain guarded.
        openSnapshot(std::move(resource),editor,false);
    }});
    browser_->setArchiveOpenHandler([this](const auto& path,auto inputs){
        if(archives_->openGameArchive(path,inputs)){
            for(const auto& input:inputs)if(std::find(protectedInputs_.begin(),protectedInputs_.end(),input)==protectedInputs_.end())protectedInputs_.push_back(input);
            selectEditor(static_cast<std::size_t>(EditorKind::Archives));
        }
    });
    static_assert(neobif::ui::kBrowserApiVersion>=3,"Update NeoBIF: explicit Open-with routing required");
    browser_->setOpenHandler({[](std::uint16_t type){return !editorsForType(type).empty();},
        [this](neoshared::ResourceDocument resource){openResource(std::move(resource));},
        [](std::uint16_t type){
            std::vector<neobif::ui::OpenTarget> result;
            for(const auto& choice:editorsForType(type))result.push_back({choice.id,choice.label});
            return result;
        },
        [this](neoshared::ResourceDocument resource,const std::string& editor){openResource(std::move(resource),editor);}});
    splitter_->SetMinimumPaneSize(FromDIP(260));
    splitter_->SplitVertically(browser_,editors_,FromDIP(420));splitter_->SetSashGravity(0.0);
    auto* layout=new wxBoxSizer(wxVERTICAL);layout->Add(splitter_,1,wxEXPAND);SetSizer(layout);
    buildMenus();
    editors_->Bind(wxEVT_NOTEBOOK_PAGE_CHANGED,[this](wxBookCtrlEvent& event){
        if(event.GetEventObject()==editors_ && event.GetSelection()>=0)switchMenus(static_cast<std::size_t>(event.GetSelection()));
        event.Skip();
    });
    Bind(wxEVT_MENU,[this](wxCommandEvent& event){
        auto list=panels();
        if(!neomodules::routeCommand({browser_,list.at(activeEditor_)},event))event.Skip();
    });
    Bind(wxEVT_MENU_OPEN,[this](wxMenuEvent& event){
        auto list=panels();neomodules::routeMenuOpen({browser_,list.at(activeEditor_)},event);event.Skip();
    });
    Bind(wxEVT_CLOSE_WINDOW,[this](wxCloseEvent& event){
        if(!browser_->canClose()) {if(event.CanVeto()){event.Veto();return;}}
        const auto list=panels();
        for(std::size_t i=0;i<list.size();++i) {
            selectEditor(i);
            if(!list[i]->canClose()){if(event.CanVeto()){event.Veto();return;}}
        }
        settings_.saveWindowPlacement(*this);event.Skip();
    });
    SetDropTarget(new DropTarget(*this));
    wxui::configureResponsiveWindow(*this,wxSize(1480,900),wxSize(1000,650));
    settings_.restoreWindowPlacement(*this);
    dark_=wxui::readDarkMode("NeoToolKit-Test");fontScale_=settings_.fontScale();applyAppearance();
}
WorkspaceFrame::~WorkspaceFrame() {
    // Destroy panels before either attached or inactive menus: game-menu helpers
    // unbind their handlers during the panel's destruction.
    menusReady_=false;
    DestroyChildren();browser_=nullptr;tables_=nullptr;soundsets_=nullptr;talkTables_=nullptr;structured_=nullptr;dialogues_=nullptr;journals_=nullptr;archives_=nullptr;
}
void WorkspaceFrame::buildMenus() {
    auto* bar=new wxMenuBar;
    auto* file=new wxMenu;
    file->Append(ID_Open,"Open resource...\tCtrl+O");
    auto source=browser_->takeMenus();
    auto* explorer=new wxMenu;
    while(source && source->GetMenuCount()) {
        auto label=source->GetMenuLabel(0);auto* menu=source->Remove(0);
        stripAccelerators(*menu); // active editor owns Ctrl+F/Save/etc.
        explorer->AppendSubMenu(menu,label);
    }
    file->AppendSeparator();file->Append(ID_Exit,"Exit\tCtrl+Q");
    bar->Append(file,"&File");bar->Append(explorer,"Game &Explorer");
    auto list=panels();
    for(std::size_t i=0;i<list.size();++i) {
        auto module=list[i]->takeMenus();
        while(module && module->GetMenuCount()) {
            auto label=module->GetMenuLabel(0);label.Replace("&","");
            if(label=="File") {
                static const char* titles[]={"Table","Soundset","Talk table","Structured resource","Conversation","Journal","Archive"};
                label=titles[i];
            }
            auto* menu=module->Remove(0);
            // All editors' own open remains available; host's Ctrl+O handles all types.
            for(auto* item:menu->GetMenuItems()) {
                auto text=item->GetItemLabel();
                if(text.EndsWith("\tCtrl+O") || text.EndsWith("\tCtrl-O"))item->SetItemLabel(text.BeforeFirst('\t')+"\tCtrl+Shift+O");
            }
            menus_[i].push_back({label,std::unique_ptr<wxMenu>(menu)});
        }
    }
    auto* workspace=new wxMenu;
    workspace->AppendCheckItem(ID_ToggleExplorer,"Show Game Explorer")->Check(true);
    workspace->AppendSeparator();workspace->AppendCheckItem(ID_Dark,"Dark mode");
    workspace->Append(ID_FontLarger,"Increase font size\tCtrl++");
    workspace->Append(ID_FontSmaller,"Decrease font size\tCtrl+-");
    workspace->Append(ID_FontReset,"Reset font size\tCtrl+0");
    workspace->AppendSeparator();workspace->Append(ID_About,"About NeoToolKit Test");
    bar->Append(workspace,"&Workspace");SetMenuBar(bar);menusReady_=true;switchMenus(0);
    Bind(wxEVT_MENU,[this](wxCommandEvent&){Close();},ID_Exit);
    Bind(wxEVT_MENU,[this](wxCommandEvent&){
        auto path=wxui::chooseOpenFile(this,"Open resource",
            "Resources (archives, KEY, tables, soundsets, GFF)|*.erf;*.ERF;*.mod;*.MOD;*.rim;*.RIM;*.sav;*.SAV;*.hak;*.HAK;*.nwm;*.NWM;*.crf;*.CRF;*.rimp;*.RIMP;*.key;*.KEY;*.2da;*.2DA;*.gda;*.GDA;*.ssf;*.SSF;*.tlk;*.TLK;*.dlg;*.DLG;*.jrl;*.JRL;*.gff;*.GFF;*.utc;*.UTC;*.uti;*.UTI;*.utm;*.UTM;*.utp;*.UTP;*.utd;*.UTD;*.ute;*.UTE;*.uts;*.UTS;*.utt;*.UTT;*.utw;*.UTW;*.are;*.ARE;*.git;*.GIT;*.ifo;*.IFO|All files (*.*)|*.*");
        if(path)openPath(*path);
    },ID_Open);
    Bind(wxEVT_MENU,[this](wxCommandEvent&){wxMessageBox(
        "NeoToolKit Test " NEOTOOLKIT_VERSION "\n\nNeoBIF browser with Neo2DA, NeoSSF, NeoTLK, NeoGFF, NeoDLG, NeoJRL and NeoERF panels.\n"
        "Archive members are editable snapshots. Save As creates working files, not archive modifications.\n\n"
        "Standalone archives open in the integrated NeoERF editor.","About NeoToolKit Test",wxOK|wxICON_INFORMATION,this);},ID_About);
    Bind(wxEVT_MENU,[this](wxCommandEvent& event){dark_=event.IsChecked();wxui::writeDarkMode("NeoToolKit-Test",dark_);applyAppearance();},ID_Dark);
    Bind(wxEVT_MENU,[this](wxCommandEvent&){fontScale_=neoview::steppedFontScale(fontScale_,1);settings_.setFontScale(fontScale_);applyAppearance();},ID_FontLarger);
    Bind(wxEVT_MENU,[this](wxCommandEvent&){fontScale_=neoview::steppedFontScale(fontScale_,-1);settings_.setFontScale(fontScale_);applyAppearance();},ID_FontSmaller);
    Bind(wxEVT_MENU,[this](wxCommandEvent&){fontScale_=neoview::kDefaultFontScale;settings_.setFontScale(fontScale_);applyAppearance();},ID_FontReset);
    Bind(wxEVT_MENU,[this](wxCommandEvent& event){
        if(event.IsChecked()) {if(!splitter_->IsSplit()){browser_->Show();splitter_->SplitVertically(browser_,editors_,FromDIP(420));}}
        else if(splitter_->IsSplit()){splitter_->Unsplit(browser_);browser_->Hide();}
        Layout();
    },ID_ToggleExplorer);
}
void WorkspaceFrame::switchMenus(std::size_t index) {
    if(!menusReady_ || index>=menus_.size())return;
    auto* bar=GetMenuBar();
    for(std::size_t i=0;i<attachedMenuCount_;++i)menus_[activeEditor_][i].menu.reset(bar->Remove(2));
    activeEditor_=index;
    for(std::size_t i=0;i<menus_[index].size();++i)bar->Insert(2+i,menus_[index][i].menu.release(),menus_[index][i].title);
    attachedMenuCount_=menus_[index].size();bar->Refresh();
}
void WorkspaceFrame::selectEditor(std::size_t index) {
    if(index>=kEditorCount)return;
    editors_->ChangeSelection(static_cast<int>(index));switchMenus(index);
}
void WorkspaceFrame::applyAppearance() {
    wxui::applyTheme(this,dark_);browser_->setAppearance(dark_,fontScale_);
    for(auto* panel:panels())panel->setAppearance(dark_,fontScale_);
    GetMenuBar()->Check(ID_Dark,dark_);
}
void WorkspaceFrame::checkOutput(const std::filesystem::path& path,const neomodules::Panel* owner) const {
    neoshared::checkResourceOutput(path,protectedInputs_);
    neoshared::checkResourceOutput(path,browser_->sourcePaths());
    for(auto* panel:panels()) if(panel && panel!=owner)
        for(const auto& open:panel->openPaths()) if(neoshared::sameResourcePath(path,open))
            throw std::runtime_error("That destination is open in another editor. Save a different working copy first.");
}
bool WorkspaceFrame::openResource(neoshared::ResourceDocument resource,const std::string& editor) {
    return openSnapshot(std::move(resource),editor,true);
}
bool WorkspaceFrame::openSnapshot(neoshared::ResourceDocument resource,const std::string& editor,bool protectSession) {
    const auto inputs=resource.protectedInputs;
    const auto selected=selectResourceEditor(editorsForType(resource.type),editor);
    bool ok=false;
    switch(selected) {
    case EditorKind::Tables:ok=tables_->openResource(std::move(resource));break;
    case EditorKind::Soundsets:ok=soundsets_->openResource(std::move(resource));break;
    case EditorKind::TalkTables:ok=talkTables_->openResource(std::move(resource));break;
    case EditorKind::Gff:ok=structured_->openResource(std::move(resource));break;
    case EditorKind::Dialogues:ok=dialogues_->openResource(std::move(resource));break;
    case EditorKind::Journals:ok=journals_->openResource(std::move(resource));break;
    case EditorKind::Archives:ok=archives_->openResource(std::move(resource));break;
    }
    if(ok) {
        if(protectSession) for(const auto& input:inputs) if(std::find(protectedInputs_.begin(),protectedInputs_.end(),input)==protectedInputs_.end())protectedInputs_.push_back(input);
        selectEditor(static_cast<std::size_t>(selected));
    }
    return ok;
}
void WorkspaceFrame::openPath(const std::filesystem::path& path,const std::string& editor) {
    try {
        auto ext=neoshared::pathToUtf8(path.extension());std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return char(std::tolower(c));});
        std::error_code ec;
        if(std::filesystem::is_directory(path,ec)||ext==".key") {
            if(!editor.empty())throw std::runtime_error("Choose a resource inside the game browser, not an editor for a KEY/directory.");
            browser_->openPath(path);return;
        }
        const auto choices=editorsForExtension(ext);
        if(choices.empty())throw std::runtime_error("Open a game directory, chitin.key, ERF/RIM-family archive, table, soundset, talk table, or GFF-backed resource.");
        const auto selected=selectResourceEditor(choices,editor);
        bool ok=false;
        switch(selected) {
        case EditorKind::Tables:ok=tables_->openFile(path);break;
        case EditorKind::Soundsets:ok=soundsets_->openFile(path);break;
        case EditorKind::TalkTables:ok=talkTables_->openFile(path);break;
        case EditorKind::Gff:ok=structured_->openFile(path);break;
        case EditorKind::Dialogues:ok=dialogues_->openFile(path);break;
        case EditorKind::Journals:ok=journals_->openFile(path);break;
        case EditorKind::Archives: {
            auto sources=browser_->sourcePaths();sources.insert(sources.end(),protectedInputs_.begin(),protectedInputs_.end());
            const bool gameSource=std::any_of(sources.begin(),sources.end(),[&](const auto& input){return neoshared::sameResourcePath(path,input);});
            ok=gameSource?archives_->openGameArchive(path,std::move(sources)):archives_->openFile(path);break;
        }
        }
        if(ok)selectEditor(static_cast<std::size_t>(selected));
    }catch(const std::exception& ex){wxui::showError(this,ex);}
}
}
