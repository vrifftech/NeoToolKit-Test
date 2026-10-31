#pragma once

#include "NeoModulePanel.hpp"
#include <core/AreaLoadTables.hpp>
#include <core/ModelDocument.hpp>
#include <gff/AppModel.hpp>
#include <wx/panel.h>
#include <functional>

class wxStaticText;
namespace neomdl::ui {class ModelCanvas;}

namespace neotoolkit {

class PlaceableEditor final : public wxPanel {
public:
    using Edit=std::function<void(const std::string&,const std::string&,const std::string&,const std::string&)>;
    PlaceableEditor(wxWindow* parent,Edit edit);
    ~PlaceableEditor() override;
    void open(const neogff::GffStruct& root,neomdl::ResourceResolver resolver,neomdl::model::GameVersion game);
    void refresh(const neogff::GffStruct& root);
    void applyPending();
    void clear();
    std::vector<std::filesystem::path> openPaths() const;
private:
    enum class Kind {Text,Number,Flag,Choice,LocalizedText,LocalizedRef,Color};
    struct Row {
        std::string field,type,label;Kind kind;wxWindow* control;
        double initial{};bool k2{};std::vector<unsigned> values;bool edited{};
    };
    void commit(std::size_t index);
    void loadChoices();
    void updatePreview();
    Edit edit_;
    const neogff::GffStruct* root_{};
    neomdl::model::GameVersion game_{neomdl::model::GameVersion::Kotor2};
    neomdl::detail::AreaLoadTables tables_;
    std::vector<Row> rows_;
    std::unique_ptr<neomdl::ModelDocument> document_;
    neomdl::ui::ModelCanvas* canvas_{};
    wxStaticText* status_{};
    std::string modelName_;
    std::optional<std::uint16_t> animation_;
};

class PlaceableEditorPanel : public neomodules::Panel {
public:
    using Panel::Panel;
    virtual bool openFile(const std::filesystem::path& path)=0;
    virtual bool openResource(neoshared::ResourceDocument resource)=0;
};
PlaceableEditorPanel* createPlaceableEditorPanel(wxWindow* parent,neomodules::Context context);

// Materialize an omitted optional property only when the user edits it.
void editPlaceableField(neogff::GffModel& model,const std::string& path,const std::string& type,const std::string& value);

}
