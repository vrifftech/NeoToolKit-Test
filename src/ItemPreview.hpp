#pragma once
#include "ItemData.hpp"
#include "core/ModelDocument.hpp"
#include <wx/panel.h>
#include <functional>

class wxChoice;
class wxStaticBitmap;
class wxStaticText;
class wxTextCtrl;
namespace neomdl::ui {class ModelCanvas;}
namespace neotoolkit {
class ItemPreview final : public wxPanel {
public:
    ItemPreview(wxWindow*,std::function<const neogff::GffStruct&()> item,std::function<neomdl::ResourceResolver()> resources);
    ~ItemPreview() override;
    void setContext(const std::filesystem::path& talkTable,neomdl::model::GameVersion game);
    void setTalkTable(const std::filesystem::path& path) {talkTable_=path;}
    void refreshItem();
    void clearItem();
    void clear();
    std::vector<std::filesystem::path> openPaths() const;
private:
    void load();
    void update();
    void error(const std::string&);
    std::function<const neogff::GffStruct&()> item_;
    std::function<neomdl::ResourceResolver()> resources_;
    neomdl::ResourceResolver resolver_;
    neotlk::TlkLookup tlk_;
    std::unique_ptr<ItemReader> reader_;
    std::unique_ptr<neomdl::ModelDocument> assets_;
    std::shared_ptr<neomdl::ModelDocument> visual_;
    std::filesystem::path talkTable_;
    std::string modelKey_,iconKey_;
    wxChoice* game_{};wxChoice* modelMode_{};
    wxStaticBitmap* icon_{};wxStaticText* title_{};wxStaticText* status_{};
    wxTextCtrl* details_{};
    neomdl::ui::ModelCanvas* canvas_{};
};
}
