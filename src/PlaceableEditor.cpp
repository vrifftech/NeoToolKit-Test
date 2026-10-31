#include "PlaceableEditor.hpp"
#include "wx/ModelCanvas.hpp"
#include <core/ObjectVisualState.hpp>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/clrpicker.h>
#include <wx/notebook.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/splitter.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <algorithm>
#include <cmath>

namespace neotoolkit {
namespace {
template<class T> const T* field(const neogff::GffStruct& root,const std::string& name) {
    return dynamic_cast<const T*>(root.GetFieldByLabel(name));
}
template<class T> auto number(const neogff::GffStruct& root,const std::string& name,decltype(T{}.value) initial=0) {
    const auto* value=field<T>(root,name);return value?value->value:initial;
}
int englishId(const neogff::GffLocalizedStringField* value) {
    int id=0;if(value)for(const auto& text:value->substrings)if(text.stringid==0||text.stringid==1)id=text.stringid;return id;
}
}

ObjectEditor::ObjectEditor(wxWindow* parent,std::uint16_t type,Edit edit):wxPanel(parent),edit_(std::move(edit)),type_(type) {
    auto* box=new wxBoxSizer(wxVERTICAL);auto* split=new wxSplitterWindow(this,wxID_ANY,wxDefaultPosition,wxDefaultSize,wxSP_LIVE_UPDATE);
    auto* tabs=new wxNotebook(split,wxID_ANY);wxScrolledWindow* pages[4];wxBoxSizer* layouts[4];
    const char* placeableNames[]={"General","Locks / traps","Durability","Scripts"};
    const char* creatureNames[]={"General","Appearance / behavior","Stats","Scripts"};
    for(int i=0;i<4;++i) {pages[i]=new wxScrolledWindow(tabs);pages[i]->SetScrollRate(0,FromDIP(12));layouts[i]=new wxBoxSizer(wxVERTICAL);pages[i]->SetSizer(layouts[i]);tabs->AddPage(pages[i],type_==2027?creatureNames[i]:placeableNames[i]);}
    const auto add=[&](int page,const char* name,const char* type,const char* label,Kind kind=Kind::Text,double minimum=0,double maximum=255,double initial=0,bool k2=false) {
        const auto index=rows_.size();auto* pane=pages[page];auto* layout=layouts[page];wxWindow* control;
        if(kind==Kind::Flag) {
            auto* flag=new wxCheckBox(pane,wxID_ANY,label);control=flag;flag->Bind(wxEVT_CHECKBOX,[this,index](wxCommandEvent&){commit(index);});
        } else {
            auto* title=new wxStaticText(pane,wxID_ANY,label);title->Wrap(FromDIP(330));layout->Add(title,0,wxLEFT|wxRIGHT|wxTOP,6);
            if(kind==Kind::Choice) {
                auto* choice=new wxChoice(pane,wxID_ANY);control=choice;choice->Bind(wxEVT_CHOICE,[this,index](wxCommandEvent&){commit(index);});
            } else if(kind==Kind::Number||kind==Kind::LocalizedRef) {
                auto* value=new wxSpinCtrlDouble(pane,wxID_ANY,{},wxDefaultPosition,wxDefaultSize,wxSP_ARROW_KEYS|wxTE_PROCESS_ENTER,minimum,maximum,initial,1);value->SetDigits(0);control=value;
                value->Bind(wxEVT_SPINCTRLDOUBLE,[this,index](wxSpinDoubleEvent&){commit(index);});
                value->Bind(wxEVT_TEXT_ENTER,[this,index](wxCommandEvent&){commit(index);});
                value->Bind(wxEVT_TEXT,[this,index](wxCommandEvent&){rows_[index].edited=true;});
                value->Bind(wxEVT_KILL_FOCUS,[this,index](wxFocusEvent& event){if(rows_[index].edited)commit(index);event.Skip();});
                value->SetToolTip(wxui::toWx("Accepted range: "+std::to_string(static_cast<long long>(minimum))+" to "+std::to_string(static_cast<long long>(maximum))));
            } else if(kind==Kind::Color) {
                auto* color=new wxColourPickerCtrl(pane,wxID_ANY,*wxWHITE);control=color;color->Bind(wxEVT_COLOURPICKER_CHANGED,[this,index](wxColourPickerEvent&){commit(index);});
            } else {
                const bool multiline=kind==Kind::LocalizedText&&std::string(name)=="Description";
                auto* text=new wxTextCtrl(pane,wxID_ANY,{},wxDefaultPosition,multiline?FromDIP(wxSize(-1,90)):wxDefaultSize,multiline?wxTE_MULTILINE:wxTE_PROCESS_ENTER);control=text;
                if(std::string(type)=="ResRef")text->SetMaxLength(16);
                text->Bind(wxEVT_TEXT_ENTER,[this,index](wxCommandEvent&){commit(index);});
                text->Bind(wxEVT_KILL_FOCUS,[this,index,text](wxFocusEvent& event){if(text->IsModified())commit(index);event.Skip();});
            }
        }
        rows_.push_back({std::string(name).substr(0,16),type,label,kind,control,initial,k2,{}});layout->Add(control,0,wxEXPAND|wxALL,6);
    };
    const auto loc=[&](const char* field,const char* label) {
        add(0,field,"CExoLocString",label,Kind::LocalizedText);
        add(0,field,"CExoLocString",(std::string(label)+" TLK reference (-1 = embedded text)").c_str(),Kind::LocalizedRef,-1,4294967294.0,-1);
    };
    add(0,"Tag","CExoString","Tag");
    if(type_==2027) {
        loc("FirstName","First name");loc("LastName","Last name");loc("Description","Description");
        add(0,"Conversation","ResRef","Conversation");add(0,"Age","INT","Age",Kind::Number,-2147483648.0,2147483647.0);
        add(0,"Gender","BYTE","Gender",Kind::Choice);
        for(const auto* label:{"Male","Female","Both","Other","None"}) {auto& row=rows_.back();static_cast<wxChoice*>(row.control)->Append(label);row.values.push_back(unsigned(row.values.size()));}
        add(0,"Race","BYTE","Race — racialtypes.2da",Kind::Choice,0,255,6);
        add(0,"GoodEvil","BYTE","Alignment (0 dark / 100 light)",Kind::Number,0,100,0);
        add(1,"Appearance_Type","WORD","Appearance — appearance.2da",Kind::Choice);
        add(1,"Phenotype","INT","Phenotype",Kind::Number,0,255);
        add(1,"Appearance_Head","BYTE","Head variation",Kind::Number,1,255,1);
        add(1,"DuplicatingHead","BYTE","Head override — heads.2da",Kind::Choice,0,255,255);
        add(1,"UseBackupHead","BYTE","Use backup head",Kind::Flag);
        for(const auto& item:std::vector<std::pair<const char*,const char*>>{{"IsPC","Player character"},{"Interruptable","Interruptible dialogue"},{"Plot","Plot / invulnerable"},{"Min1HP","Cannot fall below 1 HP"},{"PartyInteract","Party interaction"},{"NotReorienting","Do not turn when clicked"},{"Disarmable","Disarmable"},{"IsDestroyable","Destroyable"},{"IsRaiseable","Raiseable"},{"DeadSelectable","Selectable when dead"}})
            add(1,item.first,"BYTE",item.second,Kind::Flag,0,1,std::string(item.first)=="IsPC"||std::string(item.first)=="Interruptable"||std::string(item.first)=="Disarmable"||std::string(item.first)=="IsDestroyable"||std::string(item.first)=="IsRaiseable"||std::string(item.first)=="DeadSelectable"?1:0);
        for(const auto& item:std::vector<std::pair<const char*,const char*>>{{"Hologram","Hologram (K2)"},{"IgnoreCrePath","Ignore creature paths (K2)"},{"WillNotRender","Do not render (K2)"},{"Confused","Confused (K2)"}})add(1,item.first,"BYTE",item.second,Kind::Flag,0,1,0,true);
        add(1,"BaseCNPCAlignment","CHAR","Base companion alignment (-1 = current; K2)",Kind::Number,-1,100,-1,true);
        for(const auto& item:std::vector<std::pair<const char*,const char*>>{{"Str","Strength"},{"Dex","Dexterity"},{"Con","Constitution"},{"Int","Intelligence"},{"Wis","Wisdom"},{"Cha","Charisma"}})add(2,item.first,"BYTE",item.second,Kind::Number,0,255,0);
        add(2,"NaturalAC","BYTE","Natural armor class",Kind::Number);
        for(const auto& item:std::vector<std::pair<const char*,const char*>>{{"HitPoints","Base hit points"},{"CurrentHitPoints","Current base hit points"},{"ForcePoints","Base Force points"},{"CurrentForce","Current base Force points"}})add(2,item.first,"SHORT",item.second,Kind::Number,-32768,32767,std::string(item.first).find("HitPoints")!=std::string::npos?1:0);
        for(const auto& item:std::vector<std::pair<const char*,const char*>>{{"fortbonus","Fortitude bonus"},{"refbonus","Reflex bonus"},{"willbonus","Will bonus"}})add(2,item.first,"SHORT",item.second,Kind::Number,-128,127);
        add(2,"MovementRate","BYTE","Movement rate — creaturespeed.2da",Kind::Choice);
        for(const auto* script:{"ScriptHeartbeat","ScriptOnNotice","ScriptSpellAt","ScriptAttacked","ScriptDamaged","ScriptDisturbed","ScriptEndRound","ScriptDialogue","ScriptSpawn","ScriptRested","ScriptDeath","ScriptUserDefine","ScriptOnBlocked","ScriptEndDialogue"})add(3,script,"ResRef",script);
    } else {
    loc("LocName","Name");loc("Description","Description");
    add(0,"Appearance","DWORD","Appearance — placeables.2da",Kind::Choice);
    add(0,"Conversation","ResRef","Conversation");
    for(const auto& item:std::vector<std::pair<const char*,const char*>>{{"Useable","Usable"},{"Static","Static"},{"Plot","Plot / invulnerable"},{"HasInventory","Has inventory"},{"PartyInteract","Party interaction"},{"DieWhenEmpty","Destroy when empty"},{"LightState","Light on"},{"Open","Open"}})
        add(0,item.first,"BYTE",item.second,Kind::Flag,0,1,std::string(item.first)=="LightState"?1:0);
    add(0,"AnimationState","BYTE","Animation state",Kind::Choice);
    for(const auto* label:{"Default","Open","Closed","Destroyed","Activated","Deactivated"}) {
        auto& row=rows_.back();static_cast<wxChoice*>(row.control)->Append(label);row.values.push_back(unsigned(row.values.size()));
    }
    add(0,"UseTweakColor","BYTE","Use tint (K2)",Kind::Flag,0,1,0,true);
    add(0,"TweakColor","DWORD","Tint (K2)",Kind::Color,0,0,0,true);
    for(const auto& item:std::vector<std::pair<const char*,const char*>>{{"Lockable","Lockable"},{"Locked","Locked"},{"KeyRequired","Requires key"},{"AutoRemoveKey","Consume key"}})add(1,item.first,"BYTE",item.second,Kind::Flag);
    add(1,"KeyName","CExoString","Key tag");add(1,"OpenLockDC","BYTE","Open lock DC",Kind::Number);add(1,"CloseLockDC","BYTE","Close lock DC",Kind::Number);
    add(1,"OpenLockDiff","BYTE","Open lock calculation (K2)",Kind::Choice,0,255,0,true);
    for(const auto& mode:std::vector<std::pair<unsigned,const char*>>{{0,"Difficulty + modifier"},{3,"Difficulty + level + modifier"},{4,"Base DC + difficulty adjustment"}}) {
        auto& row=rows_.back();static_cast<wxChoice*>(row.control)->Append(mode.second);row.values.push_back(mode.first);
    }
    add(1,"OpenLockDiffMod","CHAR","Open lock modifier (K2)",Kind::Number,-128,127,0,true);
    for(const auto& item:std::vector<std::pair<const char*,const char*>>{{"TrapFlag","Trapped"},{"TrapDetectable","Trap detectable"},{"TrapDisarmable","Trap disarmable"},{"TrapOneShot","One-shot trap"}})add(1,item.first,"BYTE",item.second,Kind::Flag);
    add(1,"TrapType","BYTE","Trap type — traps.2da",Kind::Choice,0,255,255);
    add(1,"TrapDetectDC","BYTE","Trap detection DC",Kind::Number);add(1,"DisarmDC","BYTE","Trap disarm DC",Kind::Number);
    add(2,"HP","SHORT","Maximum hit points",Kind::Number,-32768,32767);add(2,"CurrentHP","SHORT","Current hit points",Kind::Number,-32768,32767);
    add(2,"Min1HP","BYTE","Cannot fall below 1 HP",Kind::Flag);add(2,"NotBlastable","BYTE","Cannot be blasted open (K2)",Kind::Flag,0,1,0,true);
    for(const auto& item:std::vector<std::pair<const char*,const char*>>{{"Hardness","Hardness"},{"Fort","Fortitude save"},{"Ref","Reflex save"},{"Will","Will save"}})add(2,item.first,"BYTE",item.second,Kind::Number);
    for(const auto* script:{"OnClosed","OnDamaged","OnDeath","OnDisarm","OnHeartbeat","OnInvDisturbed","OnLock","OnMeleeAttacked","OnOpen","OnSpellCastAt","OnUnlock","OnUserDefined","OnDialog","OnTrapTriggered","OnUsed","OnEndDialogue"})add(3,script,"ResRef",script);
    add(3,"OnFailToOpen","ResRef","OnFailToOpen (K2)",Kind::Text,0,0,0,true);
    }
    for(int i=0;i<4;++i)pages[i]->FitInside();
    auto* view=new wxPanel(split);auto* viewBox=new wxBoxSizer(wxVERTICAL);canvas_=new neomdl::ui::ModelCanvas(view);viewBox->Add(canvas_,1,wxEXPAND);
    auto* frame=new wxButton(view,wxID_ANY,"Frame model");viewBox->Add(frame,0,wxALL,5);frame->Bind(wxEVT_BUTTON,[this](wxCommandEvent&){canvas_->frameModel();});view->SetSizer(viewBox);
    if(type_==2027) {
        viewBox->Add(new wxStaticText(view,wxID_ANY,"Preview animation"),0,wxLEFT|wxRIGHT,6);animationPicker_=new wxChoice(view,wxID_ANY);viewBox->Add(animationPicker_,0,wxEXPAND|wxALL,6);
        animationPicker_->Bind(wxEVT_CHOICE,[this](wxCommandEvent&){const auto index=animationPicker_->GetSelection();canvas_->setAnimation(index>0?std::optional<std::size_t>(std::size_t(index-1)):std::nullopt,0,true,true);});
    }
    split->SetMinimumPaneSize(FromDIP(300));split->SplitVertically(tabs,view,FromDIP(370));split->SetSashGravity(.35);box->Add(split,1,wxEXPAND);
    auto* note=new wxStaticText(this,wxID_ANY,type_==2027?"The preview assembles the body, head and equipped models. Animation selection affects the preview. Hit/Force point fields are stored base values; the engine adds level and ability modifiers. Classes, skills, feats and inventory are available in All fields. Choose game resources in Explorer for assets.":"Open overrides saved Animation, which overrides AnimationState. Static implies plot protection. Min1HP keeps current HP at least 1. Choose game resources in Explorer for the preview. Advanced fields and inventory entries are available in All fields.");note->Wrap(FromDIP(950));box->Add(note,0,wxEXPAND|wxALL,6);
    status_=new wxStaticText(this,wxID_ANY,"Choose game resources to load appearance choices, traps and the textured model.");status_->Wrap(FromDIP(950));box->Add(status_,0,wxEXPAND|wxALL,6);SetSizer(box);
}

ObjectEditor::~ObjectEditor() {clear();DestroyChildren();}

void ObjectEditor::loadChoices() {
    for(auto& row:rows_) {
        std::string file;unsigned maximum=256;
        if(row.field=="Appearance") {file="placeables";maximum=game_==neomdl::model::GameVersion::Kotor1?256u:2147483648u;}
        else if(row.field=="TrapType") {file="traps";maximum=255;}
        else if(row.field=="Appearance_Type") {file="appearance";maximum=65536;}
        else if(row.field=="Race")file="racialtypes";
        else if(row.field=="DuplicatingHead") {file="heads";maximum=255;}
        else if(row.field=="MovementRate")file="creaturespeed";
        else continue;
        auto* choice=static_cast<wxChoice*>(row.control);choice->Clear();row.values.clear();
        if(row.field=="TrapType") {choice->Append("255 — No trap");row.values.push_back(255);}
        if(row.field=="DuplicatingHead") {choice->Append("255 — Use appearance head");row.values.push_back(255);}
        if(row.field=="MovementRate") {choice->Append("7 — From appearance");row.values.push_back(7);}
        const auto& table=tables_.table(file);
        if(row.field=="Race")maximum=static_cast<std::uint8_t>(table.rowCount());
        for(std::size_t i=0;i<table.rowCount()&&i<maximum;++i) {
            if(row.field=="MovementRate"&&i==7)continue;
            std::string label;neomdl::engine2da::getString(table,int(i),row.field=="MovementRate"?"2daname":"label",label);
            if(row.field=="Appearance") {std::string model;neomdl::engine2da::getString(table,int(i),"modelname",model);label+=" — "+model;}
            if(row.field=="DuplicatingHead")neomdl::engine2da::getString(table,int(i),"head",label);
            choice->Append(wxui::toWx(std::to_string(i)+" — "+label));row.values.push_back(unsigned(i));
        }
    }
}

void ObjectEditor::open(const neogff::GffStruct& root,neomdl::ResourceResolver resolver,neomdl::model::GameVersion game) {
    game_=game;tables_={std::move(resolver),{}};modelName_.clear();animation_.reset();canvas_->setDocument(nullptr);document_.reset();creatureKey_.clear();creatureState_.reset();creatureAppearance_.reset();
    if(type_==2027) {animationPicker_->Clear();animationPicker_->Disable();}
    for(auto& row:rows_)if(row.field=="Appearance"||row.field=="TrapType"||row.field=="Appearance_Type"||row.field=="Race"||row.field=="DuplicatingHead"||row.field=="MovementRate") {static_cast<wxChoice*>(row.control)->Clear();row.values.clear();}
    std::string error;try {loadChoices();}catch(const std::exception& e){error="Game resources: "+std::string(e.what());}
    refresh(root);
    if(!error.empty())status_->SetLabel(wxui::toWx(error));
}

void ObjectEditor::refresh(const neogff::GffStruct& root) {
    root_=&root;const bool k2=game_==neomdl::model::GameVersion::Kotor2;
    for(auto& row:rows_) {
        if(row.label=="Plot / invulnerable")row.field=field<neogff::GffByteField>(root,"Invulnerable")?"Invulnerable":"Plot";
        const auto* value=root.GetFieldByLabel(row.field);row.control->Enable(!row.k2||k2);
        if(row.kind==Kind::Flag) {
            bool checked=value?std::stoul(value->GetString())!=0:row.initial!=0;
            if(row.field=="Static"&&!value)checked=number<neogff::GffByteField>(root,"Useable")!=1;
            if(row.label=="Plot / invulnerable"&&type_==2044) {
                const bool staticObject=number<neogff::GffByteField>(root,"Static",number<neogff::GffByteField>(root,"Useable")!=1)==1;
                checked=checked||staticObject;row.control->Enable(!staticObject);row.control->SetToolTip("Invulnerable takes precedence over Plot. Static placeables are always plot protected.");
            }
            static_cast<wxCheckBox*>(row.control)->SetValue(checked);
        } else if(row.kind==Kind::Number) {
            auto* input=static_cast<wxSpinCtrlDouble*>(row.control);
            if(row.field=="CurrentHP") {
                const bool minimum=number<neogff::GffByteField>(root,"Min1HP")!=0;input->SetRange(minimum?1:-32768,32767);
                input->SetToolTip(minimum?"Accepted range: 1 to 32767 while Min1HP is enabled":"Accepted range: -32768 to 32767");
            }
            if(row.field=="OpenLockDC")row.control->Enable(!k2||number<neogff::GffByteField>(root,"OpenLockDiff")==4);
            if(row.field=="OpenLockDiffMod")row.control->Enable(k2&&number<neogff::GffByteField>(root,"OpenLockDiff")!=4);
            if(row.type=="CHAR")input->SetValue(value?double(static_cast<std::int8_t>(static_cast<const neogff::GffCharField*>(value)->value)):row.initial);
            else if(row.field=="Phenotype")input->SetValue(number<neogff::GffIntField>(root,"Phenotype")&255u);
            else if(row.field=="fortbonus"||row.field=="refbonus"||row.field=="willbonus")input->SetValue(value?static_cast<std::int8_t>(static_cast<const neogff::GffShortField*>(value)->value):0);
            else if(row.field=="CurrentHitPoints"&&!value)input->SetValue(number<neogff::GffShortField>(root,"HitPoints",1));
            else if(row.field=="CurrentForce"&&!value)input->SetValue(number<neogff::GffShortField>(root,"ForcePoints"));
            else input->SetValue(value?std::stod(value->GetString()):row.initial);
        }
        else if(row.kind==Kind::Choice) {
            unsigned current=value?unsigned(std::stoul(value->GetString())):unsigned(row.initial);
            if(row.field=="Gender")current=std::min(current,4u);
            if(row.field=="MovementRate"&&!value)current=number<neogff::GffIntField>(root,"WalkRate",0);
            if(row.field=="OpenLockDiff")row.values[0]=current!=3&&current!=4?current:0;
            const auto found=std::find(row.values.begin(),row.values.end(),current);
            static_cast<wxChoice*>(row.control)->SetSelection(found==row.values.end()?wxNOT_FOUND:int(found-row.values.begin()));
            row.control->SetToolTip(wxui::toWx("Stored value: "+std::to_string(current)));
            if(row.field=="AnimationState")row.control->Enable(!number<neogff::GffByteField>(root,"Open")&&!field<neogff::GffIntField>(root,"Animation"));
        } else if(row.kind==Kind::LocalizedRef) {
            const auto* loc=field<neogff::GffLocalizedStringField>(root,row.field);
            static_cast<wxSpinCtrlDouble*>(row.control)->SetValue(loc&&loc->strref!=0xffffffffu?double(loc->strref):-1);
        } else if(row.kind==Kind::Color) {
            const auto color=number<neogff::GffUInt32Field>(root,row.field);
            static_cast<wxColourPickerCtrl*>(row.control)->SetColour(wxColour(color&255u,(color>>8u)&255u,(color>>16u)&255u));
        } else {
            std::string text;
            if(row.kind==Kind::LocalizedText) {const auto* loc=field<neogff::GffLocalizedStringField>(root,row.field);if(loc)text=loc->GetStringById(englishId(loc));}
            else if(value)text=value->GetString();
            auto* input=static_cast<wxTextCtrl*>(row.control);input->ChangeValue(wxui::toWx(text));input->DiscardEdits();
        }
        row.edited=false;
    }
    try {if(type_==2027)updateCreaturePreview();else updatePreview();}catch(const std::exception& e){canvas_->setDocument(nullptr);document_.reset();creatureKey_.clear();creatureState_.reset();creatureAppearance_.reset();modelName_.clear();animation_.reset();if(type_==2027) {animationPicker_->Clear();animationPicker_->Disable();}status_->SetLabel(wxui::toWx("Preview: "+std::string(e.what())));}
    Layout();
}

void ObjectEditor::commit(std::size_t index) {
    try {
        auto& row=rows_[index];row.edited=false;std::string value,path=row.field;
        if(row.kind==Kind::Flag)value=static_cast<wxCheckBox*>(row.control)->GetValue()?"1":"0";
        else if(row.kind==Kind::Choice)value=std::to_string(row.values.at(std::size_t(static_cast<wxChoice*>(row.control)->GetSelection())));
        else if(row.kind==Kind::Number||row.kind==Kind::LocalizedRef)value=std::to_string(static_cast<long long>(std::llround(static_cast<wxSpinCtrlDouble*>(row.control)->GetValue())));
        else if(row.kind==Kind::Color) {
            const auto color=static_cast<wxColourPickerCtrl*>(row.control)->GetColour();
            const auto original=number<neogff::GffUInt32Field>(*root_,row.field);
            value=std::to_string((original&0xff000000u)|unsigned(color.Red())|(unsigned(color.Green())<<8u)|(unsigned(color.Blue())<<16u));
        } else {auto* input=static_cast<wxTextCtrl*>(row.control);value=wxui::toStd(input->GetValue());input->DiscardEdits();}
        if(row.kind==Kind::LocalizedText)path+="(lang"+std::to_string(englishId(field<neogff::GffLocalizedStringField>(*root_,row.field)))+")";
        if(row.kind==Kind::LocalizedRef)path+="(strref)";
        edit_(path,row.type,value,row.label);
    }catch(const std::exception& e){wxui::showError(this,e);}
}

void ObjectEditor::applyPending() {
    for(std::size_t i=0;i<rows_.size();++i) {
        const auto* text=dynamic_cast<wxTextCtrl*>(rows_[i].control);
        if(rows_[i].edited||(text&&text->IsModified()))commit(i);
    }
}

void ObjectEditor::updateCreaturePreview() {
    std::vector<std::string> key;
    for(const auto* name:{"Tag","Appearance_Type","Gender","Race","GoodEvil","Phenotype","Appearance_Head","DuplicatingHead","UseBackupHead","PM_IsDisguised","PM_Appearance"}) {
        const auto* value=root_->GetFieldByLabel(name);key.push_back(value?std::to_string(value->fieldtype)+":"+value->GetString():std::string{});
    }
    if(const auto* equipment=field<neogff::GffList>(*root_,"Equip_ItemList"))for(const auto& item:equipment->allStructs()) {
        const auto* name=field<neogff::GffResRefField>(*item,"EquippedRes");key.push_back(std::to_string(item->typeid_)+":"+(name?name->GetString():std::string{}));
    }
    if(key!=creatureKey_||!document_) {
        std::vector<std::string> diagnostics;
        auto state=neomdl::readCreatureAppearance(*root_,true,tables_.resolver,diagnostics,neomdl::resourceLoadJob(),game_);
        const bool geometry=!creatureState_||neomdl::creatureAppearanceChangesGeometry(*creatureState_,state);
        auto appearance=neomdl::resolveCreatureAppearance(state,tables_.resolver,geometry?nullptr:&*creatureAppearance_);
        const bool replace=geometry||appearance.appearance!=creatureAppearance_->appearance;
        if(replace) {
            auto document=neomdl::assembleCreatureAppearance(appearance,tables_.resolver,":creature-editor");
            canvas_->setDocument(nullptr);document_=std::move(document);canvas_->setDocument(document_.get());
            animationPicker_->Clear();animationPicker_->Append("Bind pose");
            for(std::size_t i=0;i<document_->animations().size();++i)animationPicker_->Append(wxui::toWx(document_->animation(i).name));
            animationPicker_->SetSelection(0);animationPicker_->Enable();
        } else {
            std::string headTexture;for(const auto& item:appearance.attachments)if(item.kind==neomdl::VisualAttachmentKind::CreatureHead)headTexture=item.texture;
            document_->applyVisualTextures(appearance.bodyTexture,headTexture);canvas_->refreshMaterials();
        }
        std::string message=appearance.bodyModel;
        for(const auto& item:appearance.attachments)message+=" + "+item.model;
        for(const auto& diagnostic:diagnostics)message+="\n"+diagnostic;
        for(const auto& diagnostic:appearance.diagnostics)message+="\n"+diagnostic;
        status_->SetLabel(wxui::toWx(message));creatureState_=std::move(state);creatureAppearance_=std::move(appearance);creatureKey_=std::move(key);
    }
    neomdl::OwnerVisualEffects effects;
    effects.hologram=game_==neomdl::model::GameVersion::Kotor2&&number<neogff::GffByteField>(*root_,"Hologram");
    effects.instanceOpacity=game_==neomdl::model::GameVersion::Kotor2&&number<neogff::GffByteField>(*root_,"WillNotRender")?0.0f:1.0f;
    std::string error;for(std::size_t owner=0;owner<=document_->references().size();++owner)canvas_->setOwnerVisualEffects(owner,effects,error);
}

void ObjectEditor::updatePreview() {
    const auto appearance=number<neogff::GffUInt32Field>(*root_,"Appearance");
    const auto row=game_==neomdl::model::GameVersion::Kotor1?appearance&255u:appearance;
    const auto model=neomdl::resourceKey(tables_.stringValue("placeables",row,"modelname"));
    if(modelName_!=model||!document_) {
        auto document=std::make_shared<neomdl::ModelDocument>();
        document->openResource(tables_.resolver(model,neomdl::kMdlResourceType).value(),tables_.resolver,neomdl::resourceLoadJob(),game_);
        canvas_->setDocument(nullptr);document_=std::move(document);modelName_=model;animation_.reset();canvas_->setDocument(document_.get());
    }
    const auto* saved=field<neogff::GffIntField>(*root_,"Animation");
    const auto state=number<neogff::GffByteField>(*root_,"AnimationState");
    const std::uint16_t server=number<neogff::GffByteField>(*root_,"Open")?10075u:saved?std::uint16_t(saved->value):neomdl::placeableAnimationState(std::uint8_t(state)).serverAnimation;
    if(animation_!=server) {
        animation_=server;canvas_->clearAnimationRuns();auto command=neomdl::placeableServerAnimation(server);std::string diagnostic;
        if(neomdl::resolveObjectAnimation(command,tables_.resolver,*document_,diagnostic)) {
            if(!command.playEmpty) {
                const auto clip=document_->findAnimation(command.animationName).value();const auto& clips=document_->animations();
                const auto found=std::find_if(clips.begin(),clips.end(),[&](const auto& ref){return ref.modelIndex==clip.modelIndex&&ref.animationIndex==clip.animationIndex;});
                canvas_->playAnimationRun(std::size_t(found-clips.begin()),command.playbackRate,command.runFlags);
            }
        }
        status_->SetLabel(wxui::toWx(model+" — "+(diagnostic.empty()?command.animationName:diagnostic)));
    }
    neomdl::OwnerVisualEffects effects;effects.colorShiftEnabled=game_==neomdl::model::GameVersion::Kotor2&&number<neogff::GffByteField>(*root_,"UseTweakColor");
    const auto color=number<neogff::GffUInt32Field>(*root_,"TweakColor");effects.colorShift={{float(color&255u)/255,float((color>>8u)&255u)/255,float((color>>16u)&255u)/255,1}};
    std::string error;canvas_->setOwnerVisualEffects(0,effects,error);
}

void ObjectEditor::clear() {
    for(auto& row:rows_) {row.edited=false;if(auto* text=dynamic_cast<wxTextCtrl*>(row.control))text->DiscardEdits();}
    Enable(false);root_=nullptr;canvas_->setDocument(nullptr);document_.reset();modelName_.clear();animation_.reset();creatureKey_.clear();creatureState_.reset();creatureAppearance_.reset();
    if(type_==2027) {animationPicker_->Clear();animationPicker_->Disable();}
}
std::vector<std::filesystem::path> ObjectEditor::openPaths() const {return document_?document_->openPaths():std::vector<std::filesystem::path>{};}

void editBlueprintField(neogff::GffModel& model,const std::string& path,const std::string& type,const std::string& value) {
    const auto name=path.substr(0,path.find('('));
    if(!model.gff().root()->GetFieldByLabel(name))model.addField({},name,type);
    if(type=="CHAR") {
        static_cast<neogff::GffCharField*>(model.gff().root()->GetFieldByLabel(name))->value=static_cast<char>(std::stoi(value));model.gff().dirty(true);
    } else model.setValue(path,value);
}

}
