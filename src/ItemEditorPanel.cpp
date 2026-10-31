#include "ItemEditorPanel.hpp"
#include "ItemPreview.hpp"
#include "wx/GFFEditorPanel.hpp"
#include "NeoViewState.hpp"
#include <wx/sizer.h>
#include <wx/splitter.h>

namespace neotoolkit {
namespace {
class ItemEditor final : public ItemEditorPanel {
public:
    ItemEditor(wxWindow* parent,neomodules::Context context):ItemEditorPanel(parent,std::move(context)) {
        auto* split=new wxSplitterWindow(this,wxID_ANY,wxDefaultPosition,wxDefaultSize,wxSP_LIVE_UPDATE);
        auto child=context_;child.validateOutputs=[this](const auto& paths,const auto*,const auto& inputs){validateHostOutputs(paths,inputs);};
        fields_=neogff::ui::createEditorPanel(split,std::move(child));
        preview_=new ItemPreview(split,[this]()->const auto&{return *fields_->activeModel()->gff().root();},[this]{auto captured=context_.captureResourceResolver();return [captured=std::move(captured)](const std::string& name,std::uint16_t type){return captured(name,type,{});};});
        split->SetMinimumPaneSize(FromDIP(260));split->SplitVertically(fields_,preview_,-FromDIP(480));split->SetSashGravity(.6);auto* box=new wxBoxSizer(wxVERTICAL);box->Add(split,1,wxEXPAND);SetSizer(box);
        setModuleMenus(fields_->takeMenus().release());
        Bind(wxEVT_MENU,[this](wxCommandEvent& event){fields_->dispatch(event);});
        Bind(wxEVT_UPDATE_UI,[this](wxUpdateUIEvent& event){fields_->dispatch(event);});
        Bind(wxEVT_MENU_OPEN,[this](wxMenuEvent& event){fields_->dispatch(event);});
        fields_->Bind(neogff::ui::EVT_GFF_DOCUMENT_EDITED,[this](wxCommandEvent&){refresh();});
        fields_->Bind(neogff::ui::EVT_GFF_DOCUMENT_SELECTED,[this](wxCommandEvent&){const auto* model=fields_->activeModel();if(model)preview_->setTalkTable(model->tlk().filename());refresh();});
        preview_->Disable();
    }
    ~ItemEditor() override {DestroyChildren();}
    bool openFile(const std::filesystem::path& path) override {return fields_->openFile(path);}
    bool openResource(neoshared::ResourceDocument source) override {return fields_->openResource(std::move(source));}
    bool canClose() override {return fields_->canClose();}
    void setAppearance(bool dark,double scale) override {fields_->setAppearance(dark,scale);wxui::applyTheme(preview_,dark);neoview::applyFontScale(preview_,scale);}
    std::vector<std::filesystem::path> openPaths() const override {auto paths=fields_->openPaths();const auto preview=preview_->openPaths();paths.insert(paths.end(),preview.begin(),preview.end());return paths;}
private:
    void refresh() {
        const auto* model=fields_->activeModel();const bool item=model&&model->loaded()&&model->gff().filetype().compare(0,3,"UTI")==0;
        preview_->Enable(item);
        if(item)preview_->refreshItem();else preview_->clearItem();
    }
    neogff::ui::GFFEditorPanel* fields_{};
    ItemPreview* preview_{};
};
}
ItemEditorPanel* createItemEditorPanel(wxWindow* parent,neomodules::Context context) {return new ItemEditor(parent,std::move(context));}
}
