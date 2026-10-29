#include "AreaControls.hpp"
#include "NeoWxUi.hpp"
#include <gff/Common.hpp>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/clrpicker.h>
#include <wx/scrolwin.h>
#include <wx/simplebook.h>
#include <wx/sizer.h>
#include <wx/slider.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <algorithm>
#include <cmath>

namespace neotoolkit {

AreaControls::AreaControls(wxWindow* parent,
    std::function<void(const std::string&,const std::string&,const std::string&)> edit,
    std::function<void()> preview):wxPanel(parent),edit_(std::move(edit)) {
    auto* box=new wxBoxSizer(wxVERTICAL);
    box->Add(new wxStaticText(this,wxID_ANY,"Preview lighting (not saved)"),0,wxALL,5);
    phase_=new wxChoice(this,wxID_ANY);
    for(const auto* name:{"Authored time / module clock","Day","Night"})phase_->Append(name);
    phase_->SetSelection(0);box->Add(phase_,0,wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM,5);
    phase_->Bind(wxEVT_CHOICE,[preview=std::move(preview)](wxCommandEvent&){preview();});
    auto* category=new wxChoice(this,wxID_ANY);
    auto* pages=new wxSimplebook(this);wxScrolledWindow* page[4];wxBoxSizer* content[4];
    const char* names[]={"Lighting","Fog","Grass","Environment"};
    const char* notes[]={
        "Scene ambient and sun/moon colors are separate. Sun/moon lighting uses the area's sun model.",
        "Enable fog applies to day and night. Night uses its own color and distances.",
        "Grass appears on eligible room surfaces. Set a texture and positive size before increasing density.",
        "Day/night cycles use the module's IFO clock. Weather chances affect in-game scheduling."};
    for(int i=0;i<4;++i) {
        category->Append(names[i]);page[i]=new wxScrolledWindow(pages);page[i]->SetScrollRate(0,FromDIP(12));
        content[i]=new wxBoxSizer(wxVERTICAL);page[i]->SetSizer(content[i]);pages->AddPage(page[i],names[i]);
        auto* note=new wxStaticText(page[i],wxID_ANY,notes[i]);note->Wrap(FromDIP(195));
        content[i]->Add(note,0,wxEXPAND|wxALL,5);
    }
    category->SetSelection(0);category->Bind(wxEVT_CHOICE,[pages,category](wxCommandEvent&){pages->SetSelection(category->GetSelection());});
    box->Add(category,0,wxEXPAND|wxALL,5);box->Add(pages,1,wxEXPAND);
    struct Control {int page;Kind kind;const char* label;const char* field;float maximum{},step{1};};
    const Control controls[]={
        {0,Kind::Color,"Scene ambient","DynAmbientColor"},
        {0,Kind::Color,"Day ambient","SunAmbientColor"},{0,Kind::Color,"Day diffuse","SunDiffuseColor"},
        {0,Kind::Color,"Night ambient","MoonAmbientColor"},{0,Kind::Color,"Night diffuse","MoonDiffuseColor"},
        {0,Kind::Number,"Shadow opacity (%)","ShadowOpacity",255,1},
        {1,Kind::Flag,"Enable fog","SunFogOn"},
        {1,Kind::Color,"Day color","SunFogColor"},
        {1,Kind::Number,"Day near distance","SunFogNear",500,0.1f},{1,Kind::Number,"Day far distance","SunFogFar",500,0.1f},
        {1,Kind::Color,"Night color","MoonFogColor"},
        {1,Kind::Number,"Night near distance","MoonFogNear",500,0.1f},{1,Kind::Number,"Night far distance","MoonFogFar",500,0.1f},
        {2,Kind::Text,"Texture reference","Grass_TexName"},
        {2,Kind::Number,"Density (blades / unit^2)","Grass_Density",20,0.1f},
        {2,Kind::Number,"Blade size","Grass_QuadSize",5,0.05f},
        {2,Kind::Color,"Ambient","Grass_Ambient"},{2,Kind::Color,"Diffuse","Grass_Diffuse"},
        {2,Kind::Color,"Emissive (K2)","Grass_Emissive"},
        {2,Kind::Number,"Atlas weight: lower left","Grass_Prob_LL",1,0.01f},
        {2,Kind::Number,"Atlas weight: lower right","Grass_Prob_LR",1,0.01f},
        {2,Kind::Number,"Atlas weight: upper left","Grass_Prob_UL",1,0.01f},
        {2,Kind::Number,"Atlas weight: upper right","Grass_Prob_UR",1,0.01f},
        {3,Kind::Flag,"Day/night cycle","DayNightCycle"},{3,Kind::Flag,"Static time: night","IsNight"},
        {3,Kind::WeatherOff,"Disable weather and wind","Flags"},{3,Kind::Wind,"Wind strength","WindPower"},
        {3,Kind::Number,"Rain chance (%)","ChanceRain",100,1},
        {3,Kind::Number,"Snow chance (%)","ChanceSnow",100,1},
        {3,Kind::Number,"Lightning chance (%)","ChanceLightning",100,1}
    };
    for(const auto& c:controls) {
        const auto index=rows_.size();rows_.push_back({c.kind,c.field,c.label,c.step,c.maximum});
        auto& row=rows_.back();auto* parentPage=page[c.page];auto* layout=content[c.page];
        if(c.kind==Kind::Flag||c.kind==Kind::WeatherOff) {
            auto* control=new wxCheckBox(parentPage,wxID_ANY,c.label);row.control=control;layout->Add(control,0,wxEXPAND|wxALL,5);
            control->Bind(wxEVT_CHECKBOX,[this,index,control](wxCommandEvent&){
                const auto& r=rows_[index];std::uint32_t value=control->GetValue()?1u:0u;
                if(r.kind==Kind::WeatherOff) {
                    const auto original=static_cast<const neogff::GffUInt32Field&>(*root_->GetFieldByLabel(r.field)).value;
                    value=(original&~1u)|value;
                }
                apply(index,std::to_string(value));
            });
            continue;
        }
        auto* label=new wxStaticText(parentPage,wxID_ANY,c.label);label->Wrap(FromDIP(195));layout->Add(label,0,wxLEFT|wxRIGHT|wxTOP,5);
        if(c.kind==Kind::Color) {
            auto* control=new wxColourPickerCtrl(parentPage,wxID_ANY,*wxBLACK,wxDefaultPosition,wxDefaultSize,wxCLRP_USE_TEXTCTRL);
            row.control=control;layout->Add(control,0,wxEXPAND|wxALL,5);
            control->Bind(wxEVT_COLOURPICKER_CHANGED,[this,index](wxColourPickerEvent& event){
                const auto& r=rows_[index];const auto color=event.GetColour();
                const auto original=static_cast<const neogff::GffUInt32Field&>(*root_->GetFieldByLabel(r.field)).value;
                const auto value=(original&0xff000000u)|std::uint32_t(color.Red())|(std::uint32_t(color.Green())<<8u)|(std::uint32_t(color.Blue())<<16u);
                apply(index,std::to_string(value));
            });
        } else if(c.kind==Kind::Wind) {
            auto* control=new wxChoice(parentPage,wxID_ANY);for(const auto* value:{"None","Low","High"})control->Append(value);
            row.control=control;layout->Add(control,0,wxEXPAND|wxALL,5);
            control->Bind(wxEVT_CHOICE,[this,index,control](wxCommandEvent&){apply(index,std::to_string(control->GetSelection()));});
        } else {
            auto* line=new wxBoxSizer(wxHORIZONTAL);
            if(c.kind==Kind::Number) {
                row.slider=new wxSlider(parentPage,wxID_ANY,0,0,int(std::lround(c.maximum/c.step)));
                row.slider->SetMinSize(FromDIP(wxSize(55,-1)));line->Add(row.slider,1,wxALIGN_CENTER_VERTICAL|wxRIGHT,4);
                row.slider->Bind(wxEVT_SCROLL_THUMBTRACK,[this,index](wxScrollEvent&){sliderChanged(index,false);});
                const auto commit=[this,index](wxScrollEvent&){sliderChanged(index,true);};
                row.slider->Bind(wxEVT_SCROLL_THUMBRELEASE,commit);row.slider->Bind(wxEVT_SCROLL_CHANGED,commit);
                row.slider->Bind(wxEVT_SCROLL_LINEUP,commit);row.slider->Bind(wxEVT_SCROLL_LINEDOWN,commit);
                row.slider->Bind(wxEVT_SCROLL_PAGEUP,commit);row.slider->Bind(wxEVT_SCROLL_PAGEDOWN,commit);
                row.slider->Bind(wxEVT_SCROLL_TOP,commit);row.slider->Bind(wxEVT_SCROLL_BOTTOM,commit);
            }
            row.text=new wxTextCtrl(parentPage,wxID_ANY,{},wxDefaultPosition,wxDefaultSize,wxTE_PROCESS_ENTER);row.control=row.text;
            row.text->SetMinSize(FromDIP(wxSize(65,-1)));line->Add(row.text,c.kind==Kind::Text?1:0,wxEXPAND);
            row.text->Bind(wxEVT_TEXT_ENTER,[this,index](wxCommandEvent&){apply(index,wxui::toStd(rows_[index].text->GetValue()));});
            row.text->Bind(wxEVT_KILL_FOCUS,[this,index](wxFocusEvent& event){
                if(rows_[index].text->IsModified())apply(index,wxui::toStd(rows_[index].text->GetValue()));
                event.Skip();
            });
            layout->Add(line,0,wxEXPAND|wxALL,5);
        }
    }
    auto* hint=new wxStaticText(this,wxID_ANY,"Only fields present in this ARE are enabled. Typed values apply on Enter or when leaving the field.");
    hint->Wrap(FromDIP(195));box->Add(hint,0,wxEXPAND|wxALL,5);SetSizer(box);load(nullptr,true);enablePreview(false);
}

void AreaControls::load(const neogff::GffStruct* root,bool k2) {
    root_=root;
    for(auto& row:rows_) {
        const auto* field=root?root->GetFieldByLabel(row.field):nullptr;
        const bool present=field&&(row.field!="Grass_Emissive"||k2);
        row.control->Enable(present);if(row.slider)row.slider->Enable(present);
        if(!present) {if(row.text) {row.text->ChangeValue({});row.text->DiscardEdits();}continue;}
        const auto value=field->GetString();
        switch(row.kind) {
        case Kind::Color: {
            const auto color=static_cast<const neogff::GffUInt32Field&>(*field).value;
            static_cast<wxColourPickerCtrl*>(row.control)->SetColour(wxColour(color&255u,(color>>8u)&255u,(color>>16u)&255u));break;
        }
        case Kind::Flag:static_cast<wxCheckBox*>(row.control)->SetValue(std::stoul(value)!=0);break;
        case Kind::WeatherOff:static_cast<wxCheckBox*>(row.control)->SetValue((std::stoul(value)&1u)!=0);break;
        case Kind::Wind:static_cast<wxChoice*>(row.control)->SetSelection(std::stoi(value));break;
        case Kind::Number: {
            const auto scalar=neogff::ParseFloatDecimal(value);
            row.slider->SetRange(int(std::floor(std::min(0.0f,scalar)/row.step)),int(std::ceil(std::max(row.maximum,scalar)/row.step)));
            row.slider->SetValue(int(std::lround(scalar/row.step)));
            row.text->ChangeValue(wxui::toWx(value));row.text->DiscardEdits();break;
        }
        case Kind::Text:row.text->ChangeValue(wxui::toWx(value));row.text->DiscardEdits();break;
        }
    }
}
void AreaControls::sliderChanged(std::size_t index,bool commit) {
    auto& row=rows_[index];const auto value=neogff::FormatNumber(row.slider->GetValue()*row.step);
    row.text->ChangeValue(wxui::toWx(value));if(commit)apply(index,value);
}
void AreaControls::apply(std::size_t index,std::string value) {
    try {
        const auto& row=rows_[index];if(row.text)row.text->DiscardEdits();
        const auto& field=*root_->GetFieldByLabel(row.field);
        if(field.fieldtype==neogff::FIELD_TYPE_FLOAT&&neogff::ParseFloatDecimal(value)==static_cast<const neogff::GffFloatField&>(field).value)return;
        if(value==field.GetString())return;
        edit_(row.field,value,row.label);
    } catch(const std::exception& e) {wxui::showError(this,e);}
}
void AreaControls::enablePreview(bool enabled) {phase_->Enable(enabled);}
int AreaControls::previewPhase() const {return phase_->GetSelection();}

}
