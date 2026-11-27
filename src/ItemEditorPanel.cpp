#include "ItemEditorPanel.hpp"
#include "ItemPreview.hpp"
#include "wx/GFFEditorPanel.hpp"
#include "NeoViewState.hpp"
#include <neoshared/bif/ModFolder.hpp>
#include <wx/sizer.h>
#include <wx/splitter.h>

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
        auto* split=new wxSplitterWindow(this,wxID_ANY,wxDefaultPosition,wxDefaultSize,wxSP_LIVE_UPDATE);
        auto child=context_;child.validateOutputs=[this](const auto& paths,const auto*,const auto& inputs){validateHostOutputs(paths,inputs);};
        fields_=neogff::ui::createEditorPanel(split,std::move(child));
        preview_=new ItemPreview(split,[this]()->const auto&{return *fields_->activeModel()->gff().root();},[this]{return resources_;});
        split->SetMinimumPaneSize(FromDIP(260));split->SplitVertically(fields_,preview_,-FromDIP(480));split->SetSashGravity(.6);auto* box=new wxBoxSizer(wxVERTICAL);box->Add(split,1,wxEXPAND);SetSizer(box);
        setModuleMenus(fields_->takeMenus().release());
        Bind(wxEVT_MENU,[this](wxCommandEvent& event){fields_->dispatch(event);});
        Bind(wxEVT_UPDATE_UI,[this](wxUpdateUIEvent& event){fields_->dispatch(event);});
        Bind(wxEVT_MENU_OPEN,[this](wxMenuEvent& event){fields_->dispatch(event);});
        fields_->Bind(neogff::ui::EVT_GFF_DOCUMENT_EDITED,[this](wxCommandEvent&){refresh();});
        fields_->Bind(neogff::ui::EVT_GFF_DOCUMENT_SELECTED,[this](wxCommandEvent&){refresh();});
        preview_->Disable();
    }
    ~ItemEditor() override {DestroyChildren();}
    bool openFile(const std::filesystem::path& path) override {return fields_->openFile(path);}
    bool openResource(neoshared::ResourceDocument source,std::shared_ptr<ArchiveContext> archive={}) override {
        if(fields_->activateResource(source.identity))return true;
        const auto identity=source.identity;
        sources_.insert_or_assign(identity,makeContext(source,std::move(archive)));
        const auto opened=fields_->openResource(std::move(source));if(!opened)sources_.erase(identity);return opened;
    }
    void resourceContextChanged() override {
        for(auto& [identity,context]:sources_) {
            const auto source=context.game?context.game->installation()->installPath/"chitin.key":context.source.sourcePath;
            context.game=games_(context.source.module,source);
        }
        refresh(true);
    }
    bool canClose() override {return fields_->canClose();}
    void setAppearance(bool dark,double scale) override {fields_->setAppearance(dark,scale);wxui::applyTheme(preview_,dark);neoview::applyFontScale(preview_,scale);}
    std::vector<std::filesystem::path> openPaths() const override {auto paths=fields_->openPaths();const auto preview=preview_->openPaths();paths.insert(paths.end(),preview.begin(),preview.end());return paths;}
private:
    SourceContext makeContext(neoshared::ResourceDocument source,std::shared_ptr<ArchiveContext> archive={}) {
        if(source.module.empty())source.module=neoshared::bif::ModFolder::moduleName(source.sourcePath);
        auto game=archive?archive->game:games_(source.module,source.sourcePath);
        return {std::move(source),std::move(archive),std::move(game)};
    }
    void refresh(bool contextChanged=false) try {
        const auto* model=fields_->activeModel();const bool item=model&&model->loaded()&&model->gff().filetype().compare(0,3,"UTI")==0;
        preview_->Enable(item);
        if(!item) {preview_->clear();selectedModel_=model;return;}
        auto identity=fields_->activeResourceIdentity();
        if(identity.empty())identity=neomdl::resourceFileIdentity(fields_->activeFilename());
        if(contextChanged||selectedModel_!=model||selectedIdentity_!=identity) {
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
            preview_->setContext(source.game?source.game->installation()->tlkPath:model->tlk().filename(),
                source.game&&source.game->installation()->id=="kotor2"?neomdl::model::GameVersion::Kotor2:neomdl::model::GameVersion::Kotor1);
            selectedModel_=model;selectedIdentity_=identity;
        }
        preview_->refreshItem();
    } catch(const std::exception& e) {preview_->clear();preview_->Disable();selectedModel_=nullptr;wxui::showError(this,e);}
    GameResources games_;
    std::map<std::string,SourceContext> sources_;
    neomdl::ResourceResolver resources_;
    const neogff::GffModel* selectedModel_{};
    std::string selectedIdentity_;
    neogff::ui::GFFEditorPanel* fields_{};
    ItemPreview* preview_{};
};
}
ItemEditorPanel* createItemEditorPanel(wxWindow* parent,neomodules::Context context,GameResources games) {return new ItemEditor(parent,std::move(context),std::move(games));}
}
