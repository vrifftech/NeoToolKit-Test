#pragma once
#include "wx/BrowserPanel.hpp"
#include "wx/EditorPanel.hpp"
#include "wx/SSFEditorPanel.hpp"
#include "wx/TLKEditorPanel.hpp"
#include "wx/GFFEditorPanel.hpp"
#include "wx/DLGEditorPanel.hpp"
#include "wx/JRLEditorPanel.hpp"
#include "wx/ERFEditorPanel.hpp"
#include "wx/TextureEditorPanel.hpp"
#include "EditorRouting.hpp"
#include "NeoSettings.hpp"
#include <wx/splitter.h>
#include <wx/aui/auibook.h>
#include <array>
#include <memory>
#include <vector>
namespace neotoolkit {
class WorkspaceFrame final : public wxFrame {
public:
    WorkspaceFrame();
    ~WorkspaceFrame() override;
    void openPath(const std::filesystem::path& path, const std::string& editor={});
    bool openResource(neoshared::ResourceDocument resource, const std::string& editor={});
    neobif::ui::BrowserPanel& browser() {return *browser_;}
    neo2da::ui::EditorPanel& tables() {ensureEditor(EditorKind::Tables);return *tables_;}
    neossf::ui::SSFEditorPanel& soundsets() {ensureEditor(EditorKind::Soundsets);return *soundsets_;}
    neotlk::ui::TLKEditorPanel& talkTables() {ensureEditor(EditorKind::TalkTables);return *talkTables_;}
    neogff::ui::GFFEditorPanel& structured() {ensureEditor(EditorKind::Gff);return *structured_;}
    neodlg::ui::DLGEditorPanel& dialogues() {ensureEditor(EditorKind::Dialogues);return *dialogues_;}
    neojrl::ui::JRLEditorPanel& journals() {ensureEditor(EditorKind::Journals);return *journals_;}
    neotpc::ui::TextureEditorPanel& textures() {ensureEditor(EditorKind::Textures);return *textures_;}
    neoerf::ui::ERFEditorPanel& archives() {ensureEditor(EditorKind::Archives);return *archives_;}
    std::size_t activeEditorIndex() const {return activeEditor_;}
    void selectEditor(std::size_t index);
private:
    bool openSnapshot(neoshared::ResourceDocument resource,const std::string& editor,bool protectSession);
    struct Menu {wxString title;std::unique_ptr<wxMenu> menu;};
    void buildMenus();
    void captureEditorMenus(std::size_t index);
    void detachActiveMenus();
    void switchMenus(std::size_t index);
    void showEditor(EditorKind kind);
    bool closeEditorPage(int page);
    void showBrowserOnly();
    void setExplorerVisible(bool visible);
    neomodules::Panel* ensureEditor(EditorKind kind);
    void destroyEditor(EditorKind kind);
    neomodules::Panel* panelFor(EditorKind kind) const;
    int editorPage(EditorKind kind) const;
    std::size_t editorForPage(int page) const;
    void applyAppearance();
    void checkOutput(const std::filesystem::path& path,const neomodules::Panel* owner) const;
    std::vector<neomodules::Panel*> panels() const;
    neobif::ui::BrowserPanel* browser_{};
    neo2da::ui::EditorPanel* tables_{};
    neossf::ui::SSFEditorPanel* soundsets_{};
    neotlk::ui::TLKEditorPanel* talkTables_{};
    neogff::ui::GFFEditorPanel* structured_{};
    neodlg::ui::DLGEditorPanel* dialogues_{};
    neojrl::ui::JRLEditorPanel* journals_{};
    neotpc::ui::TextureEditorPanel* textures_{};
    neoerf::ui::ERFEditorPanel* archives_{};
    wxSplitterWindow* splitter_{};
    wxAuiNotebook* editors_{};
    std::array<std::vector<Menu>,kEditorCount> menus_;
    std::array<std::vector<std::unique_ptr<wxMenu>>,kEditorCount> hiddenMenus_;
    std::size_t activeEditor_=kEditorCount;
    std::size_t attachedMenuCount_=0;
    bool menusReady_=false;
    std::vector<std::filesystem::path> protectedInputs_;
    neosettings::AppSettings settings_{"NeoToolKit-Test"};
    bool dark_{};
    double fontScale_=1.0;
};
}
