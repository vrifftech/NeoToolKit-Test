#pragma once

#include "ItemData.hpp"
#include "core/ModelDocument.hpp"
#include <wx/listctrl.h>

class wxImageList;

namespace neotoolkit {

class ItemListView final : public wxListCtrl {
public:
    explicit ItemListView(wxWindow* parent,bool stock);
    void setResources(neomdl::ResourceResolver resolver,const std::filesystem::path& talkTable,
        neomdl::model::GameVersion game);
    void clearItems();
    const std::string& itemName(const std::string& resref);
    long appendItem(const std::string& resref,bool infinite=false);
    std::string itemDetails(const std::string& resref,bool infinite=false);
private:
    struct Item {ItemData data;int image{-1};std::string error;};
    const Item& read(const std::string& resref);
    neomdl::ResourceResolver resolver_;
    neotlk::TlkLookup tlk_;
    std::unique_ptr<ItemReader> reader_;
    std::unique_ptr<neomdl::ModelDocument> textures_;
    std::map<std::string,Item> items_;
    std::map<std::string,int> icons_;
    std::string resourceError_;
    wxImageList* images_{};
    bool stock_{};
};

}
