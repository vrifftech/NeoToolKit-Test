#include "PlaceableEditor.hpp"
#include <neoshared/bif/ModFolder.hpp>
#include "wx/GFFEditorPanel.hpp"
#include "NeoViewState.hpp"
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/notebook.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

namespace neotoolkit {
namespace {
class ObjectBlueprints final : public ObjectEditorPanel {
public:
    ObjectBlueprints(wxWindow* parent,neomodules::Context context,std::uint16_t type):ObjectEditorPanel(parent,std::move(context)),type_(type) {
        auto* box=new wxBoxSizer(wxVERTICAL);auto* tools=new wxBoxSizer(wxHORIZONTAL);
        tools->Add(new wxStaticText(this,wxID_ANY,"Target game"),0,wxALIGN_CENTER_VERTICAL|wxALL,6);
        game_=new wxChoice(this,wxID_ANY);game_->Append("Knights of the Old Republic");game_->Append("The Sith Lords");game_->SetSelection(1);tools->Add(game_,0,wxALL,4);
        auto* resources=new wxButton(this,wxID_ANY,"Refresh game resources");tools->Add(resources,0,wxALL,4);box->Add(tools,0,wxEXPAND);
        tabs_=new wxNotebook(this,wxID_ANY);
        editor_=new ObjectEditor(tabs_,type_,[this](const auto& path,const auto& type,const auto& value,const auto&) {
            editBlueprintField(*fields_->activeModel(),path,type,value);fields_->refreshActiveDocument();editor_->refresh(*fields_->activeModel()->gff().root());
        });
        tabs_->AddPage(editor_,type_==2027?"Creature":"Placeable",true);
        auto child=context_;child.validateOutputs=[this](const auto& paths,const auto*,const auto& inputs){validateHostOutputs(paths,inputs);};
        fields_=neogff::ui::createEditorPanel(tabs_,std::move(child));tabs_->AddPage(fields_,"All fields");box->Add(tabs_,1,wxEXPAND);SetSizer(box);
        setModuleMenus(fields_->takeMenus().release());
        Bind(wxEVT_MENU,[this](wxCommandEvent& event){editor_->applyPending();fields_->dispatch(event);refresh();});
        Bind(wxEVT_UPDATE_UI,[this](wxUpdateUIEvent& event){fields_->dispatch(event);});
        Bind(wxEVT_MENU_OPEN,[this](wxMenuEvent& event){fields_->dispatch(event);});
        fields_->Bind(neogff::ui::EVT_GFF_DOCUMENT_EDITED,[this](wxCommandEvent&){refresh();});
        fields_->Bind(neogff::ui::EVT_GFF_FIELD_SELECTED,[this](wxCommandEvent&){
            if(fields_->activeModel()!=selectedModel_)refresh();
        });
        tabs_->Bind(wxEVT_NOTEBOOK_PAGE_CHANGED,[this](wxBookCtrlEvent& event){if(event.GetEventObject()==tabs_) {editor_->applyPending();if(event.GetSelection()==0)refresh();}event.Skip();});
        game_->Bind(wxEVT_CHOICE,[this](wxCommandEvent&){refresh(true);});resources->Bind(wxEVT_BUTTON,[this](wxCommandEvent&){refresh(true);});
        editor_->clear();
    }
    ~ObjectBlueprints() override {DestroyChildren();}
    bool openFile(const std::filesystem::path& path) override {editor_->applyPending();const auto opened=fields_->openFile(path);if(opened) {refresh(true);tabs_->ChangeSelection(0);}return opened;}
    bool openResource(neoshared::ResourceDocument source) override {editor_->applyPending();scopes_[source.identity]=source.module.empty()?neoshared::bif::ModFolder::moduleName(source.sourcePath):source.module;const auto opened=fields_->openResource(std::move(source));if(opened) {refresh(true);tabs_->ChangeSelection(0);}return opened;}
    void resourceContextChanged() override {editor_->applyPending();refresh(true);}
    bool canClose() override {editor_->applyPending();return fields_->canClose();}
    void setAppearance(bool dark,double scale) override {fields_->setAppearance(dark,scale);wxui::applyTheme(editor_,dark);neoview::applyFontScale(editor_,scale);}
    std::vector<std::filesystem::path> openPaths() const override {auto paths=fields_->openPaths();const auto preview=editor_->openPaths();paths.insert(paths.end(),preview.begin(),preview.end());return paths;}
private:
    void refresh(bool resources=false) {
        const auto* model=fields_->activeModel();const auto identity=fields_->activeResourceIdentity();
        const bool changed=identity_!=identity||selectedModel_!=model;identity_=identity;selectedModel_=model;
        if(model&&model->loaded()&&model->fileType().compare(0,3,type_==2027?"UTC":"UTP")==0) {
            editor_->Enable();
            if(resources||changed) {
                const auto module=scopes_.find(identity);const auto scope=module==scopes_.end()?std::string{}:module->second;
                auto captured=context_.captureScopedResourceResolver(scope);const auto directory=model->filename().parent_path();
                auto local=directory.empty()?neomdl::ResourceResolver{}:neomdl::directoryResources(directory);
                const auto mod=context_.captureModFolder();
                editor_->open(*model->gff().root(),[captured=std::move(captured),local=std::move(local),mod,scope](const std::string& name,std::uint16_t type) {
                    if(mod&&mod->shadows(name,type,scope))return mod->resolve(name,type,scope,neomdl::resourceLoadJob());
                    if(local)if(auto source=local(name,type))return source;
                    return captured(name,type,{});
                },game_->GetSelection()==0?neomdl::model::GameVersion::Kotor1:neomdl::model::GameVersion::Kotor2);
            } else editor_->refresh(*model->gff().root());
        } else {editor_->clear();tabs_->ChangeSelection(1);}
    }
    std::uint16_t type_;
    neogff::ui::GFFEditorPanel* fields_{};
    ObjectEditor* editor_{};
    wxNotebook* tabs_{};wxChoice* game_{};
    std::string identity_;
    std::map<std::string,std::string> scopes_;
    const neogff::GffModel* selectedModel_{};
};
}
ObjectEditorPanel* createObjectEditorPanel(wxWindow* parent,neomodules::Context context,std::uint16_t type) {return new ObjectBlueprints(parent,std::move(context),type);}
}
