#include "ModuleEditorPanel.hpp"
#include "ObjectPalette.hpp"
#include "AreaControls.hpp"
#include "ModuleControls.hpp"
#include "ModulePackage.hpp"
#include "BlueprintEditor.hpp"
#include "CameraControls.hpp"
#include "wx/GFFEditorPanel.hpp"
#include "wx/ModelCanvas.hpp"
#include "core/GameResourceSession.hpp"
#include "NeoViewState.hpp"
#include <neoshared/bif/LooseArchiveCatalog.hpp>
#include <neoshared/erf/Archive.hpp>
#include <wx/choice.h>
#include <wx/dirdlg.h>
#include <wx/filedlg.h>
#include <wx/listbox.h>
#include <wx/notebook.h>
#include <wx/progdlg.h>
#include <wx/splitter.h>
#include <wx/timer.h>
#include <wx/wrapsizer.h>
#include <wx/wx.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <future>
#include <fstream>
#include <map>
#include <set>

namespace neotoolkit {
namespace {
using neoshared::ResourceDocument;
using neomdl::model::Vec3;
using Clock=std::chrono::steady_clock;
using Key=std::pair<std::string,std::uint16_t>;
using ByteSnapshot=std::shared_ptr<const std::vector<std::uint8_t>>;
constexpr std::uint16_t kAre=2012,kIfo=2014,kGit=2023;
constexpr float kDegrees=57.29577951308232f;
enum {ID_OpenModule=wxID_HIGHEST+24000,ID_ModuleAssets,ID_RefreshArea,
      ID_SaveResources,ID_SaveModule,ID_FrameArea,ID_FrameObject,ID_DuplicateObject,ID_DeleteObject,ID_SceneReport,ID_AddObject,
      ID_ModuleUndo,ID_ModuleRedo,ID_RedrawTrigger,ID_FinishPolygon,ID_CancelPolygon,ID_PackageModule,ID_EditBlueprint};

bool editableResource(std::uint16_t type) {return type==kAre||type==kIfo||type==kGit;}
Key resourceKey(const ResourceDocument& source) {
    return {neomdl::resourceKey(neoshared::pathToUtf8(neoshared::pathFromUtf8(source.fileName).stem())),source.type};
}
std::string decimal(float value) {return wxString::Format("%.9g",double(value)).ToStdString();}
using Fields=std::map<std::string,const neogff::GffField*>;
Fields fieldsOf(const neogff::GffStruct& record) {
    Fields fields;for(const auto& field:record.allFields())fields.emplace(field->GetLabel(),field.get());return fields;
}
float scalar(const Fields& fields,const std::string& name) {
    return dynamic_cast<const neogff::GffFloatField&>(*fields.at(name)).value;
}
void setField(neogff::GffStruct& record,std::unique_ptr<neogff::GffField> value) {
    auto& fields=record.allFields();
    const auto found=std::find_if(fields.begin(),fields.end(),[&](const auto& field){return field->GetLabel()==value->GetLabel();});
    if(found==fields.end())record.AddField(std::move(value));else *found=std::move(value);
}

struct Resource {
    ResourceDocument source;
    neogff::GffModel* model{};
    ByteSnapshot snapshot=std::make_shared<const std::vector<std::uint8_t>>(source.bytes);
};
struct Placement {
    std::string identity,path,list,label;
    std::size_t index{};
    Vec3 position;
    enum class Coordinates {Short,Long,Vector,Entry} coordinates;
    enum class Rotation {None,Bearing,Direction,Entry} rotation{Rotation::None};
    float heading{};
};
struct Preview {
    neomdl::AreaScene scene;
    std::shared_ptr<neomdl::GameResourceSession> session;
};
struct EditState {
    std::map<Key,ByteSnapshot> bytes;
    std::map<Key,ResourceDocument> templates;
    std::map<std::string,std::string> imports;
    std::map<Key,neomdl::ResourceResolver> origins;
    std::map<std::string,ModulePackageInput> inlineSources;
    std::string area,path,identity,action;
};

void writeResource(const std::filesystem::path& directory,const ResourceDocument& source) {
    std::ofstream output;output.exceptions(std::ios::failbit|std::ios::badbit);
    output.open(directory/neoshared::pathFromUtf8(source.fileName),std::ios::binary|std::ios::trunc);
    output.write(reinterpret_cast<const char*>(source.bytes.data()),source.bytes.size());
}

std::shared_ptr<neomdl::GameResourceSession> openModuleAssets(const neoshared::game::GameInstall& installation,const std::string& module,const neoshared::bif::JobControl& job={}) {
    neomdl::GameResourceSession base;std::string error;
    if(!base.open(installation,error,job))throw std::runtime_error(error);
    const auto modules=base.modules();const auto found=std::find_if(modules.begin(),modules.end(),[&](const auto& candidate){return neomdl::resourceKey(candidate.name)==module;});
    if(found==modules.end())throw std::runtime_error("The installation has no module scope named "+module+".");
    auto session=std::make_shared<neomdl::GameResourceSession>();
    if(!session->openModule(base,*found,error,job))throw std::runtime_error(error);
    return session;
}

class ModuleEditor final : public ModuleEditorPanel {
public:
    ModuleEditor(wxWindow* parent,neomodules::Context context)
        : ModuleEditorPanel(parent,std::move(context)),timer_(this) {
        auto* root=new wxBoxSizer(wxVERTICAL);
        auto* tools=new wxWrapSizer(wxHORIZONTAL);
        const std::pair<int,const char*> actions[]={{ID_OpenModule,"Open module..."},
            {ID_ModuleAssets,"Game resources..."},{ID_RefreshArea,"Refresh 3D"},
            {ID_AddObject,"Add object..."},
            {ID_ModuleUndo,"Undo"},{ID_ModuleRedo,"Redo"},
            {ID_SaveResources,"Save resources..."},{ID_SaveModule,"Save archive copy..."},{ID_PackageModule,"Package MOD..."},{ID_SceneReport,"Scene report..."}};
        for(const auto& action:actions)tools->Add(new wxButton(this,action.first,action.second),0,wxALL,3);
        root->Add(tools,0,wxEXPAND|wxALL,4);
        sourceLabel_=new wxStaticText(this,wxID_ANY,"Open a RIM/MOD archive or loose GIT, ARE or IFO resource.");
        root->Add(sourceLabel_,0,wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM,8);
        outer_=new wxSplitterWindow(this,wxID_ANY,wxDefaultPosition,wxDefaultSize,wxSP_LIVE_UPDATE);
        auto* inspector=new wxPanel(outer_);
        auto* sidebar=new wxBoxSizer(wxVERTICAL);
        sidebar->Add(new wxStaticText(inspector,wxID_ANY,"Preview game"),0,wxALL,6);
        gameChoice_=new wxChoice(inspector,wxID_ANY);
        gameChoice_->Append("Knights of the Old Republic (K1)");gameChoice_->Append("The Sith Lords (K2)");gameChoice_->SetSelection(1);
        sidebar->Add(gameChoice_,0,wxEXPAND|wxLEFT|wxRIGHT,6);
        sidebar->Add(new wxStaticText(inspector,wxID_ANY,"Area"),0,wxALL,6);
        areas_=new wxChoice(inspector,wxID_ANY);
        sidebar->Add(areas_,0,wxEXPAND|wxLEFT|wxRIGHT,6);
        objectTabs_=new wxNotebook(inspector,wxID_ANY);
        auto* objectPage=new wxPanel(objectTabs_);auto* objectBox=new wxBoxSizer(wxVERTICAL);
        objectBox->Add(new wxStaticText(objectPage,wxID_ANY,"GIT objects / module entry"),0,wxALL,6);
        objects_=new wxListBox(objectPage,wxID_ANY);
        objectBox->Add(objects_,1,wxEXPAND|wxLEFT|wxRIGHT,6);
        auto* fields=new wxFlexGridSizer(2,4,6);fields->AddGrowableCol(1);
        const char* names[]={"X","Y","Z","Heading (degrees)"};
        for(std::size_t i=0;i<4;++i) {
            fields->Add(new wxStaticText(objectPage,wxID_ANY,names[i]),0,wxALIGN_CENTER_VERTICAL);
            transform_[i]=new wxTextCtrl(objectPage,wxID_ANY);
            fields->Add(transform_[i],1,wxEXPAND);
        }
        objectBox->Add(fields,0,wxEXPAND|wxALL,6);
        apply_=new wxButton(objectPage,wxID_ANY,"Apply transform");
        objectBox->Add(apply_,0,wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM,6);
        auto* objectTools=new wxWrapSizer(wxHORIZONTAL);
        for(const auto& action:std::vector<std::pair<int,const char*>>{{ID_FrameObject,"Frame"},
                {ID_DuplicateObject,"Duplicate"},{ID_DeleteObject,"Delete"},{ID_RedrawTrigger,"Redraw footprint"},{ID_EditBlueprint,"Edit blueprint..."}})
            objectTools->Add(new wxButton(objectPage,action.first,action.second),0,wxALL,3);
        objectBox->Add(objectTools,0,wxEXPAND|wxALL,3);objectPage->SetSizer(objectBox);
        objectSource_=new wxStaticText(objectPage,wxID_ANY,"Select an object to see where its properties load from.");
        objectSource_->Wrap(FromDIP(195));objectBox->Add(objectSource_,0,wxEXPAND|wxALL,6);
        palette_=new ObjectPalette(objectTabs_);objectTabs_->AddPage(objectPage,"Objects");objectTabs_->AddPage(palette_,"Add objects");
        areaControls_=new AreaControls(objectTabs_,[this](const std::string& field,const std::string& value,const std::string& label){
            rememberSelection();auto& resource=resources_.at({controlsArea_,kAre});
            resource.model->setValue(field,value);stage(resource);
            selecting_=true;fields_->activateResource(resource.source.identity);fields_->selectField(field);selecting_=false;
            refreshAreaControls();recordEdit("Edit area "+label);requestPreview();
        },[this]{
            try {applyAreaPreviewPhase();}catch(const std::exception& e){wxui::showError(this,e);}
        });
        objectTabs_->AddPage(areaControls_,"Area");
        moduleControls_=new ModuleControls(objectTabs_,[this](const std::string& path,const std::string& type,const std::string& value,const std::string& label){
            rememberSelection();auto& resource=resources_.at({"module",kIfo});
            const auto field=path.substr(0,path.find('('));
            if(!resource.model->gff().root()->GetFieldByLabel(field))resource.model->addField({},field,type,type=="CExoLocString"?"-1":value);
            resource.model->setValue(path,value);stage(resource);
            selecting_=true;fields_->activateResource(resource.source.identity);fields_->refreshActiveDocument();fields_->selectField(path);selecting_=false;
            rebuildObjects();refreshModuleControls();recordEdit("Edit module "+label);requestPreview();
        },[this](ModuleControls::AreaAction action,std::size_t index,const std::string& name){editModuleAreaList(action,index,name);});
        objectTabs_->AddPage(moduleControls_,"Module");
        CameraControls::Actions cameraActions;
        cameraActions.direction=[this](float yaw,float pitch){canvas_->setViewerOrientation(yaw,pitch);updateActions();};
        cameraActions.select=[this](std::size_t index){selectObject(findPlacement("CameraList\\"+std::to_string(index)),true);rememberSelection();};
        cameraActions.preview=[this](std::optional<neomdl::AreaCameraRecord> camera){previewCamera(std::move(camera));};
        cameraActions.edit=[this](const neomdl::AreaCameraRecord& camera){editCamera(camera);};
        cameraControls_=new CameraControls(objectTabs_,std::move(cameraActions));objectTabs_->AddPage(cameraControls_,"Cameras");
        sidebar->Add(objectTabs_,1,wxEXPAND|wxTOP,6);
        inspector->SetSizer(sidebar);
        split_=new wxSplitterWindow(outer_,wxID_ANY,wxDefaultPosition,wxDefaultSize,wxSP_LIVE_UPDATE);
        auto* viewport=new wxPanel(split_);auto* viewBox=new wxBoxSizer(wxVERTICAL);
        auto* gizmoTools=new wxWrapSizer(wxHORIZONTAL);
        gizmoTools->Add(new wxStaticText(viewport,wxID_ANY,"Tool"),0,wxALIGN_CENTER_VERTICAL|wxALL,6);
        gizmoTool_=new wxChoice(viewport,wxID_ANY);
        for(const auto* tool:{"Select (Q)","Move (W)","Rotate (E)","Polygon vertices (R)"})gizmoTool_->Append(tool);
        gizmoTool_->SetSelection(0);gizmoTools->Add(gizmoTool_,0,wxALL,4);
        gizmoTools->Add(new wxButton(viewport,ID_FinishPolygon,"Finish polygon"),0,wxALL,4);
        gizmoTools->Add(new wxButton(viewport,ID_CancelPolygon,"Cancel polygon"),0,wxALL,4);
        gizmoHint_=new wxStaticText(viewport,wxID_ANY,"Select an object, then choose Move or Rotate.",
            wxDefaultPosition,wxDefaultSize,wxST_ELLIPSIZE_END|wxST_NO_AUTORESIZE);
        gizmoHint_->SetMinSize(FromDIP(wxSize(100,-1)));
        viewBox->Add(gizmoTools,0,wxEXPAND);viewBox->Add(gizmoHint_,0,wxEXPAND|wxALL,6);
        auto* snaps=new wxWrapSizer(wxHORIZONTAL);
        snaps->Add(new wxStaticText(viewport,wxID_ANY,"Grid"),0,wxALIGN_CENTER_VERTICAL|wxALL,5);
        gridSnap_=new wxChoice(viewport,wxID_ANY);
        for(const auto* step:{"Off","0.1","0.25","0.5","1","2"})gridSnap_->Append(step);
        gridSnap_->SetSelection(0);snaps->Add(gridSnap_,0,wxALL,3);
        gridSnap_->SetToolTip("World units for placement, move handles and trigger vertices. Off preserves free movement.");
        snaps->Add(new wxStaticText(viewport,wxID_ANY,"Rotation (degrees)"),0,wxALIGN_CENTER_VERTICAL|wxALL,5);
        rotationSnap_=new wxChoice(viewport,wxID_ANY);
        for(const auto* step:{"Off","5","15","30","45","90"})rotationSnap_->Append(step);
        rotationSnap_->SetSelection(0);snaps->Add(rotationSnap_,0,wxALL,3);
        walkmeshSnap_=new wxCheckBox(viewport,wxID_ANY,"Walkmesh");snaps->Add(walkmeshSnap_,0,wxALIGN_CENTER_VERTICAL|wxALL,5);
        walkmeshSnap_->SetToolTip("Snap placement, XY movement and polygon vertices to walkable WOK faces. Z handles remain independent.");
        viewBox->Add(snaps,0,wxEXPAND);
        canvas_=new neomdl::ui::ModelCanvas(viewport);viewBox->Add(canvas_,1,wxEXPAND);viewport->SetSizer(viewBox);
        palette_->installDropTarget(canvas_,[this](wxPoint point){return dropObject(point);},[this]{return canPlaceObject();});
        createFields();
        split_->SetMinimumPaneSize(FromDIP(260));split_->SetSashGravity(0.55);
        split_->SplitVertically(viewport,fields_,FromDIP(520));
        outer_->SetMinimumPaneSize(FromDIP(190));outer_->SplitVertically(inspector,split_,FromDIP(240));
        root->Add(outer_,1,wxEXPAND);
        SetSizer(root);createModuleStatusBar(1);
        staging_=neoshared::erf::make_unique_temp_directory(std::filesystem::temp_directory_path(),"NeoToolKit-module");
        neomdl::ui::RenderOptions options;options.previewTriggers=true;
        canvas_->setOptions(options);
        buildMenus();
        Bind(wxEVT_MENU,[this](wxCommandEvent& event){command(event);});
        Bind(wxEVT_BUTTON,[this](wxCommandEvent& event){command(event);});
        gameChoice_->Bind(wxEVT_CHOICE,[this](wxCommandEvent&){
            if(blueprintEditor_)blueprintEditor_->clear();
            cameraControls_->stopPreview();
            game_=gameChoice_->GetSelection()==0?neomdl::model::GameVersion::Kotor1:neomdl::model::GameVersion::Kotor2;
            install_.reset();session_.reset();
            palette_->clear();paletteSession_.reset();
            auto document=neomdl::ModelDocument::emptySceneDocument("module-editor:"+moduleName_,moduleName_,game_);
            canvas_->setDocument(document.get());document_=std::move(document);
            pathOwners_.clear();ownerPaths_.clear();previewCurrent_=false;refreshAreaControls();refreshCameraControls();requestPreview();
        });
        areas_->Bind(wxEVT_CHOICE,[this](wxCommandEvent&){
            cameraControls_->stopPreview();cameraControls_->load(nullptr,game_==neomdl::model::GameVersion::Kotor2);
            selectedPath_.clear();pathOwners_.clear();ownerPaths_.clear();previewCurrent_=false;
            canvas_->clearArea();rebuildObjects();activateAreaResource();refreshAreaControls();rememberSelection();requestPreview();
        });
        objects_->Bind(wxEVT_LISTBOX,[this](wxCommandEvent&){selectObject(objects_->GetSelection(),true);rememberSelection();});
        gizmoTool_->Bind(wxEVT_CHOICE,[this](wxCommandEvent&){
            canvas_->cancelAreaPolygon();canvas_->cancelAreaGizmo();palette_->stopPlacing();updateActions();
        });
        const auto snapping=[this](wxCommandEvent&){
            const float grids[]={0,0.1f,0.25f,0.5f,1,2},angles[]={0,5,15,30,45,90};
            canvas_->setAreaSnapping({grids[gridSnap_->GetSelection()],angles[rotationSnap_->GetSelection()],walkmeshSnap_->GetValue()});
        };
        gridSnap_->Bind(wxEVT_CHOICE,snapping);rotationSnap_->Bind(wxEVT_CHOICE,snapping);walkmeshSnap_->Bind(wxEVT_CHECKBOX,snapping);
        objectTabs_->Bind(wxEVT_NOTEBOOK_PAGE_CHANGED,[this](wxBookCtrlEvent& event){
            if(event.GetSelection()!=1)palette_->stopPlacing();
            event.Skip();
        });
        apply_->Bind(wxEVT_BUTTON,[this](wxCommandEvent&){
            try {applyTransform();} catch(const std::exception& e) {wxui::showError(this,e);}
        });
        canvas_->Bind(neomdl::ui::EVT_AREA_OWNER_PICKED,[this](wxCommandEvent& event){
            const auto found=ownerPaths_.find(wxui::toStd(event.GetString()));
            if(found==ownerPaths_.end())selectObject(wxNOT_FOUND,false);
            else selectObject(findPlacement(found->second),true);
            rememberSelection();
        });
        palette_->Bind(EVT_OBJECT_PLACEMENT_MODE,[this](wxCommandEvent&){
            if(palette_->placing())canvas_->cancelAreaGizmo();
            updateActions();
        });
        canvas_->Bind(neomdl::ui::EVT_AREA_GIZMO,[this](neomdl::ui::AreaGizmoEvent& event){gizmoChanged(event);});
        canvas_->Bind(neomdl::ui::EVT_AREA_MARKER_PICKED,[this](wxCommandEvent& event){
            selectObject(findPlacement(wxui::toStd(event.GetString())),true);rememberSelection();
        });
        canvas_->Bind(neomdl::ui::EVT_AREA_POLYGON,[this](neomdl::ui::AreaPolygonEvent& event){polygonChanged(event);});
        palette_->Bind(EVT_TRIGGER_POLYGON_REQUESTED,[this](wxCommandEvent&){
            try {beginTriggerDrawing(true);}catch(const std::exception& e){wxui::showError(this,e);}
        });
        canvas_->Bind(wxEVT_LEFT_DOWN,[this](wxMouseEvent& event){
            if(palette_->placing()) {canvas_->SetFocus();dropObject(event.GetPosition());}
            else event.Skip();
        });
        canvas_->Bind(wxEVT_RIGHT_DOWN,[this](wxMouseEvent& event){
            if(palette_->placing())palette_->stopPlacing();else event.Skip();
        });
        canvas_->Bind(wxEVT_CHAR_HOOK,[this](wxKeyEvent& event){
            if(event.GetKeyCode()==WXK_ESCAPE) {
                if(palette_->placing())palette_->stopPlacing();
                else if(canvas_->areaPolygonDrawing()||canvas_->areaPolygonDragging())canvas_->cancelAreaPolygon();
                else if(canvas_->areaGizmoDragging())canvas_->cancelAreaGizmo();
                else event.Skip();
            } else if(!event.HasAnyModifiers()&&!palette_->placing()&&!canvas_->areaGizmoDragging()&&!canvas_->areaPolygonDrawing()&&
                !canvas_->areaPolygonDragging()&&!canvas_->freeFlyCameraActive()&&
                (event.GetKeyCode()=='Q'||event.GetKeyCode()=='W'||event.GetKeyCode()=='E'||event.GetKeyCode()=='R')) {
                gizmoTool_->SetSelection(event.GetKeyCode()=='Q'?0:event.GetKeyCode()=='W'?1:event.GetKeyCode()=='E'?2:3);updateActions();
            } else event.Skip();
        });
        Bind(wxEVT_TIMER,[this](wxTimerEvent&){tick();});timer_.Start(33);
        updateActions();
    }
    ~ModuleEditor() override {
        timer_.Stop();cancel_->store(true);
        if(loading_.valid())loading_.wait();
        DestroyChildren();
        std::error_code error;std::filesystem::remove_all(staging_,error);
    }
    void setAppearance(bool dark,double scale) override {
        dark_=dark;scale_=scale;wxui::applyTheme(this,dark);neoview::applyFontScale(this,scale);
        fields_->setAppearance(dark,scale);
    }
    std::vector<std::filesystem::path> openPaths() const override {return inputs_;}
    bool canClose() override {
        canvas_->cancelAreaPolygon();
        canvas_->cancelAreaGizmo();
        requested_=false;cancel_->store(true);
        if(loading_.valid()) {
            loading_.wait();loading_=std::future<Preview>{};canvas_->setLoading(false);
        }
        if(!dirty())return true;
        const auto answer=wxMessageBox("Save the edited module resources before closing?",
            "Unsaved module edits",wxYES_NO|wxCANCEL|wxICON_QUESTION,this);
        if(answer==wxCANCEL)return false;
        if(answer==wxNO)return true;
        try {return saveResources();}catch(const std::exception& e) {wxui::showError(this,e);return false;}
    }
    bool openFile(const std::filesystem::path& input) override {
        try {
            if(!canClose())return false;
            const auto path=std::filesystem::absolute(input);
            std::map<Key,Resource> resources;
            auto catalog=std::make_shared<neoshared::bif::LooseArchiveCatalog>();
            const auto extension=neomdl::resourceKey(neoshared::pathToUtf8(path.extension()));
            const bool archive=extension==".rim"||extension==".mod";
            if(archive) {
                if(!catalog->openFiles(path.parent_path(),{path}))throw std::runtime_error(catalog->lastError());
                for(const auto& entry:catalog->resources())if(editableResource(entry.type)) {
                    auto source=readArchiveResource(*catalog,entry);
                    const auto key=resourceKey(source);
                    resources.emplace(key,Resource{std::move(source),{}});
                }
            } else {
                for(const auto& entry:std::filesystem::directory_iterator(path.parent_path())) {
                    const auto type=neoshared::bif::resourceTypeFromExtension(
                        neoshared::pathToUtf8(entry.path().extension()),neoshared::bif::GameProfile::KotOR);
                    if(!type||!editableResource(*type)||!entry.is_regular_file())continue;
                    ResourceDocument source{neomdl::resourceFileIdentity(entry.path()),
                        neoshared::pathToUtf8(entry.path().filename()),neoshared::pathToUtf8(entry.path()),*type,
                        neomdl::readResourceFile(entry.path()),{entry.path()}};
                    const auto key=resourceKey(source);
                    resources.emplace(key,Resource{std::move(source),{}});
                }
            }
            if(resources.empty())throw std::runtime_error("This module contains no GIT, ARE or IFO resources.");
            archivePath_=archive?path:std::filesystem::path{};
            moduleName_=neomdl::resourceKey(neoshared::pathToUtf8(path.stem()));
            if(archive&&moduleName_.size()>2&&moduleName_.substr(moduleName_.size()-2)=="_s")moduleName_.resize(moduleName_.size()-2);
            if(moduleName_=="module")moduleName_=neomdl::resourceKey(neoshared::pathToUtf8(path.parent_path().filename()));
            catalog_=std::move(catalog);localDirectory_=archive?std::filesystem::path{}:path.parent_path();
            archiveResources_=neomdl::archiveResources(catalog_);
            hostResources_=captureHostResources();
            install_.reset();session_.reset();previewEnabled_=false;
            installResources(std::move(resources),neoshared::pathToUtf8(path));
            return true;
        } catch(const std::exception& e) {wxui::showError(this,e);return false;}
    }
    bool openResource(ResourceDocument source) override {
        try {
            if(!canClose())return false;
            auto resolver=captureHostResources();
            std::map<Key,Resource> resources;
            const auto key=resourceKey(source);
            std::set<std::string> names;
            if(source.type==kIfo) {
                neogff::GffFile file;file.LoadBytes(source.bytes);
                const auto& areas=static_cast<const neogff::GffList&>(*file.root()->GetFieldByLabel("Mod_Area_list"));
                for(const auto& area:areas.allStructs())names.insert(neomdl::resourceKey(area->GetFieldByLabel("Area_Name")->GetString()));
            } else names.insert(key.first);
            resources.emplace(key,Resource{std::move(source),{}});
            for(const auto& name:names)for(const auto type:{kAre,kGit}) {
                if(resources.count({name,type}))continue;
                if(auto companion=resolver(name,type))resources.emplace(Key{name,type},Resource{std::move(*companion),{}});
            }
            if(!resources.count({"module",kIfo}))if(auto ifo=resolver("module",kIfo))
                resources.emplace(Key{"module",kIfo},Resource{std::move(*ifo),{}});
            archivePath_.clear();localDirectory_.clear();catalog_=std::make_shared<neoshared::bif::LooseArchiveCatalog>();
            archiveResources_=neomdl::archiveResources(catalog_);
            hostResources_=std::move(resolver);moduleName_=names.empty()?key.first:*names.begin();
            install_.reset();session_.reset();previewEnabled_=true;
            installResources(std::move(resources),"Module resources from Game Explorer");
            requestPreview();return true;
        } catch(const std::exception& e) {wxui::showError(this,e);return false;}
    }

private:
    static ResourceDocument readArchiveResource(const neoshared::bif::LooseArchiveCatalog& catalog,
        const neoshared::bif::LooseResourceInfo& entry) {
        const auto& archive=catalog.archives().at(entry.archiveIndex);
        ResourceDocument source{neoshared::pathToUtf8(archive.resolvedPath)+"#"+entry.fileName(),
            entry.fileName(),neoshared::pathToUtf8(archive.resolvedPath),entry.type,{}, {archive.resolvedPath}};
        std::string error;
        if(!catalog.readResource(entry.index,source.bytes,error,neomdl::resourceLoadJob()))throw std::runtime_error(error);
        return source;
    }
    neomdl::ResourceResolver captureHostResources() const {
        return [resolve=context_.captureResourceResolver()](const std::string& name,std::uint16_t type) {
            return resolve(name,type,neomdl::resourceLoadJob());
        };
    }
    void createFields() {
        neomodules::Context context;context.embedded=true;context.compact=true;
        context.validateOutputs=context_.validateOutputs;
        fields_=neogff::ui::createEditorPanel(split_,std::move(context),true);
        fields_->Bind(neogff::ui::EVT_GFF_DOCUMENT_EDITED,[this](wxCommandEvent& event){
            try {
                auto& resource=resources_.at(identityKeys_.at(wxui::toStd(event.GetString())));
                if(resource.source.type==kAre)refreshAreaControls();
                if(resource.source.type==kIfo)refreshModuleControls();
                stage(resource);rebuildObjects();recordEdit("Edit field");requestPreview();
            } catch(const std::exception& e) {wxui::showError(this,e);}
        });
        fields_->Bind(neogff::ui::EVT_GFF_FIELD_SELECTED,[this](wxCommandEvent& event){
            if(selecting_)return;
            const auto path=wxui::toStd(event.GetString()),identity=fields_->activeResourceIdentity();
            rememberSelection();
            for(std::size_t i=0;i<placements_.size();++i) {
                const auto& p=placements_[i];
                if(p.identity==identity&&((!p.path.empty()&&(path==p.path||path.find(p.path+"\\")==0))||
                    (p.coordinates==Placement::Coordinates::Entry&&path.find("Mod_Entry_")==0))) {
                    selectObject(int(i),false);rememberSelection();return;
                }
            }
        });
    }
    void buildMenus() {
        auto* menus=new wxMenuBar;
        auto* file=new wxMenu;
        file->Append(ID_OpenModule,"Open Live Module...");
        file->Append(ID_ModuleAssets,"Game Resources...");
        file->Append(ID_RefreshArea,"Refresh 3D Preview");
        file->Append(ID_SceneReport,"Scene Report...");
        file->AppendSeparator();
        file->Append(ID_SaveResources,"Save Resources...\tCtrl+S");
        file->Append(ID_SaveModule,"Save Archive Copy...\tCtrl+Shift+S");
        file->Append(ID_PackageModule,"Package Combined MOD...");
        menus->Append(file,"Module");
        editMenu_=new wxMenu;
        editMenu_->Append(ID_ModuleUndo,"Undo\tCtrl+Z");editMenu_->Append(ID_ModuleRedo,"Redo\tCtrl+Y");
        menus->Append(editMenu_,"Edit");
        auto* objects=new wxMenu;
        objects->Append(ID_AddObject,"Add Object from Game Files...");
        objects->Append(ID_FrameArea,"Frame Area");objects->Append(ID_FrameObject,"Frame Selected Object");
        objects->Append(ID_DuplicateObject,"Duplicate Selected Object");objects->Append(ID_DeleteObject,"Delete Selected Object");
        objects->Append(ID_RedrawTrigger,"Redraw Selected Polygon Footprint");
        objects->Append(ID_EditBlueprint,"Edit Selected Object Blueprint...");
        menus->Append(objects,"Objects");
        fieldMenus_=fields_->takeMenus();
        for(std::size_t i=0;i<fieldMenus_->GetMenuCount();) {
            const auto title=fieldMenus_->GetMenuLabel(i);
            if(title=="&Edit"||title=="&View")menus->Append(fieldMenus_->Remove(i),title=="&Edit"?"Fields":"Field View");
            else ++i;
        }
        setModuleMenus(menus);
    }
    void installResources(std::map<Key,Resource> resources,const std::string& title) {
        if(blueprintEditor_)blueprintEditor_->clear();
        cameraControls_->stopPreview();cameraControls_->load(nullptr,game_==neomdl::model::GameVersion::Kotor2);
        areaControls_->load(nullptr,game_==neomdl::model::GameVersion::Kotor2);
        moduleControls_->load(nullptr,{});
        palette_->clear();paletteSession_.reset();addedTemplates_.clear();importedTemplates_.clear();templateOrigins_.clear();inlineSources_.clear();templatesDirty_=false;
        selecting_=true;fields_->clearDocuments();
        resources_=std::move(resources);identityKeys_.clear();inputs_.clear();
        areas_->Clear();std::set<std::string> names;
        for(auto& [key,resource]:resources_) {
            identityKeys_[resource.source.identity]=key;
            inputs_.insert(inputs_.end(),resource.source.protectedInputs.begin(),resource.source.protectedInputs.end());
            fields_->openResource(resource.source);resource.model=fields_->activeModel();
            if(key.second==kGit||key.second==kAre)names.insert(key.first);
        }
        for(const auto& name:names)areas_->Append(wxui::toWx(name));
        if(!names.empty())areas_->SetSelection(0);
        selectedPath_.clear();pathOwners_.clear();ownerPaths_.clear();previewCurrent_=false;
        auto document=neomdl::ModelDocument::emptySceneDocument("module-editor:"+title,title,game_);
        canvas_->setDocument(document.get());document_=std::move(document);
        sourceTitle_=title;
        history_.clear();historyIndex_=0;
        savedBytes_.clear();savedTemplateKeys_.clear();
        for(const auto& [key,resource]:resources_)savedBytes_.emplace(key,resource.snapshot);
        rebuildObjects();activateAreaResource();selecting_=false;
        history_.push_back(captureEdit());refreshAreaControls();setAppearance(dark_,scale_);updateActions();
        setModuleStatusText("Module loaded. Edit a field or choose Game resources to load the 3D view.");
    }
    std::string areaName() const {return wxui::toStd(areas_->GetStringSelection());}
    void activateAreaResource() {
        auto found=resources_.find({areaName(),kGit});
        if(found==resources_.end())found=resources_.find({areaName(),kAre});
        if(found!=resources_.end())fields_->activateResource(found->second.source.identity);
    }
    void stage(Resource& resource) {
        auto bytes=resource.model->gff().SaveBytes();
        if(bytes!=resource.source.bytes) {
            resource.source.bytes=std::move(bytes);
            resource.snapshot=std::make_shared<const std::vector<std::uint8_t>>(resource.source.bytes);
        }
        if(!editableResource(resource.source.type))addedTemplates_[resourceKey(resource.source)]=resource.source;
    }
    bool dirty() const {
        return templatesDirty_||std::any_of(resources_.begin(),resources_.end(),[](const auto& pair){return pair.second.model->dirty();});
    }
    void rebuildObjects();
    void selectObject(int index,bool selectField);
    int findPlacement(const std::string& path) const {
        for(std::size_t i=0;i<placements_.size();++i)if(placements_[i].path==path)return int(i);
        return wxNOT_FOUND;
    }
    void applyTransform();
    void refreshCameraControls();
    void previewCamera(std::optional<neomdl::AreaCameraRecord> camera);
    void editCamera(const neomdl::AreaCameraRecord& camera);
    void refreshAreaControls() {
        controlsArea_=areaName();const auto resource=resources_.find({controlsArea_,kAre});
        areaControls_->load(resource==resources_.end()?nullptr:resource->second.model->gff().root(),game_==neomdl::model::GameVersion::Kotor2);
        refreshModuleControls();
    }
    void refreshModuleControls() {
        std::set<std::string> names;
        for(const auto& [key,resource]:resources_)if(key.second==kAre)names.insert(key.first);
        if(session_)for(const auto& name:session_->areaResrefs())names.insert(name);
        const auto resource=resources_.find({"module",kIfo});
        moduleControls_->load(resource==resources_.end()?nullptr:resource->second.model->gff().root(),{names.begin(),names.end()});
    }
    void editModuleAreaList(ModuleControls::AreaAction action,std::size_t index,const std::string& name);
    void applyAreaPreviewPhase() {
        const auto phase=areaControls_->previewPhase();
        if(phase==0)canvas_->clearAreaEnvironmentReplay();
        else {
            neomdl::AreaEnvironmentReplayState state;state.phase=phase==1?neomdl::AreaTimeOfDay::Day:neomdl::AreaTimeOfDay::Night;
            std::string error;if(!canvas_->applyAreaEnvironmentReplay(std::move(state),error))throw std::runtime_error(error);
        }
    }
    void previewTransform(const Placement& placement,Vec3 position,float heading);
    void gizmoChanged(const neomdl::ui::AreaGizmoEvent& event);
    void updateGizmo();
    void beginTriggerDrawing(bool create);
    void polygonChanged(const neomdl::ui::AreaPolygonEvent& event);
    void writeTriggerGeometry(const Placement& placement,const std::vector<Vec3>& points,int vertex);
    EditState captureEdit(const std::string& action={}) const;
    void rememberSelection();
    void recordEdit(const std::string& action);
    void restoreEdit(std::size_t index);
    void updateDirty();
    void duplicateObject();
    void deleteObject();
    void chooseAssets();
    bool canPlaceObject() const {return session_&&previewCurrent_&&!requested_&&!loading_.valid()&&!cameraControls_->previewing()&&
        !canvas_->areaPolygonDrawing()&&!canvas_->areaPolygonDragging()&&resources_.count({areaName(),kGit})!=0;}
    void updatePlacementCursor() {canvas_->SetCursor(wxCursor(palette_->placing()&&canPlaceObject()?wxCURSOR_CROSS:wxCURSOR_ARROW));}
    bool dropObject(wxPoint point);
    void insertObject(Vec3 position,const ObjectPlacementOptions& options,const std::vector<Vec3>& polygon={});
    std::string importTemplate(neoshared::ResourceDocument source,const neomdl::ResourceResolver& resolver);
    std::optional<ResourceDocument> sceneTemplate(const std::string& name,std::uint16_t type) const;
    bool importItemReferences(neogff::GffStruct& record,const neomdl::ResourceResolver& resolver);
    void requestPreview() {
        canvas_->cancelAreaPolygon();
        canvas_->cancelAreaGizmo();
        ++revision_;cancel_->store(true);
        if(previewEnabled_) {requested_=true;due_=Clock::now()+std::chrono::milliseconds(200);}
        updateActions();
    }
    void startPreview();
    void tick();
    void mapOwners();
    bool saveResources();
    bool saveArchive();
    bool packageModule();
    neomdl::ResourceResolver moduleResources() const;
    Resource& openBlueprintResource(ResourceDocument source,const neomdl::ResourceResolver& resolver);
    void editBlueprint();
    void refreshBlueprintEditor();
    bool blueprintChanged(const EditState& state) const;
    bool exportsResource(const Key& key) const {return editableResource(key.second)||addedTemplates_.count(key)!=0;}
    void saved() {
        for(const auto& [key,resource]:resources_)savedBytes_[key]=resource.snapshot;
        savedTemplateKeys_.clear();for(const auto& [key,source]:addedTemplates_)savedTemplateKeys_.insert(key);
        updateDirty();
        fields_->refreshActiveDocument();updateActions();
    }
    void updateActions() {
        const bool loaded=!resources_.empty(),selected=objects_->GetSelection()!=wxNOT_FOUND;
        palette_->setPlacementEnabled(canPlaceObject());updatePlacementCursor();
        enableModuleCommand(ID_AddObject,loaded);FindWindow(ID_AddObject)->Enable(loaded);
        gameChoice_->Enable(loaded);
        const bool busy=canvas_->areaGizmoDragging()||canvas_->areaPolygonDragging()||canvas_->areaPolygonDrawing();
        const bool editing=!requested_&&!loading_.valid()&&!busy;
        const bool undo=!busy&&!history_.empty()&&historyIndex_>0;
        const bool redo=!busy&&!history_.empty()&&historyIndex_+1<history_.size();
        enableModuleCommand(ID_ModuleUndo,undo);FindWindow(ID_ModuleUndo)->Enable(undo);
        enableModuleCommand(ID_ModuleRedo,redo);FindWindow(ID_ModuleRedo)->Enable(redo);
        editMenu_->SetLabel(ID_ModuleUndo,wxui::toWx(undo?"Undo "+history_[historyIndex_].action+"\tCtrl+Z":"Undo\tCtrl+Z"));
        editMenu_->SetLabel(ID_ModuleRedo,wxui::toWx(redo?"Redo "+history_[historyIndex_+1].action+"\tCtrl+Y":"Redo\tCtrl+Y"));
        if(blueprintEditor_)blueprintEditor_->setHistory(undo,redo);
        gizmoTool_->Enable(editing&&loaded);
        objects_->Enable(!busy);areas_->Enable(!busy);gameChoice_->Enable(loaded&&!busy);fields_->Enable(!busy);palette_->Enable(!busy);
        areaControls_->Enable(loaded&&!busy);areaControls_->enablePreview(previewCurrent_&&!requested_&&!loading_.valid()&&!busy);
        moduleControls_->Enable(loaded&&!busy);
        cameraControls_->Enable(loaded&&!busy);cameraControls_->enablePreview(previewCurrent_&&!requested_&&!loading_.valid()&&!busy);
        gridSnap_->Enable(!busy);rotationSnap_->Enable(!busy);walkmeshSnap_->Enable(!busy);
        FindWindow(ID_FinishPolygon)->Enable(canvas_->areaPolygonDrawing());FindWindow(ID_CancelPolygon)->Enable(canvas_->areaPolygonDrawing());
        const bool trigger=selected&&(placements_[objects_->GetSelection()].list=="TriggerList"||placements_[objects_->GetSelection()].list=="Encounter List")&&previewCurrent_&&editing;
        bool blueprint=false;
        if(selected) {
            const auto& p=placements_[objects_->GetSelection()];const auto& root=*resources_.at(identityKeys_.at(p.identity)).model->gff().root();
            const auto* use=dynamic_cast<const neogff::GffByteField*>(root.GetFieldByLabel("UseTemplates"));
            blueprint=use&&use->value&&std::any_of(kObjectKinds.begin(),kObjectKinds.end(),[&](const auto& kind){return p.list==kind.list&&kind.type!=2058&&kind.type!=2040;});
        }
        enableModuleCommand(ID_EditBlueprint,blueprint&&!busy);FindWindow(ID_EditBlueprint)->Enable(blueprint&&!busy);
        FindWindow(ID_EditBlueprint)->SetToolTip("Edit the UTC/UTP/UTD/UTT/UTS/UTI/UTM used by this instance. Inline object properties remain in Fields.");
        enableModuleCommand(ID_RedrawTrigger,trigger);FindWindow(ID_RedrawTrigger)->Enable(trigger);
        for(const auto id:{ID_ModuleAssets,ID_RefreshArea,ID_SaveResources,ID_SceneReport,ID_PackageModule}) {
            enableModuleCommand(id,loaded&&!busy);if(auto* control=FindWindow(id))control->Enable(loaded&&!busy);
        }
        const bool archive=loaded&&!archivePath_.empty()&&!busy;
        enableModuleCommand(ID_SaveModule,archive);FindWindow(ID_SaveModule)->Enable(archive);
        for(const auto id:{ID_FrameObject,ID_DuplicateObject,ID_DeleteObject}) {
            bool enabled=selected&&(id==ID_FrameObject||(editing&&placements_[objects_->GetSelection()].coordinates!=Placement::Coordinates::Entry));
            if(enabled&&id==ID_DuplicateObject&&placements_[objects_->GetSelection()].list=="CameraList") {
                const auto& p=placements_[objects_->GetSelection()];
                const auto& root=*resources_.at(identityKeys_.at(p.identity)).model->gff().root();
                const auto& cameras=static_cast<const neogff::GffList&>(*root.GetFieldByLabel("CameraList"));
                enabled=cameras.count()<(game_==neomdl::model::GameVersion::Kotor1?50u:70u);
            }
            enableModuleCommand(id,enabled);FindWindow(id)->Enable(enabled);
        }
        apply_->Enable(selected&&editing);for(auto* control:transform_)control->Enable(selected&&editing);
        if(selected)transform_[3]->Enable(editing&&placements_[objects_->GetSelection()].rotation!=Placement::Rotation::None);
        if(loaded)sourceLabel_->SetLabel(wxui::toWx(sourceTitle_+(dirty()?" *":"")));
        updateGizmo();
    }
    void command(wxCommandEvent& event) {
        try {
            switch(event.GetId()) {
            case ID_OpenModule: {
                wxFileDialog dialog(this,"Open live module",{},{},
                    "Module archives and resources|*.rim;*.RIM;*.mod;*.MOD;*.git;*.GIT;*.are;*.ARE;*.ifo;*.IFO",wxFD_OPEN|wxFD_FILE_MUST_EXIST);
                if(dialog.ShowModal()==wxID_OK)openFile(neosettings::pathFromWx(dialog.GetPath()));
                return;
            }
            case ID_ModuleAssets:chooseAssets();return;
            case ID_AddObject:objectTabs_->SetSelection(1);return;
            case ID_ModuleUndo:restoreEdit(historyIndex_-1);return;
            case ID_ModuleRedo:restoreEdit(historyIndex_+1);return;
            case ID_RedrawTrigger:beginTriggerDrawing(false);return;
            case ID_FinishPolygon:canvas_->finishAreaPolygon();return;
            case ID_CancelPolygon:canvas_->cancelAreaPolygon();return;
            case ID_RefreshArea:previewEnabled_=true;requestPreview();return;
            case ID_SaveResources:saveResources();return;
            case ID_SaveModule:saveArchive();return;
            case ID_PackageModule:packageModule();return;
            case ID_EditBlueprint:editBlueprint();return;
            case ID_FrameArea:canvas_->frameModel();return;
            case ID_FrameObject:canvas_->framePosition(placements_.at(objects_->GetSelection()).position,5.0f);return;
            case ID_DuplicateObject:duplicateObject();return;
            case ID_DeleteObject:deleteObject();return;
            case ID_SceneReport: {
                std::string report=previewError_;
                for(const auto& message:canvas_->areaScene().diagnostics())report+=message+"\n";
                wxDialog dialog(this,wxID_ANY,"Module scene report",wxDefaultPosition,FromDIP(wxSize(750,480)),wxDEFAULT_DIALOG_STYLE|wxRESIZE_BORDER);
                auto* box=new wxBoxSizer(wxVERTICAL);
                box->Add(new wxTextCtrl(&dialog,wxID_ANY,wxui::toWx(report),wxDefaultPosition,wxDefaultSize,wxTE_MULTILINE|wxTE_READONLY),1,wxEXPAND|wxALL,8);
                box->Add(dialog.CreateButtonSizer(wxOK),0,wxALL|wxALIGN_RIGHT,8);
                dialog.SetSizer(box);wxui::applyTheme(&dialog,dark_);dialog.ShowModal();return;
            }
            default:
                if(fields_->ownsCommand(event.GetId()))fields_->dispatch(event);
                else event.Skip();
            }
        } catch(const std::exception& e) {wxui::showError(this,e);}
    }

    wxSplitterWindow* outer_{};wxSplitterWindow* split_{};
    neogff::ui::GFFEditorPanel* fields_{};
    neomdl::ui::ModelCanvas* canvas_{};
    wxStaticText* sourceLabel_{};wxChoice* areas_{};wxChoice* gameChoice_{};wxListBox* objects_{};
    wxTextCtrl* transform_[4]{};wxButton* apply_{};wxStaticText* objectSource_{};
    wxNotebook* objectTabs_{};ObjectPalette* palette_{};
    AreaControls* areaControls_{};std::string controlsArea_;
    ModuleControls* moduleControls_{};
    CameraControls* cameraControls_{};
    BlueprintEditor* blueprintEditor_{};Key blueprintKey_;
    wxChoice* gizmoTool_{};wxStaticText* gizmoHint_{};wxMenu* editMenu_{};
    wxChoice* gridSnap_{};wxChoice* rotationSnap_{};wxCheckBox* walkmeshSnap_{};
    bool drawingNewTrigger_{};
    std::unique_ptr<wxMenuBar> fieldMenus_;
    std::map<Key,Resource> resources_;
    std::map<Key,ResourceDocument> addedTemplates_;
    std::map<std::string,std::string> importedTemplates_;
    std::map<Key,neomdl::ResourceResolver> templateOrigins_;
    std::map<std::string,ModulePackageInput> inlineSources_;
    bool templatesDirty_{};
    std::vector<EditState> history_;std::size_t historyIndex_{};
    std::map<Key,ByteSnapshot> savedBytes_;
    std::set<Key> savedTemplateKeys_;
    std::map<std::string,Key> identityKeys_;
    std::shared_ptr<neoshared::bif::LooseArchiveCatalog> catalog_;
    std::filesystem::path archivePath_,localDirectory_,staging_;
    std::vector<std::filesystem::path> inputs_;
    std::vector<Placement> placements_;
    std::map<std::string,std::string> ownerPaths_;
    std::map<std::string,std::set<neomdl::LightOwnerId>> pathOwners_;
    std::string selectedPath_,moduleName_,sourceTitle_,previewError_;
    neomdl::ResourceResolver hostResources_,archiveResources_;
    std::unique_ptr<neomdl::ModelDocument> document_;
    neomdl::model::GameVersion game_{neomdl::model::GameVersion::Kotor2};
    std::optional<neoshared::game::GameInstall> install_;
    std::shared_ptr<neomdl::GameResourceSession> session_;
    std::shared_ptr<neomdl::GameResourceSession> paletteSession_;
    std::future<Preview> loading_;
    std::shared_ptr<std::atomic<bool>> cancel_{std::make_shared<std::atomic<bool>>(false)};
    std::size_t revision_{},loadingRevision_{};
    bool previewEnabled_{},previewCurrent_{},requested_{},selecting_{},dark_{};
    double scale_{1.0};wxTimer timer_;
    Clock::time_point due_,lastFrame_{Clock::now()};
};

void ModuleEditor::refreshCameraControls() {
    const auto resource=resources_.find({areaName(),kGit});
    cameraControls_->load(resource==resources_.end()?nullptr:resource->second.model->gff().root(),game_==neomdl::model::GameVersion::Kotor2);
    if(previewCurrent_&&!requested_&&!loading_.valid()) {
        if(cameraControls_->previewing())previewCamera(cameraControls_->pendingCamera());
        else canvas_->previewAreaCameras(cameraControls_->records());
    }
}

void ModuleEditor::previewCamera(std::optional<neomdl::AreaCameraRecord> camera) {
    canvas_->cancelAreaPolygon();canvas_->cancelAreaGizmo();palette_->stopPlacing();
    if(camera) {
        auto records=cameraControls_->records();records.at(camera->sourceIndex)=*camera;
        canvas_->previewAreaCameras(records,camera->sourceIndex);
    } else canvas_->useViewerCamera(false);
    updateActions();
}

void ModuleEditor::editCamera(const neomdl::AreaCameraRecord& camera) {
    rememberSelection();auto& resource=resources_.at({areaName(),kGit});
    auto& list=static_cast<neogff::GffList&>(*resource.model->gff().root()->GetFieldByLabel("CameraList"));auto copy=list.GetStruct(camera.sourceIndex)->Clone();auto& record=static_cast<neogff::GffStruct&>(*copy);
    setField(record,std::make_unique<neogff::GffIntField>("CameraID",camera.cameraId));
    setField(record,std::make_unique<neogff::GffPositionField>("Position",camera.position.x,camera.position.y,camera.position.z));
    setField(record,std::make_unique<neogff::GffOrientationField>("Orientation",camera.orientation.w,camera.orientation.x,camera.orientation.y,camera.orientation.z));
    for(const auto& [name,value]:std::array<std::pair<const char*,float>,4>{{{"Pitch",camera.pitch},{"Height",camera.height},{"FieldOfView",camera.fieldOfView},{"MicRange",camera.microphoneRange}}})setField(record,std::make_unique<neogff::GffFloatField>(name,value));
    list.allStructs().at(camera.sourceIndex).reset(static_cast<neogff::GffStruct*>(copy.release()));resource.model->gff().dirty(true);stage(resource);
    fields_->activateResource(resource.source.identity);fields_->refreshActiveDocument();fields_->selectField("CameraList\\"+std::to_string(camera.sourceIndex));
    rebuildObjects();recordEdit("Edit camera");
}

neomdl::ResourceResolver ModuleEditor::moduleResources() const {
    std::map<Key,ResourceDocument> edits;
    for(const auto& [key,resource]:resources_)if(exportsResource(key))edits.emplace(key,resource.source);
    for(const auto& [key,source]:addedTemplates_)edits.insert_or_assign(key,source);
    const auto local=localDirectory_.empty()?neomdl::ResourceResolver{}:neomdl::directoryResources(localDirectory_);
    return [edits=std::make_shared<const std::map<Key,ResourceDocument>>(std::move(edits)),archives=archiveResources_,local,game=session_?session_->resolver():hostResources_](const std::string& name,std::uint16_t type)->std::optional<ResourceDocument> {
        const Key key{neomdl::resourceKey(name),type};if(const auto found=edits->find(key);found!=edits->end())return found->second;
        if(auto source=archives(name,type))return source;
        if(local)if(auto source=local(name,type))return source;
        return game(name,type);
    };
}

Resource& ModuleEditor::openBlueprintResource(ResourceDocument source,const neomdl::ResourceResolver& resolver) {
    const auto key=resourceKey(source);auto found=resources_.find(key);
    if(found!=resources_.end()) {fields_->activateResource(found->second.source.identity);return found->second;}
    const auto snapshot=std::make_shared<const std::vector<std::uint8_t>>(source.bytes);
    for(auto& state:history_) {state.bytes.emplace(key,snapshot);state.origins.emplace(key,resolver);}
    templateOrigins_.emplace(key,resolver);savedBytes_.emplace(key,snapshot);identityKeys_[source.identity]=key;
    inputs_.insert(inputs_.end(),source.protectedInputs.begin(),source.protectedInputs.end());
    fields_->openResource(source);auto& resource=resources_.emplace(key,Resource{std::move(source),fields_->activeModel(),snapshot}).first->second;
    return resource;
}

void ModuleEditor::editBlueprint() {
    const auto& p=placements_.at(objects_->GetSelection());
    const auto& kind=*std::find_if(kObjectKinds.begin(),kObjectKinds.end(),[&](const auto& k){return p.list==k.list;});
    const auto& root=*resources_.at(identityKeys_.at(p.identity)).model->gff().root();
    const auto& record=*static_cast<const neogff::GffList&>(*root.GetFieldByLabel(p.list)).GetStruct(p.index);
    const auto name=neomdl::resourceKey(record.GetFieldByLabel(kind.reference)->GetString());auto resolver=moduleResources();
    auto& resource=openBlueprintResource(resolver(name,kind.type).value(),resolver);blueprintKey_=resourceKey(resource.source);
    if(!blueprintEditor_) {
        BlueprintEditor::Actions actions;
        actions.edit=[this](const std::string& path,const std::string& value,const std::string& label){
            rememberSelection();auto& resource=resources_.at(blueprintKey_);
            resource.model->setValue(path,value);stage(resource);fields_->activateResource(resource.source.identity);fields_->refreshActiveDocument();fields_->selectField(path);
            recordEdit("Edit blueprint "+label);requestPreview();
        };
        actions.addItem=[this](ResourceDocument item,const neomdl::ResourceResolver& resolver){
            rememberSelection();const auto name=importTemplate(std::move(item),resolver);auto& resource=resources_.at(blueprintKey_);auto& root=*resource.model->gff().root();
            if(!root.GetFieldByLabel("ItemList"))root.AddField(std::make_unique<neogff::GffList>("ItemList"));
            auto& list=static_cast<neogff::GffList&>(*root.GetFieldByLabel("ItemList"));auto entry=std::make_unique<neogff::GffStruct>();entry->typeid_=0;
            entry->AddField(std::make_unique<neogff::GffResRefField>("InventoryRes",name));entry->AddField(std::make_unique<neogff::GffByteField>("Infinite",0));
            list.AddStruct(std::move(entry));resource.model->gff().dirty(true);stage(resource);fields_->refreshActiveDocument();recordEdit("Add shop item");blueprintEditor_->selectInventory(list.count()-1);requestPreview();
        };
        actions.stock=[this](BlueprintEditor::StockAction action,std::size_t index,bool enabled){
            rememberSelection();auto& resource=resources_.at(blueprintKey_);auto& list=static_cast<neogff::GffList&>(*resource.model->gff().root()->GetFieldByLabel("ItemList"));
            if(action==BlueprintEditor::StockAction::Remove)list.allStructs().erase(list.allStructs().begin()+index);
            else if(action==BlueprintEditor::StockAction::Duplicate) {auto copy=list.GetStruct(index)->Clone();list.AddStruct(static_cast<neogff::GffStruct*>(copy.release()));}
            else {auto& item=*list.GetStruct(index);const auto* flag=static_cast<const neogff::GffByteField*>(item.GetFieldByLabel("Infinite"));const auto value=(flag?flag->value:0u)&~1u;setField(item,std::make_unique<neogff::GffByteField>("Infinite",std::uint8_t(value|(enabled?1u:0u))));}
            resource.model->gff().dirty(true);stage(resource);fields_->refreshActiveDocument();recordEdit("Edit shop stock");requestPreview();
        };
        actions.itemFields=[this](const std::string& name,std::uint16_t type){auto resolver=moduleResources();openBlueprintResource(resolver(name,type).value(),resolver);blueprintEditor_->Hide();fields_->SetFocus();};
        actions.fields=[this]{fields_->activateResource(resources_.at(blueprintKey_).source.identity);fields_->refreshActiveDocument();fields_->SetFocus();};
        actions.save=[this]{saveResources();};actions.package=[this]{packageModule();};actions.undo=[this]{restoreEdit(historyIndex_-1);};actions.redo=[this]{restoreEdit(historyIndex_+1);};
        blueprintEditor_=new BlueprintEditor(this,std::move(actions));
    }
    blueprintEditor_->open(kind.type,game_,resource.source.fileName,*resource.model->gff().root(),moduleResources(),session_,catalog_,localDirectory_);
    wxui::applyTheme(blueprintEditor_,dark_);rememberSelection();updateActions();
}

void ModuleEditor::refreshBlueprintEditor() {
    if(blueprintEditor_&&blueprintEditor_->IsShown())blueprintEditor_->refresh(*resources_.at(blueprintKey_).model->gff().root(),moduleResources());
}

bool ModuleEditor::blueprintChanged(const EditState& state) const {
    return blueprintEditor_&&blueprintEditor_->IsShown()&&std::any_of(resources_.begin(),resources_.end(),[&](const auto& entry){
        return (entry.first==blueprintKey_||entry.first.second==2025)&&entry.second.source.bytes!=*state.bytes.at(entry.first);
    });
}

EditState ModuleEditor::captureEdit(const std::string& action) const {
    EditState state;
    for(const auto& [key,resource]:resources_)state.bytes.emplace(key,resource.snapshot);
    state.templates=addedTemplates_;state.imports=importedTemplates_;
    state.origins=templateOrigins_;
    state.inlineSources=inlineSources_;
    state.area=areaName();state.path=selectedPath_;state.identity=fields_->activeResourceIdentity();state.action=action;
    return state;
}

void ModuleEditor::editModuleAreaList(ModuleControls::AreaAction action,std::size_t index,const std::string& name) {
    rememberSelection();auto& resource=resources_.at({"module",kIfo});auto& root=*resource.model->gff().root();
    if(!root.GetFieldByLabel("Mod_Area_list"))root.AddField(std::make_unique<neogff::GffList>("Mod_Area_list"));
    auto& list=static_cast<neogff::GffList&>(*root.GetFieldByLabel("Mod_Area_list"));auto& records=list.allStructs();
    using Action=ModuleControls::AreaAction;
    switch(action) {
    case Action::Add: {
        auto area=std::make_unique<neogff::GffStruct>();area->typeid_=6;
        area->AddField(std::make_unique<neogff::GffResRefField>("Area_Name",neomdl::resourceKey(name)));list.AddStruct(std::move(area));break;
    }
    case Action::Rename:setField(*records.at(index),std::make_unique<neogff::GffResRefField>("Area_Name",neomdl::resourceKey(name)));break;
    case Action::Remove:records.erase(records.begin()+index);break;
    case Action::Up:std::swap(records.at(index),records.at(index-1));break;
    case Action::Down:std::swap(records.at(index),records.at(index+1));break;
    }
    resource.model->gff().dirty(true);stage(resource);fields_->activateResource(resource.source.identity);fields_->refreshActiveDocument();
    refreshModuleControls();rebuildObjects();recordEdit("Edit module area list");requestPreview();
}

void ModuleEditor::rememberSelection() {
    if(history_.empty())return;
    auto& state=history_[historyIndex_];
    state.area=areaName();state.path=selectedPath_;state.identity=fields_->activeResourceIdentity();
}

void ModuleEditor::updateDirty() {
    for(auto& [key,resource]:resources_)resource.model->gff().dirty(resource.source.bytes!=*savedBytes_.at(key));
    std::set<Key> keys;for(const auto& [key,source]:addedTemplates_)keys.insert(key);
    templatesDirty_=keys!=savedTemplateKeys_;
}

void ModuleEditor::recordEdit(const std::string& action) {
    auto state=captureEdit(action);
    const auto& current=history_[historyIndex_];
    if(state.bytes==current.bytes&&state.imports==current.imports)return;
    const bool blueprint=blueprintChanged(current);
    history_.erase(history_.begin()+historyIndex_+1,history_.end());
    history_.push_back(std::move(state));++historyIndex_;
    updateDirty();updateActions();
    if(blueprint)refreshBlueprintEditor();
}

void ModuleEditor::restoreEdit(std::size_t index) {
    canvas_->cancelAreaPolygon();
    canvas_->cancelAreaGizmo();
    const auto& state=history_.at(index);
    const bool blueprint=blueprintChanged(state);
    selecting_=true;
    for(auto& [key,resource]:resources_) {
        if(resource.source.bytes!=*state.bytes.at(key)) {
            resource.source.bytes=*state.bytes.at(key);
            resource.model->gff().LoadBytes(resource.source.bytes);
        }
        resource.snapshot=state.bytes.at(key);
    }
    addedTemplates_=state.templates;importedTemplates_=state.imports;
    templateOrigins_=state.origins;
    inlineSources_=state.inlineSources;
    historyIndex_=index;selectedPath_=state.path;
    const auto areaChanged=areaName()!=state.area;areas_->SetStringSelection(wxui::toWx(state.area));
    if(areaChanged) {canvas_->clearArea();previewCurrent_=false;ownerPaths_.clear();pathOwners_.clear();}
    updateDirty();fields_->activateResource(state.identity);fields_->refreshActiveDocument();rebuildObjects();refreshAreaControls();
    const auto selected=objects_->GetSelection();
    if(selected!=wxNOT_FOUND&&placements_[selected].identity==state.identity)selectObject(selected,true);
    selecting_=false;requestPreview();
    if(blueprint)refreshBlueprintEditor();
    setModuleStatusText(index==0?"Restored the original module resources.":"Restored: "+wxui::toWx(state.action));
}

void ModuleEditor::updateGizmo() {
    if(canvas_->areaGizmoDragging()||canvas_->areaPolygonDragging()||canvas_->areaPolygonDrawing())return;
    const auto selected=objects_->GetSelection();
    const bool sceneReady=previewCurrent_&&!requested_&&!loading_.valid()&&!cameraControls_->previewing();
    const bool ready=sceneReady&&!palette_->placing()&&selected!=wxNOT_FOUND;
    std::vector<neomdl::ui::AreaEditorMarker> markers;
    if(sceneReady)for(const auto& p:placements_) {
        using Kind=neomdl::ui::AreaMarkerKind;
        if(p.list=="WaypointList")markers.push_back({p.path,Kind::Waypoint,p.position});
        if(p.list=="SoundList")markers.push_back({p.path,Kind::Sound,p.position});
        if(p.list=="CameraList")markers.push_back({p.path,Kind::Camera,p.position});
        if(p.list=="List")markers.push_back({p.path,Kind::Item,p.position});
        if(p.list=="Encounter List")markers.push_back({p.path,Kind::Encounter,p.position});
        if(p.list=="StoreList")markers.push_back({p.path,Kind::Store,p.position});
        if(p.coordinates==Placement::Coordinates::Entry)markers.push_back({p.path,Kind::Entry,p.position});
    }
    canvas_->setAreaMarkers(std::move(markers),selectedPath_);
    const auto tool=gizmoTool_->GetSelection();
    auto mode=tool<3?static_cast<neomdl::ui::AreaGizmoMode>(tool):neomdl::ui::AreaGizmoMode::None;
    std::vector<Vec3> footprint;
    if(ready&&placements_[selected].list=="TriggerList") {
        for(const auto& trigger:canvas_->areaScene().triggers())if(trigger.sourceIndex==placements_[selected].index)footprint=trigger.worldFootprint;
    } else if(ready&&placements_[selected].list=="Encounter List") {
        const auto& p=placements_[selected];const auto& root=*resources_.at(identityKeys_.at(p.identity)).model->gff().root();
        const auto& record=*static_cast<const neogff::GffList&>(*root.GetFieldByLabel(p.list)).GetStruct(p.index);
        if(const auto* geometry=static_cast<const neogff::GffList*>(record.GetFieldByLabel("Geometry")))
            for(const auto& point:geometry->allStructs()) {
                const auto fields=fieldsOf(*point);footprint.push_back({p.position.x+scalar(fields,"X"),p.position.y+scalar(fields,"Y"),p.position.z+scalar(fields,"Z")});
            }
    }
    canvas_->setAreaPolygon(std::move(footprint),tool==3&&ready);
    if(ready) {
        const auto& p=placements_[selected];
        if(mode==neomdl::ui::AreaGizmoMode::Rotate&&p.rotation==Placement::Rotation::None) {
            canvas_->setAreaGizmo(neomdl::ui::AreaGizmoMode::None);
            gizmoHint_->SetLabel("This object has no heading field. Use Move or edit its orientation in Fields.");return;
        }
        canvas_->setAreaGizmo(mode,p.position,p.heading);
    } else canvas_->setAreaGizmo(neomdl::ui::AreaGizmoMode::None);
    gizmoHint_->SetLabel(tool==3?"Select a trigger or encounter, then drag its yellow vertices. Redraw footprint replaces the polygon.":mode==neomdl::ui::AreaGizmoMode::Move?
        "Drag X (red), Y (green), Z (blue), or the center for XY. Esc cancels.":
        mode==neomdl::ui::AreaGizmoMode::Rotate?"Drag the blue ring to change heading. Esc cancels.":
        "Select an object, then choose Move or Rotate.");
    gizmoHint_->SetToolTip(gizmoHint_->GetLabel());
}

void ModuleEditor::beginTriggerDrawing(bool create) {
    palette_->stopPlacing();canvas_->cancelAreaGizmo();canvas_->cancelAreaPolygon();rememberSelection();
    drawingNewTrigger_=create;gizmoTool_->SetSelection(3);canvas_->setAreaGizmo(neomdl::ui::AreaGizmoMode::None);
    const bool encounter=create?palette_->selectedType()==2040:placements_.at(objects_->GetSelection()).list=="Encounter List";
    const auto limit=encounter?std::optional<std::size_t>{}:std::optional<std::size_t>{neomdl::ui::kAreaTriggerVertexLimit};
    if(create) {
        const auto options=palette_->placementOptions();canvas_->beginAreaPolygon(options.planeZ,options.zOffset,limit);
    } else canvas_->beginAreaPolygon(placements_.at(objects_->GetSelection()).position.z,0,limit);
}

void ModuleEditor::writeTriggerGeometry(const Placement& p,const std::vector<Vec3>& points,int vertex) {
    auto& resource=resources_.at(identityKeys_.at(p.identity));
    auto& list=dynamic_cast<neogff::GffList&>(*resource.model->gff().root()->GetFieldByLabel(p.list));
    auto& record=*list.GetStruct(p.index);
    const auto* original=dynamic_cast<const neogff::GffList*>(record.GetFieldByLabel("Geometry"));
    auto geometry=std::make_unique<neogff::GffList>("Geometry");
    const bool encounter=p.list=="Encounter List";
    const std::array<const char*,3> labels=encounter?std::array<const char*,3>{"X","Y","Z"}:std::array<const char*,3>{"PointX","PointY","PointZ"};
    const float origin[]={p.position.x,p.position.y,p.position.z};
    for(std::size_t i=0;i<points.size();++i) {
        std::unique_ptr<neogff::GffStruct> point;
        const bool inherited=!original||i>=original->count();
        if(!inherited) {auto copy=original->GetStruct(i)->Clone();point.reset(static_cast<neogff::GffStruct*>(copy.release()));}
        else {point=std::make_unique<neogff::GffStruct>();point->typeid_=encounter?std::uint32_t(i):3;}
        if(vertex<0||i==std::size_t(vertex)||inherited) {
            const float values[]={points[i].x,points[i].y,points[i].z};
            for(std::size_t axis=0;axis<3;++axis) {
                if(!inherited&&scalar(fieldsOf(*point),labels[axis])+origin[axis]==values[axis])continue;
                setField(*point,std::make_unique<neogff::GffFloatField>(labels[axis],values[axis]-origin[axis]));
            }
        }
        geometry->AddStruct(std::move(point));
    }
    setField(record,std::move(geometry));resource.model->gff().dirty(true);stage(resource);
}

void ModuleEditor::polygonChanged(const neomdl::ui::AreaPolygonEvent& event) {
    try {
        if(event.phase==neomdl::ui::AreaGizmoPhase::Begin) {
            rememberSelection();updateActions();
            gizmoHint_->SetLabel(event.drawing?"Click corners; click the first point, double-click, or press Enter to finish. Backspace removes a corner. Esc cancels.":
                "Drag the vertex in XY. Grid and walkmesh settings apply. Esc cancels.");
            gizmoHint_->SetToolTip(gizmoHint_->GetLabel());return;
        }
        if(event.drawing) {
            if(event.phase==neomdl::ui::AreaGizmoPhase::Commit) {
                if(drawingNewTrigger_)insertObject(event.points.front(),palette_->placementOptions(),event.points);
                else {
                    const auto p=placements_.at(objects_->GetSelection());
                    if(p.list=="TriggerList")canvas_->previewAreaTriggerGeometry(p.index,event.points);
                    writeTriggerGeometry(p,event.points,-1);
                    fields_->refreshActiveDocument();rebuildObjects();recordEdit("Redraw polygon footprint");requestPreview();
                }
            } else if(event.phase==neomdl::ui::AreaGizmoPhase::Preview)
                setModuleStatusText(wxString::Format("Polygon: %zu corners. Enter finishes; Backspace removes the last corner.",event.points.size()));
        } else {
            const auto p=placements_.at(objects_->GetSelection());if(p.list=="TriggerList")canvas_->previewAreaTriggerGeometry(p.index,event.points);
            if(event.phase==neomdl::ui::AreaGizmoPhase::Commit) {
                writeTriggerGeometry(p,event.points,event.vertex);fields_->refreshActiveDocument();rebuildObjects();
                recordEdit("Move polygon vertex");requestPreview();
            }
        }
        updateActions();
    } catch(const std::exception& e) {canvas_->cancelAreaPolygon();updateActions();wxui::showError(this,e);}
}

void ModuleEditor::gizmoChanged(const neomdl::ui::AreaGizmoEvent& event) {
    try {
        if(event.phase==neomdl::ui::AreaGizmoPhase::Begin) {rememberSelection();updateActions();return;}
        const auto& p=placements_.at(objects_->GetSelection());
        const float values[]={event.position.x,event.position.y,event.position.z,event.heading};
        for(std::size_t i=0;i<4;++i)transform_[i]->ChangeValue(wxui::toWx(decimal(values[i])));
        if(event.phase==neomdl::ui::AreaGizmoPhase::Commit)applyTransform();
        else previewTransform(p,event.position,event.heading);
        if(event.phase!=neomdl::ui::AreaGizmoPhase::Preview)updateActions();
    } catch(const std::exception& e) {canvas_->cancelAreaGizmo();wxui::showError(this,e);}
}

void ModuleEditor::previewTransform(const Placement& p,Vec3 position,float heading) {
    canvas_->moveAreaMarker(p.path,position);
    if(p.list=="TriggerList")canvas_->previewAreaTrigger(p.index,position,heading-p.heading);
    if(p.list=="CameraList")canvas_->previewAreaCamera(p.index,position);
    if(p.list=="Encounter List") {
        const auto& root=*resources_.at(identityKeys_.at(p.identity)).model->gff().root();
        const auto& record=*static_cast<const neogff::GffList&>(*root.GetFieldByLabel(p.list)).GetStruct(p.index);
        std::vector<Vec3> points;
        if(const auto* geometry=static_cast<const neogff::GffList*>(record.GetFieldByLabel("Geometry")))for(const auto& point:geometry->allStructs()) {
            const auto fields=fieldsOf(*point);points.push_back({position.x+scalar(fields,"X"),position.y+scalar(fields,"Y"),position.z+scalar(fields,"Z")});
        }
        canvas_->setAreaPolygon(std::move(points),gizmoTool_->GetSelection()==3);
    }
    for(const auto& placement:canvas_->areaScene().placements()) {
        const auto list=placement.kind==neomdl::AreaObjectKind::Creature?"Creature List":
            placement.kind==neomdl::AreaObjectKind::Door?"Door List":"Placeable List";
        if(p.list==list&&p.index==placement.sourceIndex&&placement.runtimeInstance) {
            const auto orientation=heading!=p.heading?neomdl::gitBearingFrame({},heading/kDegrees).orientation:placement.frame.orientation;
            canvas_->setSceneOwnerWorld(*placement.runtimeInstance,neomdl::model::nodeTransform(position,orientation));
        }
    }
}

void ModuleEditor::rebuildObjects() {
    placements_.clear();objects_->Clear();
    const auto git=resources_.find({areaName(),kGit});
    if(git!=resources_.end()) {
        const auto& identity=git->second.source.identity;
        for(const auto& field:git->second.model->gff().root()->allFields()) {
            const auto* list=dynamic_cast<const neogff::GffList*>(field.get());
            if(!list)continue;
            for(std::size_t i=0;i<list->count();++i) {
                const auto& record=*list->GetStruct(i);
                const auto fields=fieldsOf(record);
                Placement p;p.identity=identity;p.list=list->GetLabel();p.index=i;
                p.path=p.list+"\\"+std::to_string(i);
                if(fields.count("Position")) {
                    const auto& point=dynamic_cast<const neogff::GffPositionField&>(*fields.at("Position"));
                    p.coordinates=Placement::Coordinates::Vector;
                    p.position={point.value[0],point.value[1],point.value[2]};
                } else if(fields.count("X")||fields.count("XPosition")) {
                    const bool shortNames=fields.count("X")!=0;
                    p.coordinates=shortNames?Placement::Coordinates::Short:Placement::Coordinates::Long;
                    p.position={scalar(fields,shortNames?"X":"XPosition"),
                        scalar(fields,shortNames?"Y":"YPosition"),scalar(fields,shortNames?"Z":"ZPosition")};
                } else continue;
                if(fields.count("Bearing")) {
                    p.rotation=Placement::Rotation::Bearing;p.heading=scalar(fields,"Bearing")*kDegrees;
                } else if(fields.count("XOrientation")) {
                    p.rotation=Placement::Rotation::Direction;
                    p.heading=std::atan2(-scalar(fields,"XOrientation"),scalar(fields,"YOrientation"))*kDegrees;
                }
                p.label=p.list+" ["+std::to_string(i)+"]";
                for(const auto* name:{"Tag","TemplateResRef","ResRef","CameraID"})if(const auto* value=record.GetFieldByLabel(name)) {
                    if(!value->GetString().empty()) {p.label+=" — "+value->GetString();break;}
                }
                placements_.push_back(std::move(p));
            }
        }
    }
    const auto ifo=resources_.find({"module",kIfo});
    if(ifo!=resources_.end()) {
        const auto fields=fieldsOf(*ifo->second.model->gff().root());
        if(neomdl::resourceKey(fields.at("Mod_Entry_Area")->GetString())==areaName()) {
            Placement p;p.identity=ifo->second.source.identity;p.path="$entry";p.label="Module entry";
            p.coordinates=Placement::Coordinates::Entry;p.rotation=Placement::Rotation::Entry;
            p.position={scalar(fields,"Mod_Entry_X"),scalar(fields,"Mod_Entry_Y"),scalar(fields,"Mod_Entry_Z")};
            p.heading=std::atan2(-scalar(fields,"Mod_Entry_Dir_X"),scalar(fields,"Mod_Entry_Dir_Y"))*kDegrees;
            placements_.push_back(std::move(p));
        }
    }
    for(const auto& placement:placements_)objects_->Append(wxui::toWx(placement.label));
    refreshCameraControls();
    selectObject(findPlacement(selectedPath_),false);
}

void ModuleEditor::selectObject(int index,bool selectField) {
    objects_->SetSelection(index);
    selectedPath_=index==wxNOT_FOUND?std::string{}:placements_.at(index).path;
    canvas_->setSelectedAreaOwners(pathOwners_[selectedPath_]);
    if(index!=wxNOT_FOUND) {
        const auto& p=placements_[index];
        if(p.list=="CameraList") {
            cameraControls_->select(p.index);
            if(cameraControls_->previewing()&&previewCurrent_&&!requested_&&!loading_.valid())previewCamera(cameraControls_->pendingCamera());
        }
        std::string source="Inline GIT fields supply this object's properties.";
        const auto& root=*resources_.at(identityKeys_.at(p.identity)).model->gff().root();
        const auto* templates=dynamic_cast<const neogff::GffByteField*>(root.GetFieldByLabel("UseTemplates"));
        if(p.coordinates==Placement::Coordinates::Entry)source="Entry position and direction are saved in module.ifo.";
        else if(p.list=="WaypointList")source="Waypoint properties are embedded in GIT. The engine grounds their height on the walkmesh.";
        else if(p.list=="CameraList")source="GIT supplies camera position and quaternion orientation. K1 allows 50 cameras; K2 allows 70.";
        else if(templates&&templates->value) {
            if(p.list=="Door List")source="UTD supplies door state. GIT supplies placement, links, and K2 tag/tweak color.";
            else if(p.list=="Placeable List")source="UTP supplies appearance, animation and inventory. GIT supplies placement and K2 tweak color.";
            else if(p.list=="Creature List")source="UTC supplies creature properties. GIT supplies placement; the engine may adjust it to a safe location.";
            else if(p.list=="TriggerList")source="UTT supplies type, scripts and facing. GIT supplies position, links and footprint; rotation edits the footprint.";
            else if(p.list=="SoundList")source="UTS supplies sound properties. GIT supplies position and generated type.";
            else if(p.list=="List")source="UTI supplies item properties. GIT supplies placement and facing; the item is shown with a selectable marker.";
            else if(p.list=="StoreList")source="UTM supplies store properties and inventory. GIT uses ResRef for its template, plus position and facing. The store is shown with a selectable marker.";
            else if(p.list=="Encounter List")source="UTE supplies encounter rules and creatures. GIT supplies the footprint and absolute spawn points. Moving an encounter also moves its spawn points.";
        }
        objectSource_->SetLabel(wxui::toWx(source));objectSource_->Wrap(FromDIP(195));objectSource_->GetParent()->Layout();
        const float values[]={p.position.x,p.position.y,p.position.z,p.heading};
        for(std::size_t i=0;i<4;++i)transform_[i]->ChangeValue(wxui::toWx(decimal(values[i])));
        if(selectField) {
            selecting_=true;
            fields_->activateResource(p.identity);
            fields_->selectField(p.coordinates==Placement::Coordinates::Entry?"Mod_Entry_X":p.path);
            selecting_=false;
        }
    } else {
        for(auto* field:transform_)field->Clear();
        objectSource_->SetLabel("Select an object to see where its properties load from.");
        objectSource_->Wrap(FromDIP(195));
    }
    updateActions();
}

void ModuleEditor::applyTransform() {
    const auto p=placements_.at(objects_->GetSelection());
    auto& resource=resources_.at(identityKeys_.at(p.identity));
    float values[4];
    for(std::size_t i=0;i<3;++i)values[i]=neogff::ParseFloatDecimal(wxui::toStd(transform_[i]->GetValue()));
    if(p.rotation!=Placement::Rotation::None)values[3]=neogff::ParseFloatDecimal(wxui::toStd(transform_[3]->GetValue()));
    const float previous[]={p.position.x,p.position.y,p.position.z};
    const bool positionChanged=values[0]!=previous[0]||values[1]!=previous[1]||values[2]!=previous[2];
    const bool rotationChanged=p.rotation!=Placement::Rotation::None&&values[3]!=p.heading;
    if(!positionChanged&&!rotationChanged)return;
    rememberSelection();
    if(p.list=="Encounter List"&&positionChanged) {
        auto& record=*static_cast<neogff::GffList&>(*resource.model->gff().root()->GetFieldByLabel(p.list)).GetStruct(p.index);
        if(auto* spawns=static_cast<neogff::GffList*>(record.GetFieldByLabel("SpawnPointList")))for(auto& spawn:spawns->allStructs()) {
            const auto fields=fieldsOf(*spawn);const char* labels[]={"X","Y","Z"};
            for(std::size_t axis=0;axis<3;++axis)if(values[axis]!=previous[axis])setField(*spawn,std::make_unique<neogff::GffFloatField>(labels[axis],scalar(fields,labels[axis])+values[axis]-previous[axis]));
        }
    }
    if(p.list=="TriggerList") {
        auto& list=dynamic_cast<neogff::GffList&>(*resource.model->gff().root()->GetFieldByLabel(p.list));
        auto& record=*list.GetStruct(p.index);
        const auto* original=dynamic_cast<const neogff::GffList*>(record.GetFieldByLabel("Geometry"));
        std::unique_ptr<neogff::GffList> geometry;
        const auto angle=rotationChanged?(values[3]-p.heading)/kDegrees:0.0f;
        if(original&&rotationChanged) {
            auto copy=original->Clone();geometry.reset(static_cast<neogff::GffList*>(copy.release()));
            for(auto& point:geometry->allStructs()) {
                const auto fields=fieldsOf(*point);const auto x=scalar(fields,"PointX"),y=scalar(fields,"PointY");
                setField(*point,std::make_unique<neogff::GffFloatField>("PointX",x*std::cos(angle)-y*std::sin(angle)));
                setField(*point,std::make_unique<neogff::GffFloatField>("PointY",x*std::sin(angle)+y*std::cos(angle)));
            }
        } else if(!original&&previewCurrent_) {
            const auto& triggers=canvas_->areaScene().triggers();
            const auto& trigger=*std::find_if(triggers.begin(),triggers.end(),[&](const auto& value){return value.sourceIndex==p.index;});
            geometry=std::make_unique<neogff::GffList>("Geometry");
            for(const auto& point:trigger.worldFootprint) {
                const auto x=point.x-p.position.x,y=point.y-p.position.y;
                auto vertex=std::make_unique<neogff::GffStruct>();vertex->typeid_=3;
                vertex->AddField(std::make_unique<neogff::GffFloatField>("PointX",x*std::cos(angle)-y*std::sin(angle)));
                vertex->AddField(std::make_unique<neogff::GffFloatField>("PointY",x*std::sin(angle)+y*std::cos(angle)));
                vertex->AddField(std::make_unique<neogff::GffFloatField>("PointZ",point.z-p.position.z));
                geometry->AddStruct(std::move(vertex));
            }
        }
        if(geometry)setField(record,std::move(geometry));
    }
    const auto set=[&](const std::string& name,float value) {
        resource.model->setValue(p.coordinates==Placement::Coordinates::Entry?name:p.path+"\\"+name,decimal(value));
    };
    if(p.coordinates==Placement::Coordinates::Vector&&positionChanged)
        resource.model->setValue(p.path+"\\Position",decimal(values[0])+"|"+decimal(values[1])+"|"+decimal(values[2]));
    else if(p.coordinates!=Placement::Coordinates::Vector) {
        const char* shortNames[]={"X","Y","Z"};
        const char* longNames[]={"XPosition","YPosition","ZPosition"};
        const char* entryNames[]={"Mod_Entry_X","Mod_Entry_Y","Mod_Entry_Z"};
        for(std::size_t i=0;i<3;++i)if(values[i]!=previous[i])set(p.coordinates==Placement::Coordinates::Entry?entryNames[i]:
                p.coordinates==Placement::Coordinates::Short?shortNames[i]:longNames[i],values[i]);
    }
    const float heading=p.rotation==Placement::Rotation::None?0.0f:values[3]/kDegrees;
    if(rotationChanged&&p.rotation==Placement::Rotation::Bearing)set("Bearing",heading);
    if(rotationChanged&&(p.rotation==Placement::Rotation::Direction||p.rotation==Placement::Rotation::Entry)) {
        set(p.rotation==Placement::Rotation::Entry?"Mod_Entry_Dir_X":"XOrientation",-std::sin(heading));
        set(p.rotation==Placement::Rotation::Entry?"Mod_Entry_Dir_Y":"YOrientation",std::cos(heading));
    }
    previewTransform(p,{values[0],values[1],values[2]},rotationChanged?values[3]:p.heading);
    stage(resource);fields_->refreshActiveDocument();rebuildObjects();
    recordEdit(positionChanged?(rotationChanged?"Transform object":"Move object"):"Rotate object");requestPreview();
}

void ModuleEditor::duplicateObject() {
    rememberSelection();
    const auto p=placements_.at(objects_->GetSelection());
    auto& resource=resources_.at(identityKeys_.at(p.identity));
    auto& list=static_cast<neogff::GffList&>(*resource.model->gff().root()->GetFieldByLabel(p.list));
    auto copy=list.GetStruct(p.index)->Clone();
    if(p.list=="CameraList") {
        std::set<std::int32_t> ids;
        for(const auto& camera:list.allStructs())ids.insert(static_cast<const neogff::GffIntField&>(*camera->GetFieldByLabel("CameraID")).value);
        std::int32_t id=0;while(ids.count(id))++id;
        static_cast<neogff::GffIntField&>(*static_cast<neogff::GffStruct&>(*copy).GetFieldByLabel("CameraID")).value=id;
    }
    selectedPath_=p.list+"\\"+std::to_string(list.count());
    list.AddStruct(static_cast<neogff::GffStruct*>(copy.release()));resource.model->gff().dirty(true);
    stage(resource);fields_->refreshActiveDocument();rebuildObjects();recordEdit("Duplicate object");requestPreview();
    selectObject(findPlacement(selectedPath_),true);
}

void ModuleEditor::deleteObject() {
    const auto p=placements_.at(objects_->GetSelection());
    if(wxMessageBox("Delete "+wxui::toWx(p.label)+"?","Delete GIT object",wxYES_NO|wxICON_QUESTION,this)!=wxYES)return;
    rememberSelection();
    auto& resource=resources_.at(identityKeys_.at(p.identity));
    resource.model->deleteField(p.path);selectedPath_.clear();
    stage(resource);fields_->refreshActiveDocument();rebuildObjects();recordEdit("Delete object");requestPreview();
}

std::optional<ResourceDocument> ModuleEditor::sceneTemplate(const std::string& name,std::uint16_t type) const {
    if(const auto found=addedTemplates_.find({name,type});found!=addedTemplates_.end())return found->second;
    if(auto source=archiveResources_(name,type))return source;
    if(!localDirectory_.empty())if(auto source=neomdl::directoryResources(localDirectory_)(name,type))return source;
    return session_->resolve(name,type);
}

bool ModuleEditor::importItemReferences(neogff::GffStruct& record,const neomdl::ResourceResolver& resolver) {
    bool changed=false;
    for(const auto& [label,reference]:std::array<std::pair<const char*,const char*>,2>{{
            {"Equip_ItemList","EquippedRes"},{"ItemList","InventoryRes"}}}) {
        if(auto* list=dynamic_cast<neogff::GffList*>(record.GetFieldByLabel(label)))for(const auto& item:list->allStructs()) {
            const auto* field=item->GetFieldByLabel(reference);
            if(!field)continue;
            const auto name=neomdl::resourceKey(field->GetString());
            const auto imported=importTemplate(resolver(name,2025).value(),resolver);
            if(imported!=name) {setField(*item,std::make_unique<neogff::GffResRefField>(reference,imported));changed=true;}
        }
    }
    if(auto* creatures=static_cast<neogff::GffList*>(record.GetFieldByLabel("CreatureList")))for(auto& creature:creatures->allStructs()) {
        const auto name=neomdl::resourceKey(creature->GetFieldByLabel("ResRef")->GetString());
        const auto imported=importTemplate(resolver(name,2027).value(),resolver);
        if(imported!=name) {setField(*creature,std::make_unique<neogff::GffResRefField>("ResRef",imported));changed=true;}
    }
    return changed;
}

std::string ModuleEditor::importTemplate(ResourceDocument source,const neomdl::ResourceResolver& resolver) {
    const auto identity=source.identity;
    if(const auto found=importedTemplates_.find(identity);found!=importedTemplates_.end())return found->second;
    const auto key=resourceKey(source);
    neogff::GffFile file;file.LoadBytes(source.bytes);
    const auto changed=importItemReferences(*file.root(),resolver);
    const auto existing=sceneTemplate(key.first,key.second);
    if(!changed&&existing&&existing->bytes==source.bytes) {
        importedTemplates_[identity]=key.first;return key.first;
    }
    std::string name=key.first;
    if(existing) {
        std::size_t counter=1;
        do {
            const auto suffix="_neo"+std::to_string(counter++);
            name=key.first.substr(0,16-suffix.size())+suffix;
        } while(sceneTemplate(name,key.second));
    }
    source.fileName=name+"."+neomdl::resourceExtension(source.type);
    if(changed)source.bytes=file.SaveBytes();
    source.identity="module-import:"+identity+"#"+name;
    inputs_.insert(inputs_.end(),source.protectedInputs.begin(),source.protectedInputs.end());
    addedTemplates_.emplace(Key{name,source.type},std::move(source));
    templateOrigins_[{name,key.second}]=resolver;
    importedTemplates_[identity]=name;templatesDirty_=true;return name;
}

bool ModuleEditor::dropObject(wxPoint point) {
    if(!canPlaceObject())return false;
    try {
        const auto options=palette_->placementOptions();
        auto position=canvas_->areaPlacementPoint(point,options.planeZ);
        if(position)position=canvas_->snappedAreaPoint(*position);
        if(!position) {
            setModuleStatusText("No placement intersection for the selected surface and snapping settings.");return false;
        }
        position->z+=options.zOffset;insertObject(*position,options);return true;
    } catch(const std::exception& e) {wxui::showError(this,e);return false;}
}

void ModuleEditor::insertObject(Vec3 position,const ObjectPlacementOptions& options,const std::vector<Vec3>& polygon) {
    const auto blueprint=palette_->selectedBlueprint();
    const auto resolver=palette_->selectedResources();
    rememberSelection();
    const auto& kind=*std::find_if(kObjectKinds.begin(),kObjectKinds.end(),[&](const auto& value){return value.type==blueprint.type;});
    auto& resource=resources_.at({areaName(),kGit});
    auto& root=*resource.model->gff().root();
    const auto* flag=dynamic_cast<const neogff::GffByteField*>(root.GetFieldByLabel("UseTemplates"));
    const bool templates=flag&&flag->value!=0;
    neogff::GffFile file;file.LoadBytes(blueprint.bytes);
    auto instance=std::make_unique<neogff::GffStruct>();
    if(!templates||kind.type==2058) {
        auto copy=file.root()->Clone();instance.reset(static_cast<neogff::GffStruct*>(copy.release()));
    } else {
        const auto copy=[&](std::initializer_list<const char*> names) {
            for(const auto* name:names)if(const auto* field=file.root()->GetFieldByLabel(name))instance->AddField(field->Clone());
        };
        if(kind.type==2042||kind.type==2032)copy({"LinkedTo","LinkedToModule","LinkedToFlags","TransitionDestin"});
        if(game_==neomdl::model::GameVersion::Kotor2) {
            if(kind.type==2042)copy({"Tag"});
            if(kind.type==2042||kind.type==2044)copy({"UseTweakColor","TweakColor"});
        }
    }
    instance->typeid_=kind.structure;
    const auto reference=templates&&kind.type!=2058?importTemplate(blueprint,resolver):resourceKey(blueprint).first;
    if(!templates) {importItemReferences(*instance,resolver);inlineSources_.emplace(blueprint.identity,ModulePackageInput{blueprint,resolver});}
    setField(*instance,std::make_unique<neogff::GffResRefField>(kind.reference,reference));
    const auto number=[&](const char* name,float value){setField(*instance,std::make_unique<neogff::GffFloatField>(name,value));};
    number(kind.shortCoordinates?"X":"XPosition",position.x);
    number(kind.shortCoordinates?"Y":"YPosition",position.y);
    number(kind.shortCoordinates?"Z":"ZPosition",position.z);
    const auto angle=canvas_->snappedAreaHeading(options.heading)/kDegrees;
    if(kind.bearing)number("Bearing",angle);
    else if(kind.type!=2035&&kind.type!=2040) {number("XOrientation",-std::sin(angle));number("YOrientation",std::cos(angle));number("ZOrientation",0.0f);}
    if(kind.type==2035)setField(*instance,std::make_unique<neogff::GffUInt32Field>("GeneratedType",0));
    if(kind.type==2032||kind.type==2040) {
        const bool encounter=kind.type==2040;
        auto geometry=std::make_unique<neogff::GffList>("Geometry");
        auto points=polygon;
        for(auto& point:points) {point.x-=position.x;point.y-=position.y;point.z-=position.z;}
        if(points.empty()) {
            const auto half=options.triggerSize*0.5f;
            for(const auto& p:std::array<std::array<float,2>,4>{{{{-half,-half}},{{half,-half}},{{half,half}},{{-half,half}}}})
                points.push_back({p[0]*std::cos(angle)-p[1]*std::sin(angle),p[0]*std::sin(angle)+p[1]*std::cos(angle),0.0f});
        }
        for(const auto& point:points) {
            auto vertex=std::make_unique<neogff::GffStruct>();vertex->typeid_=encounter?std::uint32_t(geometry->count()):3;
            vertex->AddField(std::make_unique<neogff::GffFloatField>(encounter?"X":"PointX",point.x));
            vertex->AddField(std::make_unique<neogff::GffFloatField>(encounter?"Y":"PointY",point.y));
            vertex->AddField(std::make_unique<neogff::GffFloatField>(encounter?"Z":"PointZ",point.z));
            geometry->AddStruct(std::move(vertex));
        }
        setField(*instance,std::move(geometry));
        if(encounter) {
            auto spawns=std::make_unique<neogff::GffList>("SpawnPointList");auto spawn=std::make_unique<neogff::GffStruct>();spawn->typeid_=0;
            spawn->AddField(std::make_unique<neogff::GffFloatField>("X",position.x));spawn->AddField(std::make_unique<neogff::GffFloatField>("Y",position.y));
            spawn->AddField(std::make_unique<neogff::GffFloatField>("Z",position.z));spawn->AddField(std::make_unique<neogff::GffFloatField>("Orientation",-angle));
            spawns->AddStruct(std::move(spawn));setField(*instance,std::move(spawns));
        }
    }
    if(!root.GetFieldByLabel(kind.list))root.AddField(std::make_unique<neogff::GffList>(kind.list));
    auto& list=dynamic_cast<neogff::GffList&>(*root.GetFieldByLabel(kind.list));
    selectedPath_=std::string(kind.list)+"\\"+std::to_string(list.count());
    list.AddStruct(std::move(instance));resource.model->gff().dirty(true);
    stage(resource);fields_->refreshActiveDocument();rebuildObjects();selectObject(findPlacement(selectedPath_),true);
    recordEdit("Place object");requestPreview();
    setModuleStatusText("Placed "+wxui::toWx(blueprint.fileName)+" at "+wxui::toWx(decimal(position.x)+", "+decimal(position.y)+", "+decimal(position.z)));
}

void ModuleEditor::chooseAssets() {
    wxDialog dialog(this,wxID_ANY,"Module game resources");
    auto* root=new wxBoxSizer(wxVERTICAL);
    root->Add(new wxStaticText(&dialog,wxID_ANY,"Choose the installation supplying room models, textures and object templates."),0,wxALL,10);
    wxArrayString games;games.Add("Knights of the Old Republic (K1)");games.Add("The Sith Lords (K2)");
    auto* game=new wxChoice(&dialog,wxID_ANY,wxDefaultPosition,wxDefaultSize,games);
    game->SetSelection(game_==neomdl::model::GameVersion::Kotor1?0:1);
    root->Add(game,0,wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM,10);
    auto* row=new wxBoxSizer(wxHORIZONTAL);
    auto* directory=new wxTextCtrl(&dialog,wxID_ANY,install_?neosettings::pathToWx(install_->installPath):wxString{});
    directory->SetMinSize(FromDIP(wxSize(420,-1)));row->Add(directory,1,wxEXPAND);
    auto* browse=new wxButton(&dialog,wxID_ANY,"Browse...");row->Add(browse,0,wxLEFT,6);
    root->Add(row,0,wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM,10);
    root->Add(new wxStaticText(&dialog,wxID_ANY,"Module scope (matches the module name in the installation)"),0,wxLEFT|wxRIGHT|wxBOTTOM,10);
    auto* scope=new wxTextCtrl(&dialog,wxID_ANY,wxui::toWx(moduleName_));
    root->Add(scope,0,wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM,10);
    root->Add(dialog.CreateButtonSizer(wxOK|wxCANCEL),0,wxALL|wxALIGN_RIGHT,10);
    browse->Bind(wxEVT_BUTTON,[&](wxCommandEvent&){
        wxDirDialog picker(&dialog,"Choose game installation",directory->GetValue(),wxDD_DIR_MUST_EXIST);
        if(picker.ShowModal()==wxID_OK)directory->ChangeValue(picker.GetPath());
    });
    dialog.SetSizerAndFit(root);wxui::applyTheme(&dialog,dark_);
    if(dialog.ShowModal()!=wxID_OK)return;
    const auto id=game->GetSelection()==0?neoshared::game::GameId::Kotor1:neoshared::game::GameId::Kotor2;
    auto installation=neoshared::game::inspectGameInstallation(neosettings::pathFromWx(directory->GetValue()),id);
    if(!installation)throw std::runtime_error("Choose a K1 or K2 installation containing chitin.key and its game data.");
    if(blueprintEditor_)blueprintEditor_->clear();
    cameraControls_->stopPreview();
    install_=std::move(installation);session_.reset();moduleName_=neomdl::resourceKey(wxui::toStd(scope->GetValue()));
    palette_->clear();paletteSession_.reset();
    session_=openModuleAssets(*install_,moduleName_);palette_->setResources(session_,catalog_,localDirectory_);paletteSession_=session_;
    game_=id==neoshared::game::GameId::Kotor1?neomdl::model::GameVersion::Kotor1:neomdl::model::GameVersion::Kotor2;
    gameChoice_->SetSelection(game->GetSelection());
    refreshAreaControls();
    auto document=neomdl::ModelDocument::emptySceneDocument("module-editor:"+moduleName_,moduleName_,game_);
    canvas_->setDocument(document.get());document_=std::move(document);
    previewCurrent_=false;previewEnabled_=true;refreshCameraControls();requestPreview();
}

void ModuleEditor::startPreview() {
    std::map<Key,ResourceDocument> edits;
    for(const auto& [key,resource]:resources_)if(exportsResource(key))edits.emplace(key,resource.source);
    for(const auto& [key,source]:addedTemplates_)edits.emplace(key,source);
    const auto local=localDirectory_.empty()?neomdl::ResourceResolver{}:neomdl::directoryResources(localDirectory_);
    auto pending=canvas_->prepareAreaLoad();
    cancel_=std::make_shared<std::atomic<bool>>(false);loadingRevision_=revision_;requested_=false;
    canvas_->setLoading(true);setModuleStatusText("Loading the edited area...");
    loading_=std::async(std::launch::async,[scene=std::move(pending),edits=std::move(edits),
        archives=archiveResources_,host=hostResources_,local,session=session_,installation=install_,
        module=moduleName_,area=areaName(),game=game_,cancel=cancel_]() mutable {
        neoshared::bif::JobControl job;job.cancelled=[cancel]{return cancel->load();};
        neomdl::ResourceLoadScope work(job);
        if(!session&&installation) {
            session=openModuleAssets(*installation,module,job);
        }
        auto resolver=[edits=std::make_shared<const std::map<Key,ResourceDocument>>(std::move(edits)),archives,host,local,session](const std::string& name,std::uint16_t type)
                ->std::optional<ResourceDocument> {
            const auto key=Key{neomdl::resourceKey(name),type};
            if(const auto found=edits->find(key);found!=edits->end())return found->second;
            if(auto resource=archives(name,type))return resource;
            if(local)if(auto resource=local(name,type))return resource;
            if(session)return session->resolve(name,type);
            return host(name,type);
        };
        neomdl::AreaLoadOptions options;options.game=game;
        scene.loadModuleArea(area,"module-editor:"+module,resolver,options,{},job);
        job.check();return Preview{std::move(scene),std::move(session)};
    });
    updateActions();
}

void ModuleEditor::mapOwners() {
    ownerPaths_.clear();pathOwners_.clear();
    for(const auto& p:canvas_->areaScene().placements()) {
        if(!p.runtimeInstance)continue;
        const std::string list=p.kind==neomdl::AreaObjectKind::Creature?"Creature List":
            p.kind==neomdl::AreaObjectKind::Door?"Door List":"Placeable List";
        const auto path=list+"\\"+std::to_string(p.sourceIndex);
        auto& owners=pathOwners_[path];owners.insert(*p.runtimeInstance);
        for(const auto& object:canvas_->areaScene().objects())if(object->instanceIndex()==*p.runtimeInstance)
            for(std::size_t i=0;i<object->document()->references().size();++i)owners.insert(object->ownerId(i));
        for(const auto owner:owners)ownerPaths_[std::to_string(owner)]=path;
    }
    canvas_->setSelectedAreaOwners(pathOwners_[selectedPath_]);
}

void ModuleEditor::tick() {
    if(loading_.valid()&&loading_.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
        try {
            auto result=loading_.get();
            if(loadingRevision_==revision_) {
                session_=std::move(result.session);
                canvas_->installArea(std::move(result.scene),{},previewCurrent_);previewCurrent_=true;
                if(cameraControls_->previewing())previewCamera(cameraControls_->pendingCamera());
                applyAreaPreviewPhase();
                if(session_&&paletteSession_!=session_) {
                    palette_->setResources(session_,catalog_,localDirectory_);paletteSession_=session_;
                }
                previewError_.clear();
                mapOwners();setModuleStatusText("Live preview — edits update the area. Drag to orbit; right-drag to pan; scroll to zoom.");
            }
        } catch(const neoshared::bif::JobCancelled&) {
            setModuleStatusText("Area load cancelled.");
        } catch(const std::exception& e) {
            if(loadingRevision_==revision_) {
                canvas_->clearArea();previewCurrent_=false;ownerPaths_.clear();pathOwners_.clear();
                previewError_=std::string(e.what())+"\n";
                setModuleStatusText("Preview unavailable: "+wxui::toWx(e.what()));
            }
        }
        canvas_->setLoading(false);
        updateActions();
    }
    if(requested_&&!loading_.valid()&&Clock::now()>=due_) {
        try {startPreview();} catch(const std::exception& e) {requested_=false;wxui::showError(this,e);}
    }
    const auto now=Clock::now();const auto elapsed=std::chrono::duration<float>(now-lastFrame_).count();lastFrame_=now;
    if(previewCurrent_&&!loading_.valid()&&IsShownOnScreen())canvas_->advance(elapsed);
    if(cameraControls_->previewing()&&!loading_.valid()&&!requested_&&!canvas_->cameraScene().activeAreaCamera()) {cameraControls_->stopPreview();updateActions();}
}

bool ModuleEditor::saveResources() {
    wxDirDialog dialog(this,"Save module resources and imported templates to a working folder",{},wxDD_DEFAULT_STYLE);
    if(dialog.ShowModal()!=wxID_OK)return false;
    const auto directory=neosettings::pathFromWx(dialog.GetPath());
    std::vector<const ResourceDocument*> exports;
    for(const auto& [key,resource]:resources_)if(editableResource(key.second))exports.push_back(&resource.source);
    for(const auto& [key,source]:addedTemplates_)exports.push_back(&source);
    std::vector<std::filesystem::path> destinations;
    for(const auto* source:exports)destinations.push_back(directory/neoshared::pathFromUtf8(source->fileName));
    validateHostOutputs(destinations,inputs_);
    const bool overwrite=std::any_of(destinations.begin(),destinations.end(),[](const auto& path){return std::filesystem::exists(path);});
    if(overwrite&&wxMessageBox("Replace the existing module resources in this folder?","Save resources",wxYES_NO|wxICON_QUESTION,this)!=wxYES)return false;
    for(const auto* source:exports)writeResource(directory,*source);
    saved();setModuleStatusText("Saved module resources to "+neosettings::pathToWx(directory));return true;
}

bool ModuleEditor::saveArchive() {
    const auto ext=neoshared::pathToUtf8(archivePath_.extension());
    const auto name=neoshared::pathToUtf8(archivePath_.stem())+"_edited"+ext;
    wxFileDialog dialog(this,"Save edited module archive copy",{},wxui::toWx(name),
        "Module archive (*"+wxui::toWx(ext)+")|*"+wxui::toWx(ext),wxFD_SAVE|wxFD_OVERWRITE_PROMPT);
    if(dialog.ShowModal()!=wxID_OK)return false;
    const auto path=neosettings::pathFromWx(dialog.GetPath());
    validateHostOutput(path,inputs_);
    neoshared::erf::ErfArchive archive(archivePath_);
    for(auto& [key,resource]:resources_) {
        if(!editableResource(key.second))continue;
        writeResource(staging_,resource.source);
        archive.add_resource(staging_/neoshared::pathFromUtf8(resource.source.fileName),true);
    }
    for(const auto& [key,source]:addedTemplates_) {
        writeResource(staging_,source);
        archive.add_resource(staging_/neoshared::pathFromUtf8(source.fileName),true);
    }
    archive.save(path);saved();setModuleStatusText("Saved module archive copy: "+neosettings::pathToWx(path));return true;
}

bool ModuleEditor::packageModule() {
    std::vector<std::filesystem::path> archives;
    const auto addArchive=[&](const std::filesystem::path& path) {
        if(std::find(archives.begin(),archives.end(),path)==archives.end())archives.push_back(path);
    };
    if(!archivePath_.empty()) {
        addArchive(archivePath_);
        std::map<std::string,std::filesystem::path> siblings;
        const std::vector<std::string> candidates{moduleName_+".mod",moduleName_+"_s.rim",moduleName_+".rim",moduleName_+"_dlg.erf"};
        for(const auto& file:std::filesystem::directory_iterator(archivePath_.parent_path())) {
            auto name=neoshared::pathToUtf8(file.path().filename());
            for(auto& c:name)if(c>='A'&&c<='Z')c=char(c+('a'-'A'));
            if(std::find(candidates.begin(),candidates.end(),name)!=candidates.end()&&file.is_regular_file())siblings.emplace(name,file.path());
        }
        for(const auto& name:candidates)if(const auto found=siblings.find(name);found!=siblings.end())addArchive(found->second);
    }
    if(session_)for(const auto& path:session_->moduleScope()->archives)addArchive(path);
    const auto sources=chooseModulePackageSources(this,std::move(archives),localDirectory_,dark_);if(!sources)return false;
    const auto local=localDirectory_.empty()?neomdl::ResourceResolver{}:neomdl::directoryResources(localDirectory_);
    const auto resolve=[archives=archiveResources_,local,game=session_?session_->resolver():hostResources_](const std::string& name,std::uint16_t type)->std::optional<ResourceDocument> {
        if(auto source=archives(name,type))return source;
        if(local)if(auto source=local(name,type))return source;
        return game(name,type);
    };
    std::vector<ModulePackageInput> edits;
    for(const auto& [key,resource]:resources_)if(editableResource(key.second))edits.push_back({resource.source,resolve});
    for(const auto& [key,source]:addedTemplates_)edits.push_back({source,templateOrigins_.at(key)});
    std::vector<ModulePackageInput> inlineSources;for(const auto& [identity,source]:inlineSources_)inlineSources.push_back(source);
    ModulePackage package;
    {
        wxProgressDialog progress("Package module","Collecting module resources...",100,this,wxPD_APP_MODAL|wxPD_ELAPSED_TIME);
        package=collectModulePackage(*sources,localDirectory_,std::move(edits),inlineSources,resolve,game_,[&](const std::string& text){progress.Pulse(wxui::toWx(text));});
    }
    wxDialog review(this,wxID_ANY,"Module package contents",wxDefaultPosition,FromDIP(wxSize(760,520)),wxDEFAULT_DIALOG_STYLE|wxRESIZE_BORDER);
    auto* box=new wxBoxSizer(wxVERTICAL);box->Add(new wxTextCtrl(&review,wxID_ANY,wxui::toWx(package.report),wxDefaultPosition,wxDefaultSize,wxTE_MULTILINE|wxTE_READONLY),1,wxEXPAND|wxALL,8);
    box->Add(review.CreateButtonSizer(wxOK|wxCANCEL),0,wxALIGN_RIGHT|wxALL,8);review.SetSizer(box);wxui::applyTheme(&review,dark_);
    static_cast<wxButton*>(review.FindWindow(wxID_OK))->SetLabel("Save MOD...");if(review.ShowModal()!=wxID_OK)return false;
    wxFileDialog dialog(this,"Save combined module",{},wxui::toWx(moduleName_+".mod"),"Module archive (*.mod)|*.mod",wxFD_SAVE|wxFD_OVERWRITE_PROMPT);
    if(dialog.ShowModal()!=wxID_OK)return false;
    const auto path=neosettings::pathFromWx(dialog.GetPath());auto protectedInputs=inputs_;
    protectedInputs.insert(protectedInputs.end(),sources->archives.begin(),sources->archives.end());
    protectedInputs.insert(protectedInputs.end(),sources->extraFiles.begin(),sources->extraFiles.end());
    for(const auto& [key,source]:package.resources)protectedInputs.insert(protectedInputs.end(),source.protectedInputs.begin(),source.protectedInputs.end());
    validateHostOutput(path,protectedInputs);
    const auto directory=staging_/"package";std::filesystem::create_directory(directory);
    neoshared::erf::ErfArchive archive;archive.new_archive(path,neoshared::erf::ArchiveType::MOD);
    {
        wxProgressDialog progress("Export module","Writing module resources...",100,this,wxPD_APP_MODAL|wxPD_ELAPSED_TIME);
        for(auto& [key,source]:package.resources) {
            source.fileName=key.first+"."+neomdl::resourceExtension(key.second);progress.Pulse("Writing "+wxui::toWx(source.fileName));writeResource(directory,source);
            archive.add_resource(directory/neoshared::pathFromUtf8(source.fileName),true);
        }
        progress.Pulse("Saving MOD archive...");archive.save(path);
    }
    saved();setModuleStatusText("Packaged "+wxString::Format("%zu",package.resources.size())+" resources: "+neosettings::pathToWx(path));return true;
}

} // namespace

ModuleEditorPanel* createModuleEditorPanel(wxWindow* parent,neomodules::Context context) {
    return new ModuleEditor(parent,std::move(context));
}

} // namespace neotoolkit
