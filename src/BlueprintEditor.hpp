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
class wxComboBox;
class wxStaticText;
class wxScrolledWindow;
class wxSplitterWindow;
class wxNotebook;

namespace neotoolkit {

class BlueprintEditor final : public wxDialog {
public:
    enum class StockAction {Duplicate,Remove,Infinite};
    struct Actions {
        std::function<void(const std::string&,const std::string&,const std::string&,const std::string&)> edit;
        std::function<void(neoshared::ResourceDocument,const neomdl::ResourceResolver&)> addItem;
        std::function<void(StockAction,std::size_t,bool)> stock;
        std::function<void(const std::string&,std::uint16_t)> itemFields;
        std::function<void()> fields,save,package,undo,redo;
    };
    BlueprintEditor(wxWindow* parent,Actions actions);
    void open(std::uint16_t type,neomdl::model::GameVersion game,const std::string& title,const neogff::GffStruct& root,
        neomdl::ResourceResolver resolver,std::shared_ptr<neomdl::GameResourceSession> session,
        std::shared_ptr<neoshared::bif::LooseArchiveCatalog> catalog,const std::filesystem::path& directory);
    void refresh(const neogff::GffStruct& root,neomdl::ResourceResolver resolver,bool itemsChanged=false);
    void clear();
    void applyPending() {if(object_)object_->applyPending();}
    void setHistory(bool undo,bool redo);
    void selectInventory(std::size_t index);
private:
    enum class Kind {Value,Flag,LocalizedText,LocalizedRef,StoreMode};
    struct Row {std::string field,label;Kind kind;wxWindow* control;};
    void buildProperties(std::uint16_t type);
    void commit(std::size_t index);
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
    wxScrolledWindow* properties_{};
    wxPanel* stockPage_{};wxPanel* previewPage_{};
    wxListCtrl* stock_{};wxCheckBox* infinite_{};
    wxComboBox* layout_{};
    ShopPreview* preview_{};
    wxNotebook* previews_{};ItemPreview* itemPreview_{};
    ObjectEditor* object_{};
    wxButton* add_{};wxButton* duplicate_{};wxButton* remove_{};wxButton* itemFields_{};wxButton* loadPreview_{};
    wxButton* undo_{};wxButton* redo_{};wxSplitterWindow* split_{};
};

}
