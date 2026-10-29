#pragma once

#include "core/GameResourceSession.hpp"
#include <neoshared/bif/LooseArchiveCatalog.hpp>
#include <wx/panel.h>
#include <wx/dnd.h>
#include <array>

class wxChoice;
class wxListCtrl;
class wxSearchCtrl;
class wxStaticText;
class wxTextCtrl;
class wxToggleButton;
class wxButton;

namespace neotoolkit {

struct ObjectKind {
    std::uint16_t type;
    const char* label;
    const char* list;
    std::uint32_t structure;
    bool shortCoordinates;
    bool bearing;
    const char* reference{"TemplateResRef"};
};
inline constexpr std::array<ObjectKind,9> kObjectKinds{{
    {2044,"Placeables","Placeable List",9,true,true},
    {2027,"Creatures","Creature List",4,false,false},
    {2042,"Doors","Door List",8,true,true},
    {2058,"Waypoints","WaypointList",5,false,false},
    {2035,"Sounds","SoundList",6,false,false},
    {2032,"Triggers","TriggerList",1,false,false},
    {2025,"Items","List",0,false,false},
    {2040,"Encounters","Encounter List",7,false,false},
    {2051,"Stores","StoreList",11,false,false,"ResRef"}
}};

struct ObjectPlacementOptions {
    float heading{};
    std::optional<float> planeZ;
    float zOffset{};
    float triggerSize{};
};

wxDECLARE_EVENT(EVT_OBJECT_PLACEMENT_MODE,wxCommandEvent);
wxDECLARE_EVENT(EVT_TRIGGER_POLYGON_REQUESTED,wxCommandEvent);
wxDECLARE_EVENT(EVT_BLUEPRINT_SELECTION_CHANGED,wxCommandEvent);

class ObjectPalette final : public wxPanel {
public:
    explicit ObjectPalette(wxWindow* parent,bool inventory=false);
    void setResources(std::shared_ptr<neomdl::GameResourceSession> session,
        std::shared_ptr<neoshared::bif::LooseArchiveCatalog> catalog,std::filesystem::path directory);
    void clear();
    void setPlacementEnabled(bool enabled);
    bool placing() const;
    void stopPlacing();
    neoshared::ResourceDocument selectedBlueprint() const;
    neomdl::ResourceResolver selectedResources() const;
    ObjectPlacementOptions placementOptions() const;
    std::optional<std::uint16_t> selectedType() const;
    void installDropTarget(wxWindow* viewport,std::function<bool(wxPoint)> drop,std::function<bool()> ready);
private:
    void selectScope();
    void rebuild();
    void filter();
    void placementModeChanged();
    void selectionChanged();
    wxChoice* scope_{};wxChoice* kind_{};wxChoice* surface_{};
    wxListCtrl* entries_{};wxSearchCtrl* search_{};wxToggleButton* place_{};
    wxTextCtrl* heading_{};wxTextCtrl* height_{};wxTextCtrl* triggerSize_{};
    wxStaticText* heightLabel_{};wxStaticText* summary_{};
    wxButton* draw_{};
    std::shared_ptr<neomdl::GameResourceSession> active_,selected_;
    std::shared_ptr<neoshared::bif::LooseArchiveCatalog> catalog_;
    std::filesystem::path directory_;
    std::vector<neomdl::ModuleScope> scopes_;
    std::vector<neomdl::GameResourceEntry> blueprints_;
    bool placementEnabled_{};
    bool inventory_{};
};

}
