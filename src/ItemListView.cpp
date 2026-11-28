#include "ItemListView.hpp"
#include "NeoWxUi.hpp"
#include <wx/image.h>
#include <wx/imaglist.h>
#include <algorithm>

namespace neotoolkit {

ItemListView::ItemListView(wxWindow* parent,bool stock)
    :wxListCtrl(parent,wxID_ANY,wxDefaultPosition,wxDefaultSize,wxLC_REPORT|wxLC_SINGLE_SEL),stock_(stock) {
    images_=new wxImageList(FromDIP(48),FromDIP(48),true);AssignImageList(images_,wxIMAGE_LIST_SMALL);
    AppendColumn("Item",wxLIST_FORMAT_LEFT,FromDIP(240));
    AppendColumn(stock?"Stock":"Stack",wxLIST_FORMAT_RIGHT,FromDIP(80));
    if(!stock)AppendColumn("Template",wxLIST_FORMAT_LEFT,FromDIP(140));
    Bind(wxEVT_SIZE,[this](wxSizeEvent& event){
        SetColumnWidth(0,std::max(FromDIP(150),GetClientSize().x-GetColumnWidth(1)-(stock_?0:GetColumnWidth(2))-FromDIP(8)));event.Skip();
    });
}

void ItemListView::setResources(neomdl::ResourceResolver resolver,const std::filesystem::path& talkTable,
    neomdl::model::GameVersion game) {
    resolver_=std::move(resolver);reader_.reset();tlk_.clear();textures_.reset();resourceError_.clear();
    items_.clear();icons_.clear();images_->RemoveAll();
    if(talkTable.empty()) {resourceError_="Choose Game resources to load item names, icons and descriptions.";return;}
    try {
        tlk_.load(talkTable);reader_=std::make_unique<ItemReader>(resolver_,tlk_,game==neomdl::model::GameVersion::Kotor2);
        textures_=neomdl::ModelDocument::emptySceneDocument("inventory-icons","Inventory icons",game);
    } catch(const std::exception& e) {resourceError_=e.what();}
}

void ItemListView::clearItems() {DeleteAllItems();items_.clear();}

const ItemListView::Item& ItemListView::read(const std::string& resref) {
    const auto key=neomdl::resourceKey(resref);
    auto [found,inserted]=items_.try_emplace(key);
    auto& item=found->second;
    if(!inserted)return item;
    try {
        if(!resourceError_.empty())throw std::runtime_error(resourceError_);
        const auto source=resolver_(key,2025);
        if(!source)throw std::runtime_error("Item template was not found in the selected resources.");
        neogff::GffFile file;file.LoadBytes(source->bytes);item.data=reader_->read(*file.root());
        auto icon=icons_.find(item.data.icon);
        if(icon==icons_.end()) {
            std::string error;
            if(!textures_->texture(item.data.icon)&&!textures_->loadRuntimeTexture(item.data.icon,resolver_,error))throw std::runtime_error(error);
            const auto& pixels=textures_->texture(item.data.icon)->layers.front();
            wxImage image(int(pixels.width),int(pixels.height));image.InitAlpha();
            for(std::size_t i=0;i<pixels.rgba.size()/4;++i) {
                for(unsigned c=0;c<3;++c)image.GetData()[3*i+c]=pixels.rgba[4*i+c];
                image.GetAlpha()[i]=pixels.rgba[4*i+3];
            }
            icon=icons_.emplace(item.data.icon,images_->Add(wxBitmap(image.Scale(FromDIP(48),FromDIP(48),wxIMAGE_QUALITY_HIGH)))).first;
        }
        item.image=icon->second;
    } catch(const std::exception& e) {item.error=resref+".uti: "+e.what();}
    return item;
}

const std::string& ItemListView::itemName(const std::string& resref) {return read(resref).data.name;}

long ItemListView::appendItem(const std::string& resref,bool infinite) {
    const auto& item=read(resref);
    const auto row=InsertItem(GetItemCount(),wxui::toWx(item.error.empty()?item.data.name:"[Item error] "+resref),item.image);
    SetItem(row,1,infinite?"Infinite":item.error.empty()?wxString::Format("%u",item.data.stack):wxString{});
    if(!stock_)SetItem(row,2,wxui::toWx(resref));
    return row;
}

std::string ItemListView::itemDetails(const std::string& resref,bool infinite) {
    const auto& item=read(resref);
    if(!item.error.empty())return item.error;
    return item.data.name+"\n"+resref+".uti\n"+(stock_?"Stock: ":"Stack: ")+(infinite?"Infinite":std::to_string(item.data.stack))+
        "   |   Base cost: "+std::to_string(item.data.cost)+(item.data.plot?"   |   Plot item":"")+"\n\n"+item.data.description;
}

}
