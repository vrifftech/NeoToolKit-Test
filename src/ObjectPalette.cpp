#include "ObjectPalette.hpp"
#include "NeoWxUi.hpp"
#include <gff/Common.hpp>
#include <wx/choice.h>
#include <wx/button.h>
#include <wx/dataobj.h>
#include <wx/listctrl.h>
#include <wx/srchctrl.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/tglbtn.h>
#include <wx/sizer.h>
#include <algorithm>
#include <map>

namespace neotoolkit {
wxDEFINE_EVENT(EVT_OBJECT_PLACEMENT_MODE,wxCommandEvent);
wxDEFINE_EVENT(EVT_TRIGGER_POLYGON_REQUESTED,wxCommandEvent);
wxDEFINE_EVENT(EVT_BLUEPRINT_SELECTION_CHANGED,wxCommandEvent);
namespace {
wxDataFormat blueprintFormat() {return wxDataFormat("application/x-neotoolkit-blueprint");}
std::vector<std::uint16_t> objectTypes() {
    std::vector<std::uint16_t> types;for(const auto& kind:kObjectKinds)types.push_back(kind.type);return types;
}
const ObjectKind& kindFor(std::uint16_t type) {
    return *std::find_if(kObjectKinds.begin(),kObjectKinds.end(),[type](const auto& kind){return kind.type==type;});
}
class BlueprintDropTarget final : public wxDropTarget {
public:
    BlueprintDropTarget(std::function<bool(wxPoint)> drop,std::function<bool()> ready)
        : wxDropTarget(new wxCustomDataObject(blueprintFormat())),drop_(std::move(drop)),ready_(std::move(ready)) {}
    wxDragResult OnDragOver(wxCoord,wxCoord,wxDragResult) override {return ready_()?wxDragCopy:wxDragNone;}
    bool OnDrop(wxCoord,wxCoord) override {return ready_();}
    wxDragResult OnData(wxCoord x,wxCoord y,wxDragResult) override {
        return GetData()&&drop_({x,y})?wxDragCopy:wxDragNone;
    }
private:
    std::function<bool(wxPoint)> drop_;
    std::function<bool()> ready_;
};
}

ObjectPalette::ObjectPalette(wxWindow* parent,bool inventory):wxPanel(parent),inventory_(inventory) {
    auto* box=new wxBoxSizer(wxVERTICAL);
    scope_=new wxChoice(this,wxID_ANY);box->Add(scope_,0,wxEXPAND|wxALL,5);
    kind_=new wxChoice(this,wxID_ANY);kind_->Append("All object types");
    for(const auto& kind:kObjectKinds)kind_->Append(kind.label);
    kind_->SetSelection(0);box->Add(kind_,0,wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM,5);
    kind_->Show(!inventory_);
    search_=new wxSearchCtrl(this,wxID_ANY);search_->SetDescriptiveText("Search template or source...");
    box->Add(search_,0,wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM,5);
    entries_=new wxListCtrl(this,wxID_ANY,wxDefaultPosition,wxDefaultSize,wxLC_REPORT|wxLC_SINGLE_SEL);
    entries_->AppendColumn("Template",wxLIST_FORMAT_LEFT,FromDIP(145));
    entries_->AppendColumn("Type",wxLIST_FORMAT_LEFT,FromDIP(90));
    entries_->AppendColumn("Source",wxLIST_FORMAT_LEFT,FromDIP(250));
    box->Add(entries_,1,wxEXPAND|wxLEFT|wxRIGHT,5);
    summary_=new wxStaticText(this,wxID_ANY,"Choose Game resources to browse templates.");
    box->Add(summary_,0,wxEXPAND|wxALL,5);
    auto* placement=new wxPanel(this);auto* placementBox=new wxBoxSizer(wxVERTICAL);
    surface_=new wxChoice(placement,wxID_ANY);surface_->Append("Place on surface");surface_->Append("Place on height plane");surface_->SetSelection(0);
    placementBox->Add(surface_,0,wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM,5);
    auto* options=new wxFlexGridSizer(2,4,5);options->AddGrowableCol(1);
    options->Add(new wxStaticText(placement,wxID_ANY,"Heading (degrees)"),0,wxALIGN_CENTER_VERTICAL);
    heading_=new wxTextCtrl(placement,wxID_ANY,"0");options->Add(heading_,1,wxEXPAND);
    heightLabel_=new wxStaticText(placement,wxID_ANY,"Z offset");options->Add(heightLabel_,0,wxALIGN_CENTER_VERTICAL);
    height_=new wxTextCtrl(placement,wxID_ANY,"0");options->Add(height_,1,wxEXPAND);
    options->Add(new wxStaticText(placement,wxID_ANY,"Footprint size"),0,wxALIGN_CENTER_VERTICAL);
    triggerSize_=new wxTextCtrl(placement,wxID_ANY,"2");options->Add(triggerSize_,1,wxEXPAND);
    placementBox->Add(options,0,wxEXPAND|wxALL,5);
    place_=new wxToggleButton(placement,wxID_ANY,"Click map to place");placementBox->Add(place_,0,wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM,5);
    draw_=new wxButton(placement,wxID_ANY,"Draw polygon footprint");placementBox->Add(draw_,0,wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM,5);
    placement->SetSizer(placementBox);box->Add(placement,0,wxEXPAND);placement->Show(!inventory_);
    draw_->Bind(wxEVT_BUTTON,[this](wxCommandEvent&){
        wxCommandEvent event(EVT_TRIGGER_POLYGON_REQUESTED,GetId());event.SetEventObject(this);ProcessWindowEvent(event);
    });
    auto* hint=new wxStaticText(this,wxID_ANY,inventory_?"Choose an item template, then Add item.":"Drag a template onto the map, or enable click placement. Esc cancels.");
    hint->Wrap(FromDIP(220));box->Add(hint,0,wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM,5);
    SetSizer(box);
    scope_->Bind(wxEVT_CHOICE,[this](wxCommandEvent&){
        try {selectScope();}catch(const std::exception& e){wxui::showError(this,e);}
    });
    kind_->Bind(wxEVT_CHOICE,[this](wxCommandEvent&){filter();});
    search_->Bind(wxEVT_TEXT,[this](wxCommandEvent&){filter();});
    entries_->Bind(wxEVT_LIST_ITEM_SELECTED,[this](wxListEvent& event){setPlacementEnabled(placementEnabled_);selectionChanged();event.Skip();});
    entries_->Bind(wxEVT_LIST_ITEM_DESELECTED,[this](wxListEvent& event){selectionChanged();event.Skip();});
    entries_->Bind(wxEVT_LIST_BEGIN_DRAG,[this](wxListEvent&){
        if(placementEnabled_&&!inventory_) {
            wxCustomDataObject data(blueprintFormat());const char marker=1;data.SetData(1,&marker);
            wxDropSource source(entries_);source.SetData(data);source.DoDragDrop(wxDrag_CopyOnly);
        }
    });
    surface_->Bind(wxEVT_CHOICE,[this](wxCommandEvent&){heightLabel_->SetLabel(surface_->GetSelection()==0?"Z offset":"Plane Z");Layout();});
    place_->Bind(wxEVT_TOGGLEBUTTON,[this](wxCommandEvent&){placementModeChanged();});
    clear();
}

void ObjectPalette::setResources(std::shared_ptr<neomdl::GameResourceSession> session,
    std::shared_ptr<neoshared::bif::LooseArchiveCatalog> catalog,std::filesystem::path directory) {
    active_=std::move(session);selected_=active_;catalog_=std::move(catalog);directory_=std::move(directory);
    scopes_.clear();scope_->Clear();scope_->Append("Current module + game files");
    for(const auto& module:active_->modules())if(module.name!=active_->moduleScope()->name) {
        scopes_.push_back(module);scope_->Append(wxui::toWx(module.name));
    }
    scope_->SetSelection(0);scope_->Enable();kind_->Enable();search_->Enable();rebuild();
}
void ObjectPalette::clear() {
    stopPlacing();active_.reset();selected_.reset();blueprints_.clear();scopes_.clear();
    entries_->DeleteAllItems();scope_->Clear();scope_->Disable();kind_->Disable();search_->Disable();
    summary_->SetLabel("Choose Game resources to browse templates.");setPlacementEnabled(false);
}
void ObjectPalette::selectScope() {
    blueprints_.clear();filter();
    if(scope_->GetSelection()==0)selected_=active_;
    else {
        auto session=std::make_shared<neomdl::GameResourceSession>();std::string error;
        if(!session->openModule(*active_,scopes_.at(scope_->GetSelection()-1),error))throw std::runtime_error(error);
        selected_=std::move(session);
    }
    rebuild();
}
void ObjectPalette::rebuild() {
    std::map<std::pair<std::string,std::uint16_t>,neomdl::GameResourceEntry> entries;
    const auto types=inventory_?std::vector<std::uint16_t>{2025}:objectTypes();
    if(scope_->GetSelection()==0) {
        for(const auto& resource:catalog_->resources())if(std::find(types.begin(),types.end(),resource.type)!=types.end())
            entries.emplace(std::make_pair(neomdl::resourceKey(resource.resref),resource.type),neomdl::GameResourceEntry{
                neomdl::resourceKey(resource.resref),resource.type,neoshared::pathToUtf8(catalog_->archives().at(resource.archiveIndex).resolvedPath)});
        if(!directory_.empty())for(const auto& file:std::filesystem::directory_iterator(directory_)) {
            const auto type=neoshared::bif::resourceTypeFromExtension(neoshared::pathToUtf8(file.path().extension()),neoshared::bif::GameProfile::KotOR);
            if(type&&std::find(types.begin(),types.end(),*type)!=types.end()) {
                const auto name=neomdl::resourceKey(neoshared::pathToUtf8(file.path().stem()));
                entries.emplace(std::make_pair(name,*type),neomdl::GameResourceEntry{name,*type,neoshared::pathToUtf8(file.path())});
            }
        }
    }
    for(auto& entry:selected_->resources(types))entries.emplace(std::make_pair(entry.resref,entry.type),std::move(entry));
    blueprints_.clear();for(auto& [key,entry]:entries)blueprints_.push_back(std::move(entry));filter();
}
void ObjectPalette::filter() {
    stopPlacing();entries_->Freeze();entries_->DeleteAllItems();
    const auto term=search_->GetValue().Lower();const auto kind=kind_->GetSelection();
    for(std::size_t i=0;i<blueprints_.size();++i) {
        const auto& entry=blueprints_[i];
        if(kind>0&&entry.type!=kObjectKinds.at(kind-1).type)continue;
        if(!term.empty()&&!wxui::toWx(entry.resref+"."+neomdl::resourceExtension(entry.type)+" "+kindFor(entry.type).label+" "+entry.source).Lower().Contains(term))continue;
        const auto row=entries_->InsertItem(entries_->GetItemCount(),wxui::toWx(entry.resref));
        entries_->SetItem(row,1,kindFor(entry.type).label);entries_->SetItem(row,2,wxui::toWx(entry.source));
        entries_->SetItemData(row,i);
    }
    entries_->Thaw();summary_->SetLabel(wxString::Format("%ld templates",entries_->GetItemCount()));
    setPlacementEnabled(placementEnabled_);
    selectionChanged();
}
void ObjectPalette::selectionChanged() {wxCommandEvent event(EVT_BLUEPRINT_SELECTION_CHANGED,GetId());event.SetEventObject(this);ProcessWindowEvent(event);}
neomdl::ResourceResolver ObjectPalette::selectedResources() const {
    auto resolve=selected_->resolver();
    if(scope_->GetSelection()!=0)return resolve;
    auto local=directory_.empty()?neomdl::ResourceResolver{}:neomdl::directoryResources(directory_);
    return [catalog=catalog_,local,resolve](const std::string& name,std::uint16_t type)->std::optional<neoshared::ResourceDocument> {
        for(const auto& entry:catalog->resources())if(entry.type==type&&neomdl::resourceKey(entry.resref)==neomdl::resourceKey(name)) {
            const auto& archive=catalog->archives().at(entry.archiveIndex);
            neoshared::ResourceDocument source{neoshared::pathToUtf8(archive.resolvedPath)+"#"+entry.fileName(),
                entry.fileName(),neoshared::pathToUtf8(archive.resolvedPath),entry.type,{}, {archive.resolvedPath}};
            std::string error;if(!catalog->readResource(entry.index,source.bytes,error))throw std::runtime_error(error);return source;
        }
        if(local)if(auto source=local(name,type))return source;
        return resolve(name,type);
    };
}
neoshared::ResourceDocument ObjectPalette::selectedBlueprint() const {
    const auto row=entries_->GetNextItem(-1,wxLIST_NEXT_ALL,wxLIST_STATE_SELECTED);
    const auto& entry=blueprints_.at(entries_->GetItemData(row));return selectedResources()(entry.resref,entry.type).value();
}
ObjectPlacementOptions ObjectPalette::placementOptions() const {
    ObjectPlacementOptions options;
    options.heading=neogff::ParseFloatDecimal(wxui::toStd(heading_->GetValue()));
    const auto height=neogff::ParseFloatDecimal(wxui::toStd(height_->GetValue()));
    if(surface_->GetSelection()==0)options.zOffset=height;else options.planeZ=height;
    if(triggerSize_->IsEnabled())options.triggerSize=neogff::ParseFloatDecimal(wxui::toStd(triggerSize_->GetValue()));
    return options;
}
void ObjectPalette::setPlacementEnabled(bool enabled) {
    placementEnabled_=enabled;
    const auto row=entries_->GetNextItem(-1,wxLIST_NEXT_ALL,wxLIST_STATE_SELECTED);
    place_->Enable(enabled&&row!=-1);
    const auto type=selectedType();const bool polygon=type==2032||type==2040;
    triggerSize_->Enable(polygon);draw_->Enable(enabled&&polygon);
    heading_->SetToolTip(type==2040?"Rotate the encounter footprint. K2 also uses this for the initial spawn facing; K1 ignores spawn facing.":"Heading in degrees about the world Z axis.");
}
std::optional<std::uint16_t> ObjectPalette::selectedType() const {
    const auto row=entries_->GetNextItem(-1,wxLIST_NEXT_ALL,wxLIST_STATE_SELECTED);
    return row==-1?std::nullopt:std::optional<std::uint16_t>{blueprints_.at(entries_->GetItemData(row)).type};
}
bool ObjectPalette::placing() const {return place_->GetValue();}
void ObjectPalette::stopPlacing() {place_->SetValue(false);placementModeChanged();}
void ObjectPalette::placementModeChanged() {
    wxCommandEvent event(EVT_OBJECT_PLACEMENT_MODE,GetId());event.SetEventObject(this);ProcessWindowEvent(event);
}
void ObjectPalette::installDropTarget(wxWindow* viewport,std::function<bool(wxPoint)> drop,std::function<bool()> ready) {
    viewport->SetDropTarget(new BlueprintDropTarget(std::move(drop),std::move(ready)));
}

}
