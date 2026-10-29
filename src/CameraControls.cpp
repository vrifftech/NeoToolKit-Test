#include "CameraControls.hpp"
#include "NeoWxUi.hpp"
#include <gff/Common.hpp>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/sizer.h>
#include <wx/slider.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <cmath>

namespace neotoolkit {
namespace {
template<class T> const T* field(const neogff::GffStruct& root,const char* label) {return dynamic_cast<const T*>(root.GetFieldByLabel(label));}
float number(const neogff::GffStruct& root,const char* label,float initial=0) {const auto* value=field<neogff::GffFloatField>(root,label);return value?value->value:initial;}
std::string decimal(float value) {return wxString::Format("%.9g",double(value)).ToStdString();}
}

CameraControls::CameraControls(wxWindow* parent,Actions actions):wxScrolledWindow(parent),actions_(std::move(actions)) {
    SetScrollRate(0,FromDIP(12));auto* box=new wxBoxSizer(wxVERTICAL);
    box->Add(new wxStaticText(this,wxID_ANY,"Viewport direction"),0,wxALL,5);
    auto* direction=new wxChoice(this,wxID_ANY);
    for(const auto* name:{"Perspective","Top (+Z)","Bottom (-Z)","Front (-Y)","Back (+Y)","Left (-X)","Right (+X)"})direction->Append(name);
    direction->SetSelection(wxNOT_FOUND);box->Add(direction,0,wxEXPAND|wxALL,5);
    direction->Bind(wxEVT_CHOICE,[this,direction](wxCommandEvent&){
        constexpr float angles[][2]={{35,25},{0,90},{0,-90},{0,0},{180,0},{90,0},{-90,0}};
        const auto index=direction->GetSelection();stopPreview();actions_.direction(angles[index][0],angles[index][1]);direction->SetSelection(wxNOT_FOUND);
    });
    box->Add(new wxStaticText(this,wxID_ANY,"Placed GIT camera"),0,wxALL,5);camera_=new wxChoice(this,wxID_ANY);box->Add(camera_,0,wxEXPAND|wxALL,5);
    through_=new wxCheckBox(this,wxID_ANY,"View through selected camera");box->Add(through_,0,wxALL,5);
    box->Add(new wxStaticText(this,wxID_ANY,"Preview FOV (vertical degrees)"),0,wxALL,5);
    fov_=new wxSlider(this,wxID_ANY,55,1,179);box->Add(fov_,0,wxEXPAND|wxALL,5);
    const char* labels[]={"Camera ID","Position (x | y | z)","Orientation quaternion (w | x | y | z)","Pitch offset (degrees)","Height offset","Vertical FOV (<=0: orthographic)","Microphone range"};
    for(std::size_t i=0;i<fields_.size();++i) {
        box->Add(new wxStaticText(this,wxID_ANY,labels[i]),0,wxLEFT|wxRIGHT|wxTOP,5);
        fields_[i]=new wxTextCtrl(this,wxID_ANY,{},wxDefaultPosition,wxDefaultSize,wxTE_PROCESS_ENTER);box->Add(fields_[i],0,wxEXPAND|wxALL,5);
        fields_[i]->Bind(wxEVT_TEXT_ENTER,[this](wxCommandEvent&){try {preview();}catch(const std::exception& e){wxui::showError(this,e);}});
    }
    apply_=new wxButton(this,wxID_ANY,"Apply camera to GIT");box->Add(apply_,0,wxEXPAND|wxALL,5);
    auto* note=new wxStaticText(this,wxID_ANY,"Preview changes stay in this panel until Apply. Quaternion orientation is kept as authored; the engine adds Pitch and Height. FOV uses the viewport aspect. Clipping belongs to the preview/runtime, not GIT. Apply participates in module undo and export.");note->Wrap(FromDIP(240));box->Add(note,0,wxEXPAND|wxALL,6);
    camera_->Bind(wxEVT_CHOICE,[this](wxCommandEvent&){showCamera();actions_.select(cameras_.at(camera_->GetSelection()).sourceIndex);});
    through_->Bind(wxEVT_CHECKBOX,[this](wxCommandEvent&){try {preview();}catch(const std::exception& e){stopPreview();wxui::showError(this,e);}});
    fov_->Bind(wxEVT_SLIDER,[this](wxCommandEvent&){fields_[5]->ChangeValue(wxString::Format("%d",fov_->GetValue()));try {preview();}catch(const std::exception& e){wxui::showError(this,e);}});
    apply_->Bind(wxEVT_BUTTON,[this](wxCommandEvent&){try {actions_.edit(pendingCamera());}catch(const std::exception& e){wxui::showError(this,e);}});
    SetSizer(box);load(nullptr,true);
}

void CameraControls::load(const neogff::GffStruct* root,bool k2) {
    const auto selected=camera_->GetSelection();cameras_.clear();camera_->Clear();
    if(root)if(const auto* list=field<neogff::GffList>(*root,"CameraList"))for(std::size_t i=0;i<list->count();++i) {
        const auto& entry=*list->GetStruct(i);neomdl::AreaCameraRecord camera;camera.sourceIndex=i;
        if(const auto* id=field<neogff::GffIntField>(entry,"CameraID"))camera.cameraId=id->value;
        if(const auto* position=field<neogff::GffPositionField>(entry,"Position"))camera.position={position->value[0],position->value[1],position->value[2]};
        if(const auto* orientation=field<neogff::GffOrientationField>(entry,"Orientation"))camera.orientation={orientation->value[1],orientation->value[2],orientation->value[3],orientation->value[0]};
        camera.pitch=number(entry,"Pitch");camera.height=number(entry,"Height");camera.fieldOfView=number(entry,"FieldOfView",55);camera.microphoneRange=number(entry,"MicRange");
        camera_->Append(wxString::Format("[%zu] Camera ID %d",i,camera.cameraId));cameras_.push_back(camera);
    }
    camera_->SetToolTip(k2?"K2 registry: 70 cameras; shots select the first matching Camera ID.":"K1 registry: 50 cameras; shots select the first matching Camera ID.");
    camera_->SetSelection(selected>=0&&std::size_t(selected)<cameras_.size()?selected:cameras_.empty()?wxNOT_FOUND:0);
    if(cameras_.empty())stopPreview();
    showCamera();FitInside();
}
void CameraControls::showCamera() {
    const bool selected=camera_->GetSelection()!=wxNOT_FOUND;camera_->Enable(!cameras_.empty());apply_->Enable(selected);fov_->Enable(selected);through_->Enable(selected&&previewEnabled_);for(auto* field:fields_)field->Enable(selected);
    if(!selected) {for(auto* field:fields_)field->ChangeValue({});return;}
    const auto& camera=cameras_.at(camera_->GetSelection());const auto& p=camera.position;const auto& q=camera.orientation;
    const std::string values[]={std::to_string(camera.cameraId),decimal(p.x)+"|"+decimal(p.y)+"|"+decimal(p.z),decimal(q.w)+"|"+decimal(q.x)+"|"+decimal(q.y)+"|"+decimal(q.z),decimal(camera.pitch),decimal(camera.height),decimal(camera.fieldOfView),decimal(camera.microphoneRange)};
    for(std::size_t i=0;i<fields_.size();++i)fields_[i]->ChangeValue(wxui::toWx(values[i]));
    fov_->SetValue(int(std::lround(camera.fieldOfView)));
}
neomdl::AreaCameraRecord CameraControls::pendingCamera() const {
    auto camera=cameras_.at(camera_->GetSelection());const auto text=[this](std::size_t i){return wxui::toStd(fields_[i]->GetValue());};
    camera.cameraId=neogff::ParseInt32Decimal(text(0));const auto p=neogff::ParseGffVector3Text(text(1));const auto q=neogff::ParseGffVector4Text(text(2));
    camera.position={p[0],p[1],p[2]};camera.orientation={q[1],q[2],q[3],q[0]};camera.pitch=neogff::ParseFloatDecimal(text(3));camera.height=neogff::ParseFloatDecimal(text(4));camera.fieldOfView=neogff::ParseFloatDecimal(text(5));camera.microphoneRange=neogff::ParseFloatDecimal(text(6));return camera;
}
void CameraControls::select(std::size_t sourceIndex) {camera_->SetSelection(int(sourceIndex));showCamera();}
bool CameraControls::previewing() const {return through_->GetValue();}
void CameraControls::preview() {actions_.preview(previewing()?std::optional<neomdl::AreaCameraRecord>{pendingCamera()}:std::nullopt);}
void CameraControls::stopPreview() {through_->SetValue(false);}
void CameraControls::enablePreview(bool enabled) {previewEnabled_=enabled;through_->Enable(enabled&&camera_->GetSelection()!=wxNOT_FOUND);}

}
