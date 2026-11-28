#pragma once

#include "ShopPreview.hpp"
#include "ItemPreview.hpp"
#include "PlaceableEditor.hpp"
#include "ObjectPalette.hpp"
#include <wx/dialog.h>
#include <functional>

class wxChoice;
class wxListCtrl;
class wxCheckBox;
class wxTextCtrl;
class wxStaticText;
class wxScrolledWindow;
class wxSplitterWindow;

namespace neotoolkit {

class ItemListView;

class BlueprintPanel final : public wxPanel {
public:
    enum class StockAction {Duplicate,Remove,Infinite};
    struct Actions {
        std::function<void(const std::string&,const std::string&,const std::string&,const std::string&)> edit;
        std::function<void(neoshared::ResourceDocument,const neomdl::ResourceResolver&)> addItem;
        std::function<void(StockAction,std::size_t,bool)> stock;
        std::function<void(std::size_t)> stockFields;
        std::function<void(const std::string&,std::uint16_t)> itemFields;
        std::function<void()> fields,save,package,undo,redo;
        std::string saveLabel="Save resources...";
        std::string sourceNote=" — module override; affects instances using this template. Save resources or Package MOD includes edits.";
    };
    BlueprintPanel(wxWindow* parent,Actions actions);
    void open(std::uint16_t type,neomdl::model::GameVersion game,const std::string& title,const neogff::GffStruct& root,
        neomdl::ResourceResolver resolver,std::shared_ptr<neomdl::GameResourceSession> session,
        std::shared_ptr<neoshared::bif::LooseArchiveCatalog> catalog,const std::filesystem::path& directory);
    void refresh(const neogff::GffStruct& root,neomdl::ResourceResolver resolver,bool itemsChanged=false);
    void clear();
    bool applyPending();
    void setSaveLabel(const std::string& label);
    void setHistory(bool undo,bool redo);
    void selectInventory(std::size_t index);
private:
    enum class Kind {Value,Flag,LocalizedText,LocalizedRef,StoreMode};
    struct Row {std::string field,label;Kind kind;wxWindow* control;std::string fieldType,defaultValue;};
    void buildProperties(std::uint16_t type);
    bool commit(std::size_t index);
    void selectStock(int index);
    void addItem();
    void loadPreview();
    Actions actions_;
    std::uint16_t type_{};
    neomdl::model::GameVersion game_{};
    const neogff::GffStruct* root_{};
    std::vector<Row> rows_;
    neomdl::ResourceResolver resolver_;
    std::shared_ptr<neomdl::GameResourceSession> session_;
    std::shared_ptr<neoshared::bif::LooseArchiveCatalog> catalog_;
    std::filesystem::path directory_;
    wxStaticText* source_{};wxStaticText* previewStatus_{};
    wxScrolledWindow* details_{};
    wxPanel* properties_{};
    wxPanel* stockPage_{};wxPanel* previewPage_{};
    ItemListView* stock_{};wxCheckBox* infinite_{};
    ShopPreview* preview_{};
    wxPanel* previews_{};ItemPreview* itemPreview_{};
    ObjectEditor* object_{};
    wxButton* add_{};wxButton* duplicate_{};wxButton* remove_{};wxButton* itemFields_{};wxButton* stockFields_{};
    wxButton* save_{};wxButton* undo_{};wxButton* redo_{};wxSplitterWindow* split_{};
};

class BlueprintEditor final : public wxDialog {
public:
    using Actions=BlueprintPanel::Actions;
    using StockAction=BlueprintPanel::StockAction;
    BlueprintEditor(wxWindow* parent,Actions actions);
    void open(std::uint16_t type,neomdl::model::GameVersion game,const std::string& title,const neogff::GffStruct& root,
        neomdl::ResourceResolver resolver,std::shared_ptr<neomdl::GameResourceSession> session,
        std::shared_ptr<neoshared::bif::LooseArchiveCatalog> catalog,const std::filesystem::path& directory);
    void refresh(const neogff::GffStruct& root,neomdl::ResourceResolver resolver,bool itemsChanged=false) {panel_->refresh(root,std::move(resolver),itemsChanged);}
    void clear() {panel_->clear();Hide();}
    bool applyPending() {return panel_->applyPending();}
    void setHistory(bool undo,bool redo) {panel_->setHistory(undo,redo);}
    void selectInventory(std::size_t index) {panel_->selectInventory(index);}
private:
    BlueprintPanel* panel_{};
};

}
