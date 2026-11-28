#include "StoreEditorPanel.hpp"
#include "BlueprintEditor.hpp"
#include "wx/GFFEditorPanel.hpp"
#include "NeoViewState.hpp"
#include <wx/button.h>
#include <wx/dirdlg.h>
#include <wx/notebook.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

namespace neotoolkit {
namespace {
class StoreEditor final : public StoreEditorPanel {
    struct Store {
        neoshared::ResourceDocument source;
        std::shared_ptr<ArchiveContext> parent;
        std::shared_ptr<neomdl::GameResourceSession> game;
        neomdl::ResourceResolver resources;
        std::filesystem::path directory;
    };
public:
    StoreEditor(wxWindow* parent,neomodules::Context context,GameResources games,
        std::function<void(neoshared::ResourceDocument,std::shared_ptr<ArchiveContext>)> open)
        :StoreEditorPanel(parent,std::move(context)),games_(std::move(games)) {
        auto* box=new wxBoxSizer(wxVERTICAL);
        auto* header=new wxBoxSizer(wxHORIZONTAL);
        source_=new wxStaticText(this,wxID_ANY,"Open a merchant UTM resource.");header->Add(source_,1,wxALIGN_CENTER_VERTICAL|wxALL,6);
        back_=new wxButton(this,wxID_ANY,"Back to archive");header->Add(back_,0,wxALL,4);back_->Hide();
        back_->Bind(wxEVT_BUTTON,[this](wxCommandEvent&){try {current().parent->show();}catch(const std::exception& e){wxui::showError(this,e);}});
        auto* assets=new wxButton(this,wxID_ANY,"Game resources...");header->Add(assets,0,wxALL,4);
        assets->Bind(wxEVT_BUTTON,[this](wxCommandEvent&){
            try {
                if(!merchant_->applyPending())return;
                auto& store=current();
                wxDirDialog dialog(this,"Choose the store's K1 or K2 installation",store.game?neosettings::pathToWx(store.game->installation()->installPath):wxString{},wxDD_DIR_MUST_EXIST);
                if(dialog.ShowModal()!=wxID_OK)return;
                const auto path=neosettings::pathFromWx(dialog.GetPath());const auto installation=neoshared::game::inspectGameInstallation(path);
                if(!installation||(installation->id!="kotor"&&installation->id!="kotor2"))throw std::runtime_error("Choose a K1 or K2 installation containing chitin.key and its game data.");
                store.game=games_(store.source.module,path/"chitin.key");bindResources(store);refresh(true);
            } catch(const std::exception& e){wxui::showError(this,e);}
        });
        box->Add(header,0,wxEXPAND);tabs_=new wxNotebook(this,wxID_ANY);
        BlueprintPanel::Actions actions;
        actions.sourceNote="";actions.saveLabel="Save UTM";
        actions.fields=[this]{tabs_->SetSelection(1);};actions.save=[this]{save();};
        actions.stockFields=[this](std::size_t index){tabs_->SetSelection(1);fields_->selectField("ItemList\\"+std::to_string(index));};
        actions.edit=[this](const auto& path,const auto& type,const auto& value,const auto&){editBlueprintField(*fields_->activeModel(),path,type,value);edited();};
        actions.addItem=[this](neoshared::ResourceDocument item,const neomdl::ResourceResolver&) {
            auto& root=*fields_->activeModel()->gff().root();
            if(!root.GetFieldByLabel("ItemList"))root.AddField(std::make_unique<neogff::GffList>("ItemList"));
            auto& list=static_cast<neogff::GffList&>(*root.GetFieldByLabel("ItemList"));
            auto entry=std::make_unique<neogff::GffStruct>();entry->typeid_=0;
            entry->AddField(std::make_unique<neogff::GffResRefField>("InventoryRes",neoshared::pathToUtf8(neoshared::pathFromUtf8(item.fileName).stem())));
            entry->AddField(std::make_unique<neogff::GffByteField>("Infinite",0));list.AddStruct(std::move(entry));
            edited();merchant_->selectInventory(list.count()-1);
        };
        actions.stock=[this](BlueprintPanel::StockAction action,std::size_t index,bool enabled) {
            auto& list=static_cast<neogff::GffList&>(*fields_->activeModel()->gff().root()->GetFieldByLabel("ItemList"));
            if(action==BlueprintPanel::StockAction::Remove)list.allStructs().erase(list.allStructs().begin()+index);
            else if(action==BlueprintPanel::StockAction::Duplicate) {auto copy=list.GetStruct(index)->Clone();auto& item=static_cast<neogff::GffStruct&>(*copy);if(item.GetFieldByLabel("ObjectId"))item.DeleteField("ObjectId");list.AddStruct(static_cast<neogff::GffStruct*>(copy.release()));}
            else {
                auto& item=*list.GetStruct(index);auto* flag=static_cast<neogff::GffByteField*>(item.GetFieldByLabel("Infinite"));
                if(flag)flag->value=enabled?1:0;
                else item.AddField(std::make_unique<neogff::GffByteField>("Infinite",enabled?1:0));
            }
            edited();
        };
        actions.itemFields=[this,open=std::move(open)](const auto& name,std::uint16_t type){
            auto source=current().resources(name,type);
            if(!source)throw std::runtime_error("Cannot find item template "+name+".uti in this store's resources.");
            open(std::move(*source),current().parent);
        };
        merchant_=new BlueprintPanel(tabs_,std::move(actions));tabs_->AddPage(merchant_,"Merchant / shop preview");
        auto child=context_;child.titleChanged=[this](const wxString& title){setModuleTitle(title);if(fields_&&fields_->activeModel()==selectedModel_&&selectedKey_!=key())refresh(true);};
        child.validateOutputs=[this](const auto& paths,const auto*,const auto& inputs){validateHostOutputs(paths,inputs);};
        fields_=neogff::ui::createEditorPanel(tabs_,std::move(child));tabs_->AddPage(fields_,"All fields");
        box->Add(fields_->moveDocumentTabs(this),0,wxEXPAND|wxLEFT|wxRIGHT,6);
        auto menus=fields_->takeMenus();
        for(std::size_t i=0;i<menus->GetMenuCount();++i)for(auto* item:menus->GetMenu(i)->GetMenuItems())
            if(item->GetItemLabelText()=="Save") {saveId_=item->GetId();saveItem_=item;}
        setModuleMenus(menus.release());
        Bind(wxEVT_MENU,[this](wxCommandEvent& event){if(!merchant_->applyPending())return;if(event.GetId()==saveId_)save();else fields_->dispatch(event);});
        Bind(wxEVT_UPDATE_UI,[this](wxUpdateUIEvent& event){fields_->dispatch(event);});
        Bind(wxEVT_MENU_OPEN,[this](wxMenuEvent& event){fields_->dispatch(event);});
        fields_->Bind(wxEVT_BUTTON,[this](wxCommandEvent& event){if(event.GetId()==saveId_)save();else event.Skip();});
        fields_->Bind(neogff::ui::EVT_GFF_DOCUMENT_SELECTED,[this](wxCommandEvent&){refresh(true);});
        fields_->Bind(neogff::ui::EVT_GFF_DOCUMENT_CHANGING,[this](wxNotifyEvent& event){if(!merchant_->applyPending())event.Veto();});
        fields_->Bind(neogff::ui::EVT_GFF_DOCUMENT_EDITED,[this](wxCommandEvent&){refresh(false);});
        tabs_->Bind(wxEVT_NOTEBOOK_PAGE_CHANGING,[this](wxBookCtrlEvent& event){if(event.GetEventObject()==tabs_&&!merchant_->applyPending())event.Veto();else event.Skip();});
        box->Add(tabs_,1,wxEXPAND);SetSizer(box);merchant_->Disable();
    }
    ~StoreEditor() override {DestroyChildren();}
    bool openFile(const std::filesystem::path& path) override {if(!merchant_->applyPending())return false;const bool ok=fields_->openFile(path);if(ok)tabs_->SetSelection(0);return ok;}
    bool openResource(neoshared::ResourceDocument source,std::shared_ptr<ArchiveContext> parent={}) override {
        if(!merchant_->applyPending())return false;
        if(fields_->activateResource(source.identity)) {tabs_->SetSelection(0);return true;}
        const auto identity=source.identity;stores_.insert_or_assign(identity,makeStore(source,std::move(parent)));
        const bool ok=fields_->openResource(std::move(source));if(ok)tabs_->SetSelection(0);else stores_.erase(identity);return ok;
    }
    bool canClose() override {return merchant_->applyPending()&&fields_->canClose();}
    void resourceContextChanged() override {
        if(!merchant_->applyPending())return;
        for(auto& [identity,store]:stores_) {
            const auto source=store.game?store.game->installation()->installPath/"chitin.key":store.source.sourcePath;
            store.game=games_(store.source.module,source);bindResources(store);
        }
        refresh(true);
    }
    std::vector<std::filesystem::path> openPaths() const override {return fields_->openPaths();}
    void setAppearance(bool dark,double scale) override {fields_->setAppearance(dark,scale);wxui::applyTheme(merchant_,dark);neoview::applyFontScale(merchant_,scale);}
private:
    std::string key() const {
        const auto identity=fields_->activeResourceIdentity();
        return identity.empty()?neomdl::resourceFileIdentity(fields_->activeFilename()):identity;
    }
    Store& current() {return stores_.at(key());}
    Store makeStore(neoshared::ResourceDocument source,std::shared_ptr<ArchiveContext> parent={}) {
        const auto module=source.module.empty()?neoshared::bif::ModFolder::moduleName(source.sourcePath):source.module;
        auto game=parent?parent->game:games_(module,source.sourcePath);
        const auto directory=(source.sourcePath.has_extension()&&neomdl::resourceKey(neoshared::pathToUtf8(source.sourcePath.extension()))==".utm")?source.sourcePath.parent_path():std::filesystem::path{};
        source.module=module;
        Store store{std::move(source),std::move(parent),std::move(game),{},directory};bindResources(store);return store;
    }
    void bindResources(Store& store) {
        auto local=store.directory.empty()?neomdl::ResourceResolver{}:neomdl::directoryResources(store.directory);
        auto game=store.game?store.game->resolver():neomdl::ResourceResolver([captured=context_.captureScopedResourceResolver(store.source.module)](const std::string& name,std::uint16_t type){return captured(name,type,{});});
        store.resources=[parent=store.parent,game=std::move(game),local=std::move(local),mod=context_.captureModFolder(),scope=store.source.module](const std::string& name,std::uint16_t type) {
            if(mod&&mod->shadows(name,type,scope))return mod->resolve(name,type,scope,neomdl::resourceLoadJob());
            if(parent)if(auto resource=parent->resources(name,type))return resource;
            if(local)if(auto resource=local(name,type))return resource;
            return game(name,type);
        };
    }
    void refresh(bool changed) {
        try {
            auto* model=fields_->activeModel();
            if(!model||!model->loaded()||model->fileType().compare(0,3,"UTM")!=0) {merchant_->clear();merchant_->Disable();back_->Hide();return;}
            const auto identity=key();
            selectedModel_=model;selectedKey_=identity;
            if(!stores_.count(identity)) {
                neoshared::ResourceDocument source;source.identity=identity;source.type=2051;
                source.sourcePath=fields_->activeFilename();source.fileName=neoshared::pathToUtf8(fields_->activeFilename().filename());
                source.sourceDescription=neoshared::pathToUtf8(fields_->activeFilename());stores_.emplace(identity,makeStore(std::move(source)));
            }
            auto& store=current();merchant_->Enable();
            const auto title=store.parent?store.parent->label+" > "+store.source.fileName:store.source.sourceDescription;
            source_->SetLabel(wxui::toWx(title+(model->dirty()?" *":"")));back_->Show(bool(store.parent));
            saveItem_->SetItemLabel(store.parent?"Update Parent Archive\tCtrl+S":"Save\tCtrl+S");
            merchant_->setSaveLabel(store.parent?"Update parent archive":"Save UTM");
            fields_->FindWindow(saveId_)->SetLabel(store.parent?"Update archive":"Save");
            if(changed)merchant_->open(2051,store.game&&store.game->installation()->id=="kotor"?neomdl::model::GameVersion::Kotor1:neomdl::model::GameVersion::Kotor2,
                title,*model->gff().root(),store.resources,store.game,std::make_shared<neoshared::bif::LooseArchiveCatalog>(),store.directory);
            else merchant_->refresh(*model->gff().root(),store.resources);
            setModuleTitle(source_->GetLabel());Layout();
        } catch(const std::exception& e) {merchant_->clear();merchant_->Disable();wxui::showError(this,e);}
    }
    void edited() {fields_->activeModel()->gff().dirty(true);fields_->refreshActiveDocument();refresh(false);}
    void save() {
        try {
            if(!merchant_->applyPending())return;
            if(!fields_->activeModel()||!fields_->activeModel()->loaded()||fields_->activeModel()->fileType().compare(0,3,"UTM")!=0) {
                wxCommandEvent event(wxEVT_MENU,saveId_);fields_->dispatch(event);return;
            }
            auto& store=current();
            if(store.parent) {
                auto resource=store.source;resource.bytes=fields_->activeModel()->gff().SaveBytes();store.parent->update({std::move(resource)});
                fields_->activeModel()->gff().dirty(false);fields_->refreshActiveDocument();refresh(false);
                source_->SetLabel(source_->GetLabel()+" — updated; save the parent archive to write to disk.");
            } else {wxCommandEvent event(wxEVT_MENU,saveId_);fields_->dispatch(event);refresh(false);}
        } catch(const std::exception& e) {wxui::showError(this,e);}
    }
    GameResources games_;
    std::map<std::string,Store> stores_;
    wxNotebook* tabs_{};wxStaticText* source_{};wxButton* back_{};
    BlueprintPanel* merchant_{};neogff::ui::GFFEditorPanel* fields_{};
    int saveId_{};wxMenuItem* saveItem_{};
    const neogff::GffModel* selectedModel_{};std::string selectedKey_;
};
}
StoreEditorPanel* createStoreEditorPanel(wxWindow* parent,neomodules::Context context,GameResources games,
    std::function<void(neoshared::ResourceDocument,std::shared_ptr<ArchiveContext>)> open) {
    return new StoreEditor(parent,std::move(context),std::move(games),std::move(open));
}
}
