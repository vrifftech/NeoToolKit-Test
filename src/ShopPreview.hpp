#pragma once

#include "core/ModelDocument.hpp"
#include <GuiRenderer.hpp>
#include <gff/GFFFile.hpp>
#include <neotlk/TlkLookup.hpp>
#include <wx/panel.h>
#include <wx/bitmap.h>
#include <wx/geometry.h>
#include <functional>
#include <map>

class wxGraphicsContext;

namespace neotoolkit {

class ShopPreview final : public wxPanel {
public:
    explicit ShopPreview(wxWindow* parent,std::function<void(std::size_t)> selected);
    void load(const std::string& layout,const neomdl::ResourceResolver& resolver,
        const std::filesystem::path& talkTable,neomdl::model::GameVersion game);
    void setStore(const neogff::GffStruct& store,const neomdl::ResourceResolver& resolver,int selected);
    void selectInventory(int index) {selection_=index;Refresh();}
    void clear();
    bool loaded() const {return bool(textures_);}
    std::string sources() const;
private:
    struct Item {std::size_t source{};std::string name,description,icon;unsigned stack{};int group{};bool infinite{},plot{};};
    const neoshared::texture::TextureData& texture(const std::string& name);
    void stamp(wxGraphicsContext& dc,const std::string& name,const wxRect2DDouble& rect,const neomdl::model::Vec3& color,double alpha=1,int turn=0,bool flipX=false,bool flipY=false);
    wxRect2DDouble borderInner(const neogff::GffStruct& record,const std::string& field,wxRect2DDouble rect);
    wxRect2DDouble border(wxGraphicsContext& dc,const neogff::GffStruct& record,const std::string& field,wxRect2DDouble rect,std::optional<neomdl::model::Vec3> tint={});
    void scrollbar(wxGraphicsContext& dc,const neogff::GffStruct& style,wxRect2DDouble rect,int count,int top,int visible);
    void text(wxGraphicsContext& dc,const neogff::GffStruct& style,const std::string& value,wxRect2DDouble rect,double scale,std::optional<neomdl::model::Vec3> tint={});
    void paint(wxPaintEvent& event);
    std::unique_ptr<neomdl::ModelDocument> textures_;
    neomdl::ResourceResolver resolver_;
    neogff::GffFile layout_;
    neotlk::TlkLookup tlk_;
    neogui::GuiRenderer renderer_;
    std::vector<Item> items_;
    std::vector<std::pair<wxRect2DDouble,std::size_t>> hits_;
    std::vector<std::string> sources_;
    neomdl::model::GameVersion game_{};
    std::function<void(std::size_t)> selected_;
    int selection_{-1},firstRow_{};
    double scale_{1},offsetX_{},offsetY_{},fontScale_{1};
};

}
