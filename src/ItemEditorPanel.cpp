#include "ItemEditorPanel.hpp"
#include "BlueprintEditor.hpp"
#include "wx/GFFEditorPanel.hpp"
#include "NeoViewState.hpp"
#include <neoshared/bif/ModFolder.hpp>
#include <wx/sizer.h>
#include <wx/button.h>
#include <wx/dirdlg.h>
#include <wx/notebook.h>

namespace neotoolkit {
namespace {
class ItemEditor final : public ItemEditorPanel {
    struct SourceContext {
        neoshared::ResourceDocument source;
        std::shared_ptr<ArchiveContext> archive;
        std::shared_ptr<neomdl::GameResourceSession> game;
    };
public:
    ItemEditor(wxWindow* parent,neomodules::Context context,GameResources games):ItemEditorPanel(parent,std::move(context)),games_(std::move(games)) {
        auto* box=new wxBoxSizer(wxVERTICAL);
        auto* assets=assets_=new wxButton(this,wxID_ANY,"Game resources...");box->Add(assets,0,wxALL,4);
        assets->Bind(wxEVT_BUTTON,[this](wxCommandEvent&){try {
            if(!item_->applyPending())return;
            auto& source=sources_.at(selectedIdentity_);
            wxDirDialog dialog(this,"Choose the item's K1 or K2 installation",source.game?neosettings::pathToWx(source.game->installation()->installPath):wxString{},wxDD_DIR_MUST_EXIST);
            if(dialog.ShowModal()!=wxID_OK)return;
            const auto path=neosettings::pathFromWx(dialog.GetPath());const auto installation=neoshared::game::inspectGameInstallation(path);
            if(!installation||(installation->id!="kotor"&&installation->id!="kotor2"))throw std::runtime_error("Choose a K1 or K2 installation containing chitin.key and its game data.");
            source.game=games_(source.source.module,path/"chitin.key");refresh(true);
        } catch(const std::exception& e){wxui::showError(this,e);}});
        tabs_=new wxNotebook(this,wxID_ANY);
        BlueprintPanel::Actions actions;actions.sourceNote="";actions.saveLabel="Save UTI";
        actions.fields=[this]{tabs_->SetSelection(1);};
        actions.save=[this]{wxCommandEvent event(wxEVT_MENU,saveId_);fields_->dispatch(event);};
        actions.edit=[this](const auto& path,const auto& type,const auto& value,const auto&) {
            editBlueprintField(*fields_->activeModel(),path,type,value);fields_->refreshActiveDocument();refresh();
        };
        item_=new BlueprintPanel(tabs_,std::move(actions));tabs_->AddPage(item_,"Item");
        auto child=context_;child.validateOutputs=[this](const auto& paths,const auto*,const auto& inputs){validateHostOutputs(paths,inputs);};
        fields_=neogff::ui::createEditorPanel(tabs_,std::move(child));tabs_->AddPage(fields_,"View GFF");
        box->Insert(0,fields_->moveDocumentTabs(this),0,wxEXPAND|wxLEFT|wxRIGHT,6);box->Add(tabs_,1,wxEXPAND);SetSizer(box);
        auto menus=fields_->takeMenus();
        for(std::size_t i=0;i<menus->GetMenuCount();++i)for(auto* entry:menus->GetMenu(i)->GetMenuItems())
            if(entry->GetItemLabelText()=="Save")saveId_=entry->GetId();
        setModuleMenus(menus.release());
        Bind(wxEVT_MENU,[this](wxCommandEvent& event){if(item_->applyPending())fields_->dispatch(event);});
        Bind(wxEVT_UPDATE_UI,[this](wxUpdateUIEvent& event){fields_->dispatch(event);});
        Bind(wxEVT_MENU_OPEN,[this](wxMenuEvent& event){fields_->dispatch(event);});
        fields_->Bind(neogff::ui::EVT_GFF_DOCUMENT_EDITED,[this](wxCommandEvent&){refresh();});
        fields_->Bind(neogff::ui::EVT_GFF_DOCUMENT_SELECTED,[this](wxCommandEvent&){refresh();});
        fields_->Bind(neogff::ui::EVT_GFF_DOCUMENT_CHANGING,[this](wxNotifyEvent& event){if(!item_->applyPending())event.Veto();});
        tabs_->Bind(wxEVT_NOTEBOOK_PAGE_CHANGING,[this](wxBookCtrlEvent& event){if(event.GetEventObject()==tabs_&&!item_->applyPending())event.Veto();else event.Skip();});
        item_->Disable();assets_->Disable();
    }
    ~ItemEditor() override {DestroyChildren();}
    bool openFile(const std::filesystem::path& path) override {if(!item_->applyPending())return false;const auto opened=fields_->openFile(path);if(opened)tabs_->SetSelection(0);return opened;}
    bool openResource(neoshared::ResourceDocument source,std::shared_ptr<ArchiveContext> archive={}) override {
        if(!item_->applyPending())return false;
        if(fields_->activateResource(source.identity)) {tabs_->SetSelection(0);return true;}
        const auto identity=source.identity;
        sources_.insert_or_assign(identity,makeContext(source,std::move(archive)));
        const auto opened=fields_->openResource(std::move(source));if(!opened)sources_.erase(identity);else tabs_->SetSelection(0);return opened;
    }
    void resourceContextChanged() override {
        if(!item_->applyPending())return;
        for(auto& [identity,context]:sources_) {
            const auto source=context.game?context.game->installation()->installPath/"chitin.key":context.source.sourcePath;
            context.game=games_(context.source.module,source);
        }
        refresh(true);
    }
    bool canClose() override {return item_->applyPending()&&fields_->canClose();}
    void setAppearance(bool dark,double scale) override {fields_->setAppearance(dark,scale);wxui::applyTheme(item_,dark);neoview::applyFontScale(item_,scale);}
    std::vector<std::filesystem::path> openPaths() const override {auto paths=fields_->openPaths();const auto preview=item_->previewPaths();paths.insert(paths.end(),preview.begin(),preview.end());return paths;}
private:
    SourceContext makeContext(neoshared::ResourceDocument source,std::shared_ptr<ArchiveContext> archive={}) {
        if(source.module.empty())source.module=neoshared::bif::ModFolder::moduleName(source.sourcePath);
        auto game=archive?archive->game:games_(source.module,source.sourcePath);
        return {std::move(source),std::move(archive),std::move(game)};
    }
    void refresh(bool contextChanged=false) try {
        const auto* model=fields_->activeModel();const bool item=model&&model->loaded()&&model->gff().filetype().compare(0,3,"UTI")==0;
        item_->Enable(item);assets_->Enable(item);
        if(!item) {item_->clear();selectedModel_=model;tabs_->ChangeSelection(1);return;}
        auto identity=fields_->activeResourceIdentity();
        if(identity.empty())identity=neomdl::resourceFileIdentity(fields_->activeFilename());
        if(contextChanged||selectedModel_!=model||selectedIdentity_!=identity) {
            if(selectedModel_==model&&selectedIdentity_!=identity)
                sources_.insert_or_assign(identity,sources_.at(selectedIdentity_));
            if(!sources_.count(identity)) {
                neoshared::ResourceDocument source;source.sourcePath=fields_->activeFilename();
                sources_.emplace(identity,makeContext(std::move(source)));
            }
            const auto& source=sources_.at(identity);
            auto game=source.game?source.game->resolver():neomdl::ResourceResolver([captured=context_.captureScopedResourceResolver(source.source.module)](const std::string& name,std::uint16_t type){return captured(name,type,{});});
            const auto directory=(source.source.sourcePath.has_extension()&&neomdl::resourceKey(neoshared::pathToUtf8(source.source.sourcePath.extension()))==".uti")?source.source.sourcePath.parent_path():std::filesystem::path{};
            auto local=directory.empty()?neomdl::ResourceResolver{}:neomdl::directoryResources(directory);
            resources_=[archive=source.archive,game=std::move(game),local=std::move(local),mod=context_.captureModFolder(),scope=source.source.module](const std::string& name,std::uint16_t type) {
                if(mod&&mod->shadows(name,type,scope))return mod->resolve(name,type,scope,neomdl::resourceLoadJob());
                if(archive)if(auto resource=archive->resources(name,type))return resource;
                if(local)if(auto resource=local(name,type))return resource;
                return game(name,type);
            };
            selectedModel_=model;selectedIdentity_=identity;
            const auto title=source.source.sourceDescription.empty()?neoshared::pathToUtf8(fields_->activeFilename()):source.source.sourceDescription;
            item_->open(2025,source.game&&source.game->installation()->id=="kotor2"?neomdl::model::GameVersion::Kotor2:neomdl::model::GameVersion::Kotor1,
                title,*model->gff().root(),resources_,source.game,{},directory);
        } else item_->refresh(*model->gff().root(),resources_);
    } catch(const std::exception& e) {item_->clear();item_->Disable();selectedModel_=nullptr;wxui::showError(this,e);}
    GameResources games_;
    std::map<std::string,SourceContext> sources_;
    neomdl::ResourceResolver resources_;
    const neogff::GffModel* selectedModel_{};
    std::string selectedIdentity_;
    neogff::ui::GFFEditorPanel* fields_{};
    BlueprintPanel* item_{};
    wxNotebook* tabs_{};
    wxButton* assets_{};
    int saveId_{};
};
}
ItemEditorPanel* createItemEditorPanel(wxWindow* parent,neomodules::Context context,GameResources games) {return new ItemEditor(parent,std::move(context),std::move(games));}
}
