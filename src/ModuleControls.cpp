#include "ModuleControls.hpp"
#include "NeoWxUi.hpp"
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/combobox.h>
#include <wx/listbox.h>
#include <wx/scrolwin.h>
#include <wx/simplebook.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/wrapsizer.h>
#include <algorithm>
#include <map>

namespace neotoolkit {

ModuleControls::ModuleControls(wxWindow* parent,Edit edit,
    std::function<void(AreaAction,std::size_t,const std::string&)> areaEdit)
    :wxPanel(parent),edit_(std::move(edit)) {
    auto* box=new wxBoxSizer(wxVERTICAL);auto* category=new wxChoice(this,wxID_ANY);
    auto* pages=new wxSimplebook(this);wxScrolledWindow* page[4];wxBoxSizer* content[4];
    const char* names[]={"Properties","Event scripts","Clock","Area list"};
    const char* notes[]={
        "Module identity and entry area. Move or rotate the entry marker in Objects to change the spawn transform.",
        "Compiled script resource names, without an extension. Packaging collects referenced scripts and dialogues.",
        "Dawn, dusk and minutes per hour control the world clock (0 minutes uses the engine's 5-minute default). Start date/time fields are read from saved games; normal transitions carry the current world time.",
        "Area references in module.ifo. The engine initially loads the first entry. Removing a reference keeps its resource files."};
    for(int i=0;i<4;++i) {
        category->Append(names[i]);page[i]=new wxScrolledWindow(pages);page[i]->SetScrollRate(0,FromDIP(12));
        content[i]=new wxBoxSizer(wxVERTICAL);page[i]->SetSizer(content[i]);pages->AddPage(page[i],names[i]);
        auto* note=new wxStaticText(page[i],wxID_ANY,notes[i]);note->Wrap(FromDIP(195));content[i]->Add(note,0,wxEXPAND|wxALL,5);
    }
    category->SetSelection(0);category->Bind(wxEVT_CHOICE,[pages,category](wxCommandEvent&){pages->SetSelection(category->GetSelection());});
    box->Add(category,0,wxEXPAND|wxALL,5);box->Add(pages,1,wxEXPAND);
    for(const auto& item:std::vector<std::pair<const char*,const char*>>{{"Mod_Name","Name"},{"Mod_Description","Description"}}) {
        const auto index=localized_.size();LocalizedRow row;row.field=item.first;row.label=item.second;
        content[0]->Add(new wxStaticText(page[0],wxID_ANY,wxui::toWx(row.label+" TLK reference (-1 = embedded)")),0,wxALL,5);
        row.reference=new wxTextCtrl(page[0],wxID_ANY,{},wxDefaultPosition,wxDefaultSize,wxTE_PROCESS_ENTER);
        content[0]->Add(row.reference,0,wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM,5);
        row.language=new wxChoice(page[0],wxID_ANY);content[0]->Add(row.language,0,wxEXPAND|wxLEFT|wxRIGHT,5);
        row.text=new wxTextCtrl(page[0],wxID_ANY,{},wxDefaultPosition,FromDIP(wxSize(-1,item.first==std::string("Mod_Name")?55:100)),wxTE_MULTILINE);
        content[0]->Add(row.text,0,wxEXPAND|wxALL,5);localized_.push_back(row);
        const auto reference=[this,index] {
            auto& r=localized_[index];r.reference->DiscardEdits();
            try {edit_(r.field+"(strref)","CExoLocString",wxui::toStd(r.reference->GetValue()),r.label+" TLK reference");}
            catch(const std::exception& e){wxui::showError(this,e);}
        };
        row.reference->Bind(wxEVT_TEXT_ENTER,[reference](wxCommandEvent&){reference();});
        row.reference->Bind(wxEVT_KILL_FOCUS,[reference,control=row.reference](wxFocusEvent& event){if(control->IsModified())reference();event.Skip();});
        row.language->Bind(wxEVT_CHOICE,[this,index](wxCommandEvent&){showLocalized(index);});
        row.text->Bind(wxEVT_KILL_FOCUS,[this,index](wxFocusEvent& event){
            auto& r=localized_[index];
            if(r.text->IsModified()) {
                r.text->DiscardEdits();
                try {edit_(r.field+"(lang"+std::to_string(r.ids.at(r.language->GetSelection()))+")","CExoLocString",wxui::toStd(r.text->GetValue()),r.label+" text");}
                catch(const std::exception& e){wxui::showError(this,e);}
            }
            event.Skip();
        });
    }
    struct Control {int page;const char* field;const char* type;const char* label;};
    const Control controls[]={
        {0,"Mod_Tag","CExoString","Tag"},{0,"Mod_Version","DWORD","Module version"},
        {0,"Mod_XPScale","BYTE","XP scale"},{0,"Mod_StartMovie","ResRef","Start movie"},
        {0,"Mod_Entry_Area","ResRef","Entry area"},
        {1,"Mod_OnModLoad","ResRef","Module load"},{1,"Mod_OnModStart","ResRef","Module start"},
        {1,"Mod_OnClientEntr","ResRef","Client enter"},{1,"Mod_OnClientLeav","ResRef","Client leave"},
        {1,"Mod_OnHeartbeat","ResRef","Heartbeat"},{1,"Mod_OnUsrDefined","ResRef","User defined"},
        {1,"Mod_OnActvtItem","ResRef","Activate item"},{1,"Mod_OnAcquirItem","ResRef","Acquire item"},
        {1,"Mod_OnUnAqreItem","ResRef","Unacquire item"},{1,"Mod_OnEquipItem","ResRef","Equip item"},
        {1,"Mod_OnPlrDeath","ResRef","Player death"},{1,"Mod_OnPlrDying","ResRef","Player dying"},
        {1,"Mod_OnSpawnBtnDn","ResRef","Respawn button"},{1,"Mod_OnPlrRest","ResRef","Player rest"},
        {1,"Mod_OnPlrLvlUp","ResRef","Player level up"},
        {2,"Mod_MinPerHour","BYTE","Real minutes per game hour"},{2,"Mod_DawnHour","BYTE","Dawn hour"},
        {2,"Mod_DuskHour","BYTE","Dusk hour"},{2,"Mod_StartYear","DWORD","Saved year"},
        {2,"Mod_StartMonth","BYTE","Saved month"},{2,"Mod_StartDay","BYTE","Saved day"},
        {2,"Mod_StartHour","BYTE","Saved hour"},{2,"Mod_StartMinute","WORD","Saved minute"},
        {2,"Mod_StartSecond","WORD","Saved second"},{2,"Mod_StartMiliSec","WORD","Saved millisecond"}
    };
    for(const auto& c:controls) {
        auto* label=new wxStaticText(page[c.page],wxID_ANY,c.label);label->Wrap(FromDIP(195));content[c.page]->Add(label,0,wxLEFT|wxRIGHT|wxTOP,5);
        auto* text=new wxTextCtrl(page[c.page],wxID_ANY,{},wxDefaultPosition,wxDefaultSize,wxTE_PROCESS_ENTER);
        content[c.page]->Add(text,0,wxEXPAND|wxALL,5);rows_.push_back({c.field,c.type,c.label,text});const auto index=rows_.size()-1;
        text->Bind(wxEVT_TEXT_ENTER,[this,index](wxCommandEvent&){apply(rows_[index]);});
        text->Bind(wxEVT_KILL_FOCUS,[this,index](wxFocusEvent& event){if(rows_[index].text->IsModified())apply(rows_[index]);event.Skip();});
    }
    areas_=new wxListBox(page[3],wxID_ANY);areas_->SetMinSize(FromDIP(wxSize(-1,160)));content[3]->Add(areas_,1,wxEXPAND|wxALL,5);
    areaName_=new wxComboBox(page[3],wxID_ANY);content[3]->Add(areaName_,0,wxEXPAND|wxALL,5);
    const char* actions[]={"Add","Rename","Remove","Move up","Move down"};
    auto* buttons=new wxWrapSizer(wxHORIZONTAL);
    for(int i=0;i<5;++i) {
        auto* button=new wxButton(page[3],wxID_ANY,actions[i]);areaButtons_.push_back(button);buttons->Add(button,0,wxALL,3);
        button->Bind(wxEVT_BUTTON,[this,i,areaEdit](wxCommandEvent&){
            try {areaEdit(static_cast<AreaAction>(i),std::size_t(areas_->GetSelection()),wxui::toStd(areaName_->GetValue()));}
            catch(const std::exception& e){wxui::showError(this,e);}
        });
    }
    auto* entry=new wxButton(page[3],wxID_ANY,"Set as entry area");areaButtons_.push_back(entry);buttons->Add(entry,0,wxALL,3);
    entry->Bind(wxEVT_BUTTON,[this](wxCommandEvent&){
        try {edit_("Mod_Entry_Area","ResRef",wxui::toStd(areas_->GetStringSelection()),"entry area");}
        catch(const std::exception& e){wxui::showError(this,e);}
    });
    content[3]->Add(buttons,0,wxEXPAND|wxALL,2);
    areas_->Bind(wxEVT_LISTBOX,[this](wxCommandEvent&){areaName_->ChangeValue(areas_->GetStringSelection());updateAreaActions();});
    areaName_->Bind(wxEVT_TEXT,[this](wxCommandEvent&){updateAreaActions();});
    auto* hint=new wxStaticText(this,wxID_ANY,"Values apply on Enter or leaving the field. Script/resource names use the engine's 16-byte references.");
    hint->Wrap(FromDIP(195));box->Add(hint,0,wxEXPAND|wxALL,5);SetSizer(box);load(nullptr,{});
}

void ModuleControls::load(const neogff::GffStruct* root,const std::vector<std::string>& availableAreas) {
    root_=root;
    for(auto& row:rows_) {
        const auto* field=root?root->GetFieldByLabel(row.field):nullptr;
        row.text->Enable(root!=nullptr);row.text->ChangeValue(field?wxui::toWx(field->GetString()):wxString{});row.text->DiscardEdits();
    }
    const std::map<int,std::string> languages={{0,"English"},{1,"French"},{2,"German"},{3,"Italian"},{4,"Spanish"},{5,"Polish"},
        {128,"Korean"},{129,"Chinese (traditional)"},{130,"Chinese (simplified)"},{131,"Japanese"}};
    for(std::size_t i=0;i<localized_.size();++i) {
        auto& row=localized_[i];const auto selected=row.language->GetSelection();
        const auto id=selected==wxNOT_FOUND?0:row.ids.at(selected);
        const auto* field=root?static_cast<const neogff::GffLocalizedStringField*>(root->GetFieldByLabel(row.field)):nullptr;
        std::map<int,std::string> choices;
        for(const auto& [language,name]:languages)for(int gender=0;gender<2;++gender)choices[language*2+gender]=name+(gender?" / female":" / male");
        if(field)for(const auto& text:field->substrings)choices.emplace(text.stringid,"String ID "+std::to_string(text.stringid));
        choices.emplace(id,"String ID "+std::to_string(id));
        row.language->Clear();row.ids.clear();
        for(const auto& [key,name]:choices) {row.ids.push_back(key);row.language->Append(wxui::toWx(name));}
        row.language->SetSelection(int(std::find(row.ids.begin(),row.ids.end(),id)-row.ids.begin()));
        row.language->Enable(root!=nullptr);row.reference->Enable(root!=nullptr);row.text->Enable(root!=nullptr);
        row.reference->ChangeValue(field&&field->strref!=0xffffffffu?wxui::toWx(std::to_string(field->strref)):wxString("-1"));row.reference->DiscardEdits();showLocalized(i);
    }
    const auto selected=areas_->GetStringSelection();areas_->Clear();
    if(root)if(const auto* list=static_cast<const neogff::GffList*>(root->GetFieldByLabel("Mod_Area_list")))
        for(const auto& area:list->allStructs())areas_->Append(wxui::toWx(area->GetFieldByLabel("Area_Name")->GetString()));
    areas_->SetStringSelection(selected);areas_->Enable(root!=nullptr);
    const auto name=areaName_->GetValue();areaName_->Clear();for(const auto& area:availableAreas)areaName_->Append(wxui::toWx(area));areaName_->ChangeValue(name);
    areaName_->Enable(root!=nullptr);updateAreaActions();
}
void ModuleControls::apply(const Row& row) {
    row.text->DiscardEdits();
    try {edit_(row.field,row.type,wxui::toStd(row.text->GetValue()),row.label);}
    catch(const std::exception& e){wxui::showError(this,e);}
}
void ModuleControls::showLocalized(std::size_t index) {
    auto& row=localized_[index];const auto* field=root_?static_cast<const neogff::GffLocalizedStringField*>(root_->GetFieldByLabel(row.field)):nullptr;
    row.text->ChangeValue(field?wxui::toWx(field->GetStringById(row.ids.at(row.language->GetSelection()))):wxString{});row.text->DiscardEdits();
}
void ModuleControls::updateAreaActions() {
    const auto selected=areas_->GetSelection();const bool selection=root_&&selected!=wxNOT_FOUND;
    areaButtons_[0]->Enable(root_&&!areaName_->GetValue().empty());
    areaButtons_[1]->Enable(selection&&!areaName_->GetValue().empty());areaButtons_[2]->Enable(selection);
    areaButtons_[3]->Enable(selection&&selected>0);areaButtons_[4]->Enable(selection&&selected+1<int(areas_->GetCount()));areaButtons_[5]->Enable(selection);
}

}
