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
constexpr const char* kEditorPageTitles[kEditorCount]={
    "2DA tables","Soundsets","Talk tables","GFF resources",
    "Dialogues","Journals","Textures","Archives"
};
}
std::vector<neomodules::Panel*> WorkspaceFrame::panels() const {return {tables_,soundsets_,talkTables_,structured_,dialogues_,journals_,textures_,archives_};}
WorkspaceFrame::WorkspaceFrame():wxFrame(nullptr,wxID_ANY,"NeoToolKit Test - Game Explorer") {
    static_assert(neomodules::kPanelApiVersion >= 2, "Update NeoShared: hosted output guard API required");
    splitter_=new wxSplitterWindow(this,wxID_ANY,wxDefaultPosition,wxDefaultSize,wxSP_LIVE_UPDATE);
    neomodules::Context browserContext;
    browserContext.embedded=true;browserContext.compact=false;
    browserContext.closeRequested=[this]{Close();};
    browserContext.titleChanged=[this](const wxString& title){SetTitle("NeoToolKit Test - "+title);};
    browser_=neobif::ui::createBrowserPanel(splitter_,std::move(browserContext));
    editors_=new wxAuiNotebook(splitter_,wxID_ANY,wxDefaultPosition,wxDefaultSize,
        wxAUI_NB_TOP|wxAUI_NB_TAB_MOVE|wxAUI_NB_CLOSE_ON_ACTIVE_TAB|wxAUI_NB_SCROLL_BUTTONS);
    editors_->SetName("NeoToolKit editors");
    editors_->Hide();
    browser_->setArchiveOpenHandler([this](const auto& path,auto inputs){
        ensureEditor(EditorKind::Archives);
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
    splitter_->Initialize(browser_);
    auto* layout=new wxBoxSizer(wxVERTICAL);layout->Add(splitter_,1,wxEXPAND);SetSizer(layout);
    buildMenus();
    editors_->Bind(wxEVT_AUINOTEBOOK_PAGE_CHANGED,[this](wxAuiNotebookEvent& event){
        if(event.GetEventObject()==editors_ && event.GetSelection()>=0) {
            const auto index=editorForPage(event.GetSelection());
            if(index<kEditorCount)switchMenus(index);
        }
        event.Skip();
    });
    editors_->Bind(wxEVT_AUINOTEBOOK_PAGE_CLOSE,[this](wxAuiNotebookEvent& event){
        event.Veto();
        closeEditorPage(event.GetSelection());
    });
    Bind(wxEVT_MENU,[this](wxCommandEvent& event){
        std::vector<neomodules::Panel*> targets{browser_};
        if(activeEditor_<kEditorCount)targets.push_back(panelFor(static_cast<EditorKind>(activeEditor_)));
        if(!neomodules::routeCommand(targets,event))event.Skip();
    });
    Bind(wxEVT_MENU_OPEN,[this](wxMenuEvent& event){
        std::vector<neomodules::Panel*> targets{browser_};
        if(activeEditor_<kEditorCount)targets.push_back(panelFor(static_cast<EditorKind>(activeEditor_)));
        neomodules::routeMenuOpen(targets,event);event.Skip();
    });
    Bind(wxEVT_CLOSE_WINDOW,[this](wxCloseEvent& event){
        if(!browser_->canClose()) {if(event.CanVeto()){event.Veto();return;}}
        const auto list=panels();
        for(std::size_t i=0;i<list.size();++i) {
            if(list[i] && !list[i]->canClose()){
                selectEditor(i);
                if(event.CanVeto()){event.Veto();return;}
            }
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
    DestroyChildren();browser_=nullptr;tables_=nullptr;soundsets_=nullptr;talkTables_=nullptr;structured_=nullptr;dialogues_=nullptr;journals_=nullptr;textures_=nullptr;archives_=nullptr;
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
    auto* workspace=new wxMenu;
    workspace->AppendCheckItem(ID_ToggleExplorer,"Show Game Explorer")->Check(true);
    workspace->AppendSeparator();workspace->AppendCheckItem(ID_Dark,"Dark mode");
    workspace->Append(ID_FontLarger,"Increase font size\tCtrl++");
    workspace->Append(ID_FontSmaller,"Decrease font size\tCtrl+-");
    workspace->Append(ID_FontReset,"Reset font size\tCtrl+0");
    workspace->AppendSeparator();workspace->Append(ID_About,"About NeoToolKit Test");
    bar->Append(workspace,"&Workspace");SetMenuBar(bar);menusReady_=true;
    bar->Enable(ID_ToggleExplorer,false);
    for(std::size_t i=0;i<kEditorCount;++i)if(panels()[i])captureEditorMenus(i);
    Bind(wxEVT_MENU,[this](wxCommandEvent&){Close();},ID_Exit);
    Bind(wxEVT_MENU,[this](wxCommandEvent&){
        auto path=wxui::chooseOpenFile(this,"Open resource",
            "Resources (archives, KEY, tables, textures, soundsets, GFF)|*.erf;*.ERF;*.mod;*.MOD;*.rim;*.RIM;*.sav;*.SAV;*.hak;*.HAK;*.nwm;*.NWM;*.crf;*.CRF;*.rimp;*.RIMP;*.key;*.KEY;*.2da;*.2DA;*.gda;*.GDA;*.tpc;*.TPC;*.txb;*.TXB;*.tga;*.TGA;*.dds;*.DDS;*.png;*.PNG;*.jpg;*.JPG;*.jpeg;*.JPEG;*.jpe;*.JPE;*.bmp;*.BMP;*.txi;*.TXI;*.ssf;*.SSF;*.tlk;*.TLK;*.dlg;*.DLG;*.jrl;*.JRL;*.gff;*.GFF;*.utc;*.UTC;*.uti;*.UTI;*.utm;*.UTM;*.utp;*.UTP;*.utd;*.UTD;*.ute;*.UTE;*.uts;*.UTS;*.utt;*.UTT;*.utw;*.UTW;*.are;*.ARE;*.git;*.GIT;*.ifo;*.IFO|All files (*.*)|*.*");
        if(path)openPath(*path);
    },ID_Open);
    Bind(wxEVT_MENU,[this](wxCommandEvent&){wxMessageBox(
        "NeoToolKit Test " NEOTOOLKIT_VERSION "\n\nNeoBIF browser with Neo2DA, NeoSSF, NeoTLK, NeoGFF, NeoDLG, NeoJRL, NeoTPC and NeoERF panels.",
        "About NeoToolKit Test",wxOK|wxICON_INFORMATION,this);},ID_About);
    Bind(wxEVT_MENU,[this](wxCommandEvent& event){dark_=event.IsChecked();wxui::writeDarkMode("NeoToolKit-Test",dark_);applyAppearance();},ID_Dark);
    Bind(wxEVT_MENU,[this](wxCommandEvent&){fontScale_=neoview::steppedFontScale(fontScale_,1);settings_.setFontScale(fontScale_);applyAppearance();},ID_FontLarger);
    Bind(wxEVT_MENU,[this](wxCommandEvent&){fontScale_=neoview::steppedFontScale(fontScale_,-1);settings_.setFontScale(fontScale_);applyAppearance();},ID_FontSmaller);
    Bind(wxEVT_MENU,[this](wxCommandEvent&){fontScale_=neoview::kDefaultFontScale;settings_.setFontScale(fontScale_);applyAppearance();},ID_FontReset);
    Bind(wxEVT_MENU,[this](wxCommandEvent& event){setExplorerVisible(event.IsChecked());},ID_ToggleExplorer);
}
void WorkspaceFrame::captureEditorMenus(std::size_t index) {
    if(index>=kEditorCount)return;
    menus_[index].clear();hiddenMenus_[index].clear();
    const auto list=panels();
    if(!list[index])return;
    auto module=list[index]->takeMenus();
    while(module && module->GetMenuCount()) {
        auto label=module->GetMenuLabel(0);label.Replace("&","");
        auto* menu=module->Remove(0);
        // The host owns About/help presentation; editor-specific Help menus are
        // intentionally omitted while an editor workspace is active.
        if(label.CmpNoCase("Help")==0) {hiddenMenus_[index].emplace_back(menu);continue;}
        if(label=="File") {
            static const char* titles[]={"Table","Soundset","Talk table","Structured resource","Conversation","Journal","Texture","Archive"};
            label=titles[index];
        }
        // All editors' own open remains available; host's Ctrl+O handles all types.
        for(auto* item:menu->GetMenuItems()) {
            auto text=item->GetItemLabel();
            if(text.EndsWith("\tCtrl+O") || text.EndsWith("\tCtrl-O"))item->SetItemLabel(text.BeforeFirst('\t')+"\tCtrl+Shift+O");
        }
        menus_[index].push_back({label,std::unique_ptr<wxMenu>(menu)});
    }
}
void WorkspaceFrame::detachActiveMenus() {
    if(activeEditor_>=kEditorCount) {attachedMenuCount_=0;return;}
    auto* bar=GetMenuBar();
    if(bar) {
        for(std::size_t i=0;i<attachedMenuCount_;++i) {
            auto* menu=bar->Remove(2);
            if(i<menus_[activeEditor_].size())menus_[activeEditor_][i].menu.reset(menu);
            else delete menu;
        }
        bar->Refresh();
    }
    activeEditor_=kEditorCount;attachedMenuCount_=0;
}
void WorkspaceFrame::switchMenus(std::size_t index) {
    if(!menusReady_ || index>=menus_.size() || index==activeEditor_)return;
    detachActiveMenus();
    auto* bar=GetMenuBar();
    activeEditor_=index;
    for(std::size_t i=0;i<menus_[index].size();++i)bar->Insert(2+i,menus_[index][i].menu.release(),menus_[index][i].title);
    attachedMenuCount_=menus_[index].size();bar->Refresh();
}
neomodules::Panel* WorkspaceFrame::ensureEditor(EditorKind kind) {
    if(auto* panel=panelFor(kind))return panel;
    neomodules::Context context;context.embedded=true;context.closeRequested=[this]{Close();};
    context.validateOutput=[this](const auto& path,const auto* owner){checkOutput(path,owner);};
    neomodules::Panel* panel=nullptr;
    switch(kind) {
    case EditorKind::Tables:tables_=neo2da::ui::createEditorPanel(editors_,context);panel=tables_;break;
    case EditorKind::Soundsets:soundsets_=neossf::ui::createEditorPanel(editors_,context);panel=soundsets_;break;
    case EditorKind::TalkTables:talkTables_=neotlk::ui::createEditorPanel(editors_,context);panel=talkTables_;break;
    case EditorKind::Gff:structured_=neogff::ui::createEditorPanel(editors_,context);panel=structured_;break;
    case EditorKind::Dialogues:dialogues_=neodlg::ui::createEditorPanel(editors_,context);panel=dialogues_;break;
    case EditorKind::Journals:journals_=neojrl::ui::createEditorPanel(editors_,context);panel=journals_;break;
    case EditorKind::Textures:textures_=neotpc::ui::createEditorPanel(editors_,context);panel=textures_;break;
    case EditorKind::Archives:
        archives_=neoerf::ui::createEditorPanel(editors_,context);panel=archives_;
        archives_->setMemberOpenHandler({[](std::uint16_t type){
            std::vector<neoerf::ui::MemberOpenTarget> result;
            for(const auto& choice:editorsForType(type))result.push_back({choice.id,choice.label});
            return result;
        },[this](neoshared::ResourceDocument resource,const std::string& editor){
            // A member is an independent snapshot. Its owning writable archive may
            // still be committed deliberately by NeoERF; member saves remain guarded.
            openSnapshot(std::move(resource),editor,false);
        }});
        break;
    }
    panel->Hide();panel->setAppearance(dark_,fontScale_);
    if(menusReady_)captureEditorMenus(static_cast<std::size_t>(kind));
    return panel;
}
void WorkspaceFrame::destroyEditor(EditorKind kind) {
    const auto index=static_cast<std::size_t>(kind);
    auto* panel=panelFor(kind);if(!panel)return;
    if(activeEditor_==index)detachActiveMenus();
    // Menu helpers retain references to their menu roots. Keep both visible and
    // suppressed menus alive until the panel's derived destructor has run.
    auto retiredMenus=std::make_shared<std::vector<Menu>>(std::move(menus_[index]));
    auto retiredHiddenMenus=std::make_shared<std::vector<std::unique_ptr<wxMenu>>>(std::move(hiddenMenus_[index]));
    panel->Bind(wxEVT_DESTROY,[panel,retiredMenus,retiredHiddenMenus](wxWindowDestroyEvent& event){
        if(event.GetEventObject()==panel) {retiredMenus->clear();retiredHiddenMenus->clear();}
        event.Skip();
    });
    switch(kind) {
    case EditorKind::Tables:tables_=nullptr;break;
    case EditorKind::Soundsets:soundsets_=nullptr;break;
    case EditorKind::TalkTables:talkTables_=nullptr;break;
    case EditorKind::Gff:structured_=nullptr;break;
    case EditorKind::Dialogues:dialogues_=nullptr;break;
    case EditorKind::Journals:journals_=nullptr;break;
    case EditorKind::Textures:textures_=nullptr;break;
    case EditorKind::Archives:archives_=nullptr;break;
    }
    panel->Hide();panel->Destroy();
}
neomodules::Panel* WorkspaceFrame::panelFor(EditorKind kind) const {
    return panels().at(static_cast<std::size_t>(kind));
}
int WorkspaceFrame::editorPage(EditorKind kind) const {
    const auto* panel=panelFor(kind);if(!panel)return wxNOT_FOUND;
    for(std::size_t i=0;i<editors_->GetPageCount();++i)
        if(editors_->GetPage(i)==panel)return static_cast<int>(i);
    return wxNOT_FOUND;
}
std::size_t WorkspaceFrame::editorForPage(int page) const {
    if(page<0 || static_cast<std::size_t>(page)>=editors_->GetPageCount())return kEditorCount;
    const auto* selected=editors_->GetPage(static_cast<std::size_t>(page));
    const auto list=panels();
    for(std::size_t i=0;i<list.size();++i)if(list[i]==selected)return i;
    return kEditorCount;
}
void WorkspaceFrame::showEditor(EditorKind kind) {
    const auto index=static_cast<std::size_t>(kind);
    auto* panel=ensureEditor(kind);
    auto page=editorPage(kind);
    if(page==wxNOT_FOUND) {
        panel->Show();
        if(!editors_->AddPage(panel,wxString::FromUTF8(kEditorPageTitles[index]),false)) {panel->Hide();return;}
        page=editorPage(kind);
    }
    editors_->Show();
    auto* bar=GetMenuBar();
    if(bar)bar->Enable(ID_ToggleExplorer,true);
    if(!splitter_->IsSplit() && browser_->IsShown()) {
        splitter_->SplitVertically(browser_,editors_,FromDIP(520));
        splitter_->SetSashGravity(0.0);
    }
    if(page!=wxNOT_FOUND)editors_->ChangeSelection(page);
    switchMenus(index);
    Layout();
}
bool WorkspaceFrame::closeEditorPage(int page) {
    const auto index=editorForPage(page);
    if(index>=kEditorCount)return false;
    const auto kind=static_cast<EditorKind>(index);
    auto* panel=panelFor(kind);
    if(!panel || !panel->canClose())return false;
    const bool wasActive=activeEditor_==index;
    if(wasActive)detachActiveMenus();
    if(!editors_->RemovePage(static_cast<std::size_t>(page))) {
        if(wasActive)switchMenus(index);
        return false;
    }
    destroyEditor(kind);
    if(editors_->GetPageCount()==0) {
        showBrowserOnly();
    } else {
        int selection=editors_->GetSelection();
        if(selection==wxNOT_FOUND) {
            const auto replacement=std::min(static_cast<std::size_t>(std::max(page,0)),editors_->GetPageCount()-1);
            editors_->ChangeSelection(replacement);selection=static_cast<int>(replacement);
        }
        const auto next=editorForPage(selection);
        if(next<kEditorCount)switchMenus(next);
        auto* bar=GetMenuBar();if(bar)bar->Enable(ID_ToggleExplorer,true);
        editors_->Show();
    }
    Layout();return true;
}
void WorkspaceFrame::showBrowserOnly() {
    detachActiveMenus();
    browser_->Show();
    if(splitter_->IsSplit()) {
        splitter_->Unsplit(editors_);
    } else if(splitter_->GetWindow1()==editors_ || splitter_->GetWindow2()==editors_) {
        splitter_->ReplaceWindow(editors_,browser_);
    } else if(splitter_->GetWindow1()==nullptr && splitter_->GetWindow2()==nullptr) {
        splitter_->Initialize(browser_);
    }
    editors_->Hide();
    auto* bar=GetMenuBar();
    if(bar) {bar->Check(ID_ToggleExplorer,true);bar->Enable(ID_ToggleExplorer,false);}
    Layout();
}
void WorkspaceFrame::setExplorerVisible(bool visible) {
    auto* bar=GetMenuBar();
    if(activeEditor_>=kEditorCount) {
        if(bar)bar->Check(ID_ToggleExplorer,true);
        return;
    }
    if(visible) {
        browser_->Show();editors_->Show();
        if(!splitter_->IsSplit()) {
            splitter_->SplitVertically(browser_,editors_,FromDIP(520));
            splitter_->SetSashGravity(0.0);
        }
    } else {
        if(splitter_->IsSplit())splitter_->Unsplit(browser_);
        browser_->Hide();editors_->Show();
    }
    if(bar)bar->Check(ID_ToggleExplorer,visible);
    Layout();
}
void WorkspaceFrame::selectEditor(std::size_t index) {
    if(index>=kEditorCount)return;
    showEditor(static_cast<EditorKind>(index));
}
void WorkspaceFrame::applyAppearance() {
    wxui::applyTheme(this,dark_);browser_->setAppearance(dark_,fontScale_);
    for(auto* panel:panels())if(panel)panel->setAppearance(dark_,fontScale_);
    if(auto* bar=GetMenuBar())bar->Check(ID_Dark,dark_);
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
    ensureEditor(selected);
    bool ok=false;
    switch(selected) {
    case EditorKind::Tables:ok=tables_->openResource(std::move(resource));break;
    case EditorKind::Soundsets:ok=soundsets_->openResource(std::move(resource));break;
    case EditorKind::TalkTables:ok=talkTables_->openResource(std::move(resource));break;
    case EditorKind::Gff:ok=structured_->openResource(std::move(resource));break;
    case EditorKind::Dialogues:ok=dialogues_->openResource(std::move(resource));break;
    case EditorKind::Journals:ok=journals_->openResource(std::move(resource));break;
    case EditorKind::Textures:ok=textures_->openResource(std::move(resource));break;
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
        if(choices.empty())throw std::runtime_error("Open a game directory, chitin.key, ERF/RIM-family archive, table, texture, soundset, talk table, or GFF-backed resource.");
        const auto selected=selectResourceEditor(choices,editor);
        ensureEditor(selected);
        bool ok=false;
        switch(selected) {
        case EditorKind::Tables:ok=tables_->openFile(path);break;
        case EditorKind::Soundsets:ok=soundsets_->openFile(path);break;
        case EditorKind::TalkTables:ok=talkTables_->openFile(path);break;
        case EditorKind::Gff:ok=structured_->openFile(path);break;
        case EditorKind::Dialogues:ok=dialogues_->openFile(path);break;
        case EditorKind::Journals:ok=journals_->openFile(path);break;
        case EditorKind::Textures:ok=textures_->openFile(path);break;
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
