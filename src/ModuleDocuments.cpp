#include "ModuleEditorPanel.hpp"
#include "NeoViewState.hpp"
#include <wx/aui/auibook.h>
#include <wx/sizer.h>

namespace neotoolkit {
namespace {
wxMenu* copyMenu(const wxMenu& source) {
    auto* copy=new wxMenu;
    for(auto* item:source.GetMenuItems()) {
        if(item->IsSeparator())copy->AppendSeparator();
        else if(item->GetSubMenu())copy->AppendSubMenu(copyMenu(*item->GetSubMenu()),item->GetItemLabel());
        else copy->Append(item->GetId(),item->GetItemLabel(),item->GetHelp(),item->GetKind());
    }
    return copy;
}
void syncMenu(wxMenu& target,const wxMenu& source) {
    auto to=target.GetMenuItems().begin();
    for(auto* from:source.GetMenuItems()) {
        auto* item=*to++;
        if(from->IsSeparator())continue;
        item->SetItemLabel(from->GetItemLabel());item->Enable(from->IsEnabled());
        if(from->IsCheckable())item->Check(from->IsChecked());
        if(from->GetSubMenu())syncMenu(*item->GetSubMenu(),*from->GetSubMenu());
    }
}

class ModuleDocuments final : public ModuleEditorPanel {
    struct Document {
        std::string identity,label;
        ModuleEditorPanel* panel;
        std::unique_ptr<wxMenuBar> menus;
    };
public:
    ModuleDocuments(wxWindow* parent,neomodules::Context context,GameResources games)
        :ModuleEditorPanel(parent,std::move(context)),games_(std::move(games)) {
        tabs_=new wxAuiNotebook(this,wxID_ANY,wxDefaultPosition,wxDefaultSize,
            wxAUI_NB_TOP|wxAUI_NB_TAB_MOVE|wxAUI_NB_CLOSE_ON_ACTIVE_TAB|wxAUI_NB_SCROLL_BUTTONS);
        auto* box=new wxBoxSizer(wxVERTICAL);box->Add(tabs_,1,wxEXPAND);SetSizer(box);
        addDocument({},"Open a module");
        auto* menus=new wxMenuBar;
        for(std::size_t i=0;i<documents_.front().menus->GetMenuCount();++i) {
            auto* menu=copyMenu(*documents_.front().menus->GetMenu(i));menuRoots_.push_back(menu);
            menus->Append(menu,documents_.front().menus->GetMenuLabel(i));
        }
        setModuleMenus(menus);syncMenus();
        Bind(wxEVT_MENU,[this](wxCommandEvent& event){active().panel->dispatch(event);syncMenus();});
        Bind(wxEVT_MENU_OPEN,[this](wxMenuEvent& event){active().panel->dispatch(event);syncMenus();});
        tabs_->Bind(wxEVT_AUINOTEBOOK_PAGE_CHANGED,[this](wxAuiNotebookEvent& event){
            if(event.GetEventObject()==tabs_&&event.GetSelection()>=0)syncMenus();
            event.Skip();
        });
        tabs_->Bind(wxEVT_AUINOTEBOOK_PAGE_CLOSE,[this](wxAuiNotebookEvent& event){
            if(event.GetEventObject()!=tabs_) {event.Skip();return;}
            event.Veto();auto* panel=static_cast<ModuleEditorPanel*>(tabs_->GetPage(event.GetSelection()));
            if(!panel->canClose())return;
            removeDocument(panel);
        });
    }
    ~ModuleDocuments() override {DestroyChildren();}
    bool openFile(const std::filesystem::path& path,bool installed=false) override {
        const auto identity=neomdl::resourceFileIdentity(path);
        return open(identity,neoshared::pathToUtf8(path.filename()),[&](auto& panel){return panel.openFile(path,installed);});
    }
    bool openResource(neoshared::ResourceDocument resource,std::shared_ptr<ArchiveContext> archive={}) override {
        const auto identity=resource.identity,label=archive?archive->label+" > "+resource.fileName:resource.fileName;
        return open(identity,label,[&](auto& panel){return panel.openResource(std::move(resource),std::move(archive));});
    }
    bool canClose() override {
        for(auto& document:documents_)if(!document.panel->canClose()) {select(document.panel);return false;}
        return true;
    }
    bool activateResource(const std::string& identity) override {
        for(auto& document:documents_)if(document.panel->activateResource(identity)) {select(document.panel);return true;}
        return false;
    }
    void setAppearance(bool dark,double scale) override {
        dark_=dark;scale_=scale;wxui::applyTheme(this,dark);
        for(auto& document:documents_)document.panel->setAppearance(dark,scale);
    }
    void resourceContextChanged() override {for(auto& document:documents_)document.panel->resourceContextChanged();}
    std::vector<std::filesystem::path> openPaths() const override {
        std::vector<std::filesystem::path> result;
        for(const auto& document:documents_) {const auto paths=document.panel->openPaths();result.insert(result.end(),paths.begin(),paths.end());}
        return result;
    }
private:
    Document& active() {
        auto* page=tabs_->GetCurrentPage();
        return *std::find_if(documents_.begin(),documents_.end(),[page](const auto& document){return document.panel==page;});
    }
    void syncMenus() {
        auto& document=active();
        for(std::size_t i=0;i<menuRoots_.size();++i)syncMenu(*menuRoots_[i],*document.menus->GetMenu(i));
        const auto index=tabs_->GetPageIndex(document.panel);
        setModuleTitle(tabs_->GetPageToolTip(index).empty()?tabs_->GetPageText(index):tabs_->GetPageToolTip(index));
    }
    void select(ModuleEditorPanel* panel) {tabs_->SetSelection(tabs_->GetPageIndex(panel));syncMenus();}
    ModuleEditorPanel* addDocument(const std::string& identity,const std::string& label) {
        auto context=context_;
        context.validateOutputs=[this](const auto& paths,const auto* owner,const auto& inputs){
            auto protectedInputs=inputs;
            for(const auto& document:documents_)if(document.panel!=owner) {
                const auto open=document.panel->openPaths();protectedInputs.insert(protectedInputs.end(),open.begin(),open.end());
            }
            validateHostOutputs(paths,protectedInputs);
        };
        context.titleChanged=[this,identity](const wxString& title){
            const auto found=std::find_if(documents_.begin(),documents_.end(),[&](const auto& document){return document.identity==identity;});
            if(found==documents_.end())return; // The constructor has not installed its document yet.
            const auto index=tabs_->GetPageIndex(found->panel);
            tabs_->SetPageText(index,wxui::toWx(found->label)+(title.EndsWith(" *")?" *":""));tabs_->SetPageToolTip(index,title);
            if(tabs_->GetCurrentPage()==found->panel)setModuleTitle(title);
        };
        auto* panel=createModuleDocument(tabs_,std::move(context),games_,[this](const auto& path){openFile(path);});
        documents_.push_back({identity,label,panel,panel->takeMenus()});
        panel->setAppearance(dark_,scale_);tabs_->AddPage(panel,wxui::toWx(label),true);
        return panel;
    }
    void removeDocument(ModuleEditorPanel* panel) {
        const auto found=std::find_if(documents_.begin(),documents_.end(),[panel](const auto& document){return document.panel==panel;});
        auto menus=std::shared_ptr<wxMenuBar>(std::move(found->menus));
        panel->Bind(wxEVT_DESTROY,[menus](wxWindowDestroyEvent& event){event.Skip();});
        tabs_->RemovePage(tabs_->GetPageIndex(panel));documents_.erase(found);panel->Destroy();
        if(documents_.empty())addDocument({},"Open a module");
        syncMenus();
    }
    bool open(const std::string& identity,const std::string& label,const std::function<bool(ModuleEditorPanel&)>& load) {
        if(activateResource(identity))return true;
        for(auto& document:documents_)if(document.identity==identity) {select(document.panel);return true;}
        auto* panel=addDocument(identity,label);
        if(!load(*panel)) {removeDocument(panel);return false;}
        if(documents_.size()>1&&documents_.front().identity.empty())removeDocument(documents_.front().panel);
        select(panel);return true;
    }
    GameResources games_;
    wxAuiNotebook* tabs_{};
    std::vector<Document> documents_;
    std::vector<wxMenu*> menuRoots_;
    bool dark_{};double scale_=1;
};
}
ModuleEditorPanel* createModuleEditorPanel(wxWindow* parent,neomodules::Context context,GameResources games) {
    return new ModuleDocuments(parent,std::move(context),std::move(games));
}
}
