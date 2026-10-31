#pragma once

#include "NeoModulePanel.hpp"
#include <core/AreaLoadTables.hpp>
#include <core/ModelDocument.hpp>
#include <core/CreatureAppearance.hpp>
#include <gff/AppModel.hpp>
#include <wx/panel.h>
#include <functional>

class wxStaticText;
class wxChoice;
namespace neomdl::ui {class ModelCanvas;}

namespace neotoolkit {

class ObjectEditor final : public wxPanel {
public:
    using Edit=std::function<void(const std::string&,const std::string&,const std::string&,const std::string&)>;
    ObjectEditor(wxWindow* parent,std::uint16_t type,Edit edit);
    std::uint16_t type() const noexcept {return type_;}
    ~ObjectEditor() override;
    void open(const neogff::GffStruct& root,neomdl::ResourceResolver resolver,neomdl::model::GameVersion game);
    void refresh(const neogff::GffStruct& root);
    void refresh(const neogff::GffStruct& root,const neomdl::ResourceResolver& resolver) {tables_.resolver=resolver;refresh(root);}
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
    void updateCreaturePreview();
    Edit edit_;
    std::uint16_t type_;
    const neogff::GffStruct* root_{};
    neomdl::model::GameVersion game_{neomdl::model::GameVersion::Kotor2};
    neomdl::detail::AreaLoadTables tables_;
    std::vector<Row> rows_;
    std::shared_ptr<neomdl::ModelDocument> document_;
    std::vector<std::string> creatureKey_;
    std::optional<neomdl::CreatureAppearanceState> creatureState_;
    std::optional<neomdl::CreatureAppearance> creatureAppearance_;
    wxChoice* animationPicker_{};
    neomdl::ui::ModelCanvas* canvas_{};
    wxStaticText* status_{};
    std::string modelName_;
    std::optional<std::uint16_t> animation_;
};

class ObjectEditorPanel : public neomodules::Panel {
public:
    using Panel::Panel;
    virtual bool openFile(const std::filesystem::path& path)=0;
    virtual bool openResource(neoshared::ResourceDocument resource)=0;
};
ObjectEditorPanel* createObjectEditorPanel(wxWindow* parent,neomodules::Context context,std::uint16_t type);

// Materialize an omitted optional property only when the user edits it.
void editBlueprintField(neogff::GffModel& model,const std::string& path,const std::string& type,const std::string& value);

}
