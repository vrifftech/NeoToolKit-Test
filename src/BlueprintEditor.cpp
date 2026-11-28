#include <gff/GffText.hpp>
#include "BlueprintEditor.hpp"
#include "ItemListView.hpp"
#include "NeoWxUi.hpp"
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/listctrl.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/splitter.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/wrapsizer.h>
#include <algorithm>
#include <limits>

namespace neotoolkit {
namespace {
const neogff::GffList* inventory(const neogff::GffStruct& root) {return static_cast<const neogff::GffList*>(root.GetFieldByLabel("ItemList"));}
}

BlueprintPanel::BlueprintPanel(wxWindow* parent,Actions actions)
    :wxPanel(parent),actions_(std::move(actions)) {
    auto* box=new wxBoxSizer(wxVERTICAL);source_=new wxStaticText(this,wxID_ANY,{});box->Add(source_,0,wxEXPAND|wxALL,8);
    auto* tools=new wxWrapSizer(wxHORIZONTAL,wxREMOVE_LEADING_SPACES);
    const auto command=[&](const char* label,std::function<void()> action) {
        auto* button=new wxButton(this,wxID_ANY,label);tools->Add(button,0,wxALL,3);
        button->Bind(wxEVT_BUTTON,[this,action=std::move(action)](wxCommandEvent&){try {if(applyPending())action();}catch(const std::exception& e){wxui::showError(this,e);}});
        return button;
    };
    command("View GFF",actions_.fields);
    undo_=command("Undo",actions_.undo);redo_=command("Redo",actions_.redo);
    undo_->Show(bool(actions_.undo));redo_->Show(bool(actions_.redo));
    save_=command(actions_.saveLabel.c_str(),actions_.save);
    if(actions_.package)command("Package MOD...",actions_.package);
    box->Add(tools,0,wxEXPAND|wxLEFT|wxRIGHT,5);
    auto* split=split_=new wxSplitterWindow(this,wxID_ANY,wxDefaultPosition,wxDefaultSize,wxSP_LIVE_UPDATE);
    details_=new wxScrolledWindow(split);details_->SetScrollRate(0,FromDIP(12));auto* detailsBox=new wxBoxSizer(wxVERTICAL);details_->SetSizer(detailsBox);
    details_->Bind(wxEVT_SIZE,[this](wxSizeEvent& event){details_->SetVirtualSize(details_->GetClientSize());details_->Layout();details_->FitInside();event.Skip();});
    properties_=new wxPanel(details_);detailsBox->Add(properties_,0,wxEXPAND);
    stockPage_=new wxPanel(details_);auto* stocks=new wxBoxSizer(wxVERTICAL);
    auto* stockTitle=new wxStaticText(stockPage_,wxID_ANY,"Shop inventory");stockTitle->SetFont(stockTitle->GetFont().Bold());stocks->Add(stockTitle,0,wxALL,6);
    stockTitle->SetToolTip("Starting stock loads each row's UTI, including its initial stack size. Duplicate stock adds another copy. Trading changes the merchant's live inventory, saved in the area's GIT. Infinite belongs to this store entry.");
    stock_=new ItemListView(stockPage_,true);stock_->SetMinSize(FromDIP(wxSize(-1,180)));stocks->Add(stock_,1,wxEXPAND|wxALL,5);
    auto* row=new wxWrapSizer(wxHORIZONTAL,wxREMOVE_LEADING_SPACES);add_=new wxButton(stockPage_,wxID_ANY,"Add item...");duplicate_=new wxButton(stockPage_,wxID_ANY,"Duplicate stock");remove_=new wxButton(stockPage_,wxID_ANY,"Remove");itemFields_=new wxButton(stockPage_,wxID_ANY,"Edit UTI fields");
    for(auto* b:{add_,duplicate_,remove_,itemFields_})row->Add(b,0,wxALL,3);
    stocks->Add(row,0,wxEXPAND|wxALL,2);
    infinite_=new wxCheckBox(stockPage_,wxID_ANY,"Infinite stock for selected entry");stocks->Add(infinite_,0,wxALL,6);stockPage_->SetSizer(stocks);detailsBox->Add(stockPage_,1,wxEXPAND|wxTOP,6);
    add_->Bind(wxEVT_BUTTON,[this](wxCommandEvent&){try {addItem();}catch(const std::exception& e){wxui::showError(this,e);}});
    const auto action=[this](StockAction command,bool value){try {const auto i=stock_->GetNextItem(-1,wxLIST_NEXT_ALL,wxLIST_STATE_SELECTED);actions_.stock(command,std::size_t(i),value);}catch(const std::exception& e){wxui::showError(this,e);}};
    duplicate_->Bind(wxEVT_BUTTON,[action](wxCommandEvent&){action(StockAction::Duplicate,false);});remove_->Bind(wxEVT_BUTTON,[action](wxCommandEvent&){action(StockAction::Remove,false);});
    infinite_->Bind(wxEVT_CHECKBOX,[this,action](wxCommandEvent&){action(StockAction::Infinite,infinite_->GetValue());});
    stock_->Bind(wxEVT_LIST_ITEM_SELECTED,[this](wxListEvent& e){selectStock(int(e.GetIndex()));});
    stock_->Bind(wxEVT_LIST_ITEM_DESELECTED,[this](wxListEvent&){selectStock(-1);});
    itemFields_->Bind(wxEVT_BUTTON,[this](wxCommandEvent&){try {const auto i=stock_->GetNextItem(-1,wxLIST_NEXT_ALL,wxLIST_STATE_SELECTED);actions_.itemFields(inventory(*root_)->GetStruct(std::size_t(i))->GetFieldByLabel("InventoryRes")->GetString(),2025);}catch(const std::exception& e){wxui::showError(this,e);}});
    previews_=new wxPanel(split);auto* previewsBox=new wxBoxSizer(wxVERTICAL);previews_->SetSizer(previewsBox);
    previewPage_=new wxPanel(previews_);auto* previewBox=new wxBoxSizer(wxVERTICAL);
    preview_=new ShopPreview(previewPage_,[this](std::size_t i){stock_->SetItemState(long(i),wxLIST_STATE_SELECTED,wxLIST_STATE_SELECTED);stock_->EnsureVisible(long(i));});previewBox->Add(preview_,1,wxEXPAND|wxALL,4);
    auto* state=new wxWrapSizer(wxHORIZONTAL,wxREMOVE_LEADING_SPACES);
    const auto stateValue=[&](const char* label,int minimum){auto* group=new wxBoxSizer(wxHORIZONTAL);group->Add(new wxStaticText(previewPage_,wxID_ANY,label),0,wxALIGN_CENTER_VERTICAL|wxRIGHT,4);auto* value=new wxSpinCtrl(previewPage_,wxID_ANY,"0",wxDefaultPosition,FromDIP(wxSize(105,-1)),wxSP_ARROW_KEYS,minimum,std::numeric_limits<int>::max(),0);group->Add(value);state->Add(group,0,wxALL,4);return value;};
    auto* credits=stateValue("Preview credits",0);auto* bonus=stateValue("Bonus markup %",std::numeric_limits<int>::min());auto* onHand=stateValue("Selected item on hand",0);
    const auto updateState=[this,credits,bonus,onHand](wxCommandEvent&){preview_->setPlayerState(unsigned(credits->GetValue()),bonus->GetValue(),unsigned(onHand->GetValue()));};
    for(auto* value:{credits,bonus,onHand})value->Bind(wxEVT_TEXT,updateState);
    previewBox->Add(state,0,wxEXPAND);
    previewStatus_=new wxStaticText(previewPage_,wxID_ANY,"Choose Game resources to load the shop's GUI, textures and font atlas.");previewStatus_->Wrap(FromDIP(600));previewBox->Add(previewStatus_,0,wxEXPAND|wxALL,6);
    previewPage_->SetSizer(previewBox);previewsBox->Add(previewPage_,1,wxEXPAND);
    itemPreview_=new ItemPreview(previews_,[this]()->const auto&{return *root_;},[this]{return resolver_;});previewsBox->Add(itemPreview_,1,wxEXPAND);
    split->SetMinimumPaneSize(FromDIP(300));split->SplitVertically(details_,previews_,FromDIP(400));split->SetSashGravity(0.0);box->Add(split,1,wxEXPAND|wxALL,4);SetSizer(box);
}

void BlueprintPanel::buildProperties(std::uint16_t type) {
    for(auto& r:rows_)if(auto* text=dynamic_cast<wxTextCtrl*>(r.control))text->DiscardEdits();
    rows_.clear();properties_->DestroyChildren();
    auto* box=new wxBoxSizer(wxVERTICAL);properties_->SetSizer(box);
    wxFlexGridSizer* grid=nullptr;wxWrapSizer* prices=nullptr;
    if(type==2051) {
        auto* title=new wxStaticText(properties_,wxID_ANY,"Properties / scripts");title->SetFont(title->GetFont().Bold());box->Add(title,0,wxALL,6);
        grid=new wxFlexGridSizer(2,FromDIP(6),FromDIP(8));grid->AddGrowableCol(1);box->Add(grid,0,wxEXPAND|wxALL,6);
    }
    const auto add=[&](std::string field,std::string label,Kind kind=Kind::Value,std::string fieldType={},std::string defaultValue={}) {
        const auto index=rows_.size();field=field.substr(0,16);wxWindow* control;
        if(kind==Kind::Flag) {auto* check=new wxCheckBox(properties_,wxID_ANY,wxui::toWx(label));control=check;box->Add(check,0,wxEXPAND|wxALL,5);check->Bind(wxEVT_CHECKBOX,[this,index](wxCommandEvent&){commit(index);});}
        else {auto* caption=new wxStaticText(properties_,wxID_ANY,wxui::toWx(label));
            auto* pair=prices?new wxBoxSizer(wxHORIZONTAL):nullptr;
            if(pair) {pair->Add(caption,0,wxALIGN_CENTER_VERTICAL|wxRIGHT,FromDIP(4));prices->Add(pair,0,wxRIGHT,FromDIP(10));}
            else if(grid)grid->Add(caption,0,wxALIGN_CENTER_VERTICAL);else box->Add(caption,0,wxLEFT|wxRIGHT|wxTOP,5);
            if(kind==Kind::StoreMode) {auto* choice=new wxChoice(properties_,wxID_ANY);for(const auto* mode:{"Player buys only","Player sells only","Buy and sell"})choice->Append(mode);control=choice;choice->Bind(wxEVT_CHOICE,[this,index](wxCommandEvent&){commit(index);});}
            else {auto* text=new wxTextCtrl(properties_,wxID_ANY,{},wxDefaultPosition,wxDefaultSize,wxTE_PROCESS_ENTER);control=text;text->Bind(wxEVT_TEXT_ENTER,[this,index](wxCommandEvent&){commit(index);});text->Bind(wxEVT_KILL_FOCUS,[this,index,text](wxFocusEvent& e){if(text->IsModified())commit(index);e.Skip();});}
            if(pair) {control->SetMinSize(FromDIP(wxSize(60,-1)));pair->Add(control);}
            else if(grid) {
                const bool number=kind==Kind::LocalizedRef||fieldType=="INT";
                if(kind!=Kind::StoreMode)control->SetMinSize(FromDIP(wxSize(number?100:180,-1)));
                grid->Add(control,0,kind==Kind::LocalizedText?wxEXPAND:0);
            } else box->Add(control,0,wxEXPAND|wxALL,5);
        }
        rows_.push_back({std::move(field),std::move(label),kind,control,std::move(fieldType),std::move(defaultValue)});
        if(rows_.back().field=="MarkUp"||rows_.back().field=="MarkDown")control->SetToolTip("Percentage of item cost: 100 means the item cost before trading modifiers.");
        if(kind==Kind::LocalizedRef)control->SetToolTip("dialog.tlk string reference; -1 means no reference.");
        if(rows_.back().field=="Plot"&&(type==2042))control->SetToolTip("Invulnerable takes precedence over Plot when both fields exist. Static objects are always treated as plot.");
    };
    const auto loc=[&](const std::string& field,const std::string& label){const auto* fieldType=type==2051?"CExoLocString":"";add(field,label+(type==2051?" (English)":" — embedded English"),Kind::LocalizedText,fieldType);add(field,label+(type==2051?" StrRef":" — TLK reference (-1 = none)"),Kind::LocalizedRef,fieldType);};
    add("Tag","Tag",Kind::Value,type==2051?"CExoString":"");
    if(type==2032||type==2025)loc("LocalizedName","Name");
    else if(type==2042||type==2051)loc("LocName","Name");
    if(type==2042) {
        loc("Description","Description");add("Appearance","Appearance row");add("Plot","Plot",Kind::Flag);add("Static","Static",Kind::Flag);add("Min1HP","Cannot fall below 1 HP",Kind::Flag);add("HP","Maximum hit points");add("CurrentHP","Current hit points");
        add("Locked","Locked",Kind::Flag);add("KeyRequired","Requires a key",Kind::Flag);add("KeyName","Key tag");add("OpenLockDC","Open lock difficulty");add("CloseLockDC","Close lock difficulty");
        add("GenericType","Door type row");add("OpenState","Open state");
    }
    if(type==2032||type==2042) {add("TrapDetectable","Trap detectable",Kind::Flag);add("TrapDisarmable","Trap disarmable",Kind::Flag);add("TrapOneShot","One-shot trap",Kind::Flag);add("TrapType","Trap type");}
    if(type==2032)add("Type","Trigger type");
    if(type==2035) {
        for(const auto* key:{"Active","Positional","Looping","Random","RandomPosition"})add(key,key,Kind::Flag);
        add("Volume","Volume");add("VolumeVrtn","Volume variation");add("MinDistance","Minimum distance");add("MaxDistance","Maximum distance");add("Interval","Interval");add("IntervalVrtn","Interval variation");add("PitchVariation","Pitch variation");add("RandomRangeX","Random range X");add("RandomRangeY","Random range Y");
    }
    if(type==2051) {
        add("BuySellFlag","Shop pages",Kind::StoreMode,"BYTE");
        prices=new wxWrapSizer(wxHORIZONTAL,wxREMOVE_LEADING_SPACES);box->Add(prices,0,wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM,6);
        add("MarkUp","Buy markup (%)",Kind::Value,"INT","0");add("MarkDown","Sell markdown (%)",Kind::Value,"INT","0");prices=nullptr;
        grid=new wxFlexGridSizer(2,FromDIP(6),FromDIP(8));grid->AddGrowableCol(1);box->Add(grid,0,wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM,6);
    }
    if(type==2025) {loc("Description","Unidentified description");loc("DescIdentified","Identified description");add("BaseItem","Base item row");add("StackSize","Stack size");add("ModelVariation","Model variation");add("TextureVar","Texture variation");add("AddCost","Additional cost");add("Plot","Plot",Kind::Flag);}
    if(type==2042)add("Conversation","Conversation");
    std::vector<std::string> scripts;
    if(type==2042)scripts={"OnClosed","OnDamaged","OnDeath","OnDisarm","OnHeartbeat","OnLock","OnMeleeAttacked","OnOpen","OnSpellCastAt","OnUnlock","OnUserDefined","OnDialog","OnTrapTriggered"};
    if(type==2042)scripts.push_back("OnFailToOpen");
    if(type==2042)scripts.push_back("OnClick");
    if(type==2032)scripts={"ScriptHeartbeat","ScriptOnEnter","ScriptOnExit","ScriptUserDefine","OnTrapTriggered","OnDisarm","OnClick"};
    if(type==2051)scripts={"OnOpenStore"};
    for(const auto& name:scripts)add(name,name,Kind::Value,type==2051?"CResRef":"");
    if(type!=2051) {auto* note=new wxStaticText(properties_,wxID_ANY,"Only fields present in the template are enabled. Other fields and translations are available through View GFF.");note->Wrap(FromDIP(300));box->Add(note,0,wxEXPAND|wxALL,6);}
    properties_->Layout();
}
void BlueprintPanel::open(std::uint16_t type,neomdl::model::GameVersion game,const std::string& title,const neogff::GffStruct& root,neomdl::ResourceResolver resolver,std::shared_ptr<neomdl::GameResourceSession> session,std::shared_ptr<neoshared::bif::LooseArchiveCatalog> catalog,const std::filesystem::path& directory) {
    if(type_!=type||game_!=game) {if(type!=2044&&type!=2027)buildProperties(type);preview_->clear();itemPreview_->clear();type_=type;game_=game;}
    preview_->clear();previewStatus_->SetLabel("Choose Game resources to load this merchant's preview.");previewStatus_->Show();
    session_=std::move(session);catalog_=std::move(catalog);directory_=directory;source_->SetLabel(wxui::toWx(title+actions_.sourceNote));
    const bool object=type==2044||type==2027;split_->Show(!object);
    if(object) {
        if(!object_||object_->type()!=type) {
            if(object_) {object_->clear();GetSizer()->Detach(object_);object_->Destroy();}
            object_=new ObjectEditor(this,type,actions_.edit);GetSizer()->Add(object_,1,wxEXPAND|wxALL,4);
        }
        object_->Show();object_->Enable();object_->open(root,resolver,game);
    } else if(object_) {object_->clear();object_->Hide();}
    const bool store=type==2051,item=type==2025;stockPage_->Show(store);previewPage_->Show(store);itemPreview_->Show(item);
    if(store)stock_->setResources(resolver,session_?neoshared::game::findExistingPathCaseInsensitive(session_->installation()->installPath/"dialog.tlk"):std::filesystem::path{},game);
    if(!store&&!item&&split_->IsSplit())split_->Unsplit(previews_);
    if((store||item)&&!split_->IsSplit()) {previews_->Show();split_->SplitVertically(details_,previews_,FromDIP(400));}
    details_->Layout();details_->FitInside();
    itemPreview_->setContext(session_?session_->installation()->installPath/"dialog.tlk":std::filesystem::path{},game);
    add_->Enable(bool(session_));
    refresh(root,std::move(resolver));if(store&&session_)loadPreview();previews_->Layout();Layout();Show();Raise();
}
void BlueprintPanel::refresh(const neogff::GffStruct& root,neomdl::ResourceResolver resolver,bool itemsChanged) {
    root_=&root;resolver_=std::move(resolver);
    if(type_==2044||type_==2027) {
        if(type_==2027&&itemsChanged)object_->open(root,resolver_,game_);else object_->refresh(root,resolver_);
        return;
    }
    for(auto& row:rows_) {
        if((type_==2042)&&(row.field=="Plot"||row.field=="Invulnerable"))
            row.field=dynamic_cast<const neogff::GffByteField*>(root.GetFieldByLabel("Invulnerable"))?"Invulnerable":"Plot";
        const auto* field=root.GetFieldByLabel(row.field);row.control->Enable(field!=nullptr||!row.fieldType.empty());
        if(row.kind==Kind::Flag)static_cast<wxCheckBox*>(row.control)->SetValue(field&&std::stoul(field->GetString())!=0);
        else if(row.kind==Kind::StoreMode) {const int mode=field?std::stoi(field->GetString()):3;static_cast<wxChoice*>(row.control)->SetSelection(mode>=1&&mode<=3?mode-1:wxNOT_FOUND);}
        else {
            auto* text=static_cast<wxTextCtrl*>(row.control);if(text->IsModified())continue;std::string value=row.defaultValue;
            if(row.kind==Kind::LocalizedText) {const auto* loc=static_cast<const neogff::GffLocalizedStringField*>(field);if(loc)value=neogff::decodeGffText(loc->GetStringById(0));}
            else if(row.kind==Kind::LocalizedRef) {const auto* loc=static_cast<const neogff::GffLocalizedStringField*>(field);value=loc&&loc->strref!=0xffffffffu?std::to_string(loc->strref):"-1";}
            else if(field)value=neogff::decodeGffText(field->GetString());
            text->ChangeValue(wxui::toWx(value));text->DiscardEdits();
        }
    }
    const auto selected=stock_->GetNextItem(-1,wxLIST_NEXT_ALL,wxLIST_STATE_SELECTED);stock_->clearItems();
    if(type_==2051)if(const auto* list=inventory(root))for(std::size_t i=0;i<list->count();++i) {const auto& item=*list->GetStruct(i);const auto* ref=item.GetFieldByLabel("InventoryRes");const auto* flag=static_cast<const neogff::GffByteField*>(item.GetFieldByLabel("Infinite"));stock_->appendItem(ref?ref->GetString():"(missing InventoryRes)",flag&&flag->value);}
    if(selected>=0&&selected<stock_->GetItemCount())stock_->SetItemState(selected,wxLIST_STATE_SELECTED,wxLIST_STATE_SELECTED);
    selectStock(selected<stock_->GetItemCount()?int(selected):-1);
    if(type_==2051)try {preview_->setStore(root,resolver_,int(selected));}catch(const std::exception& e){preview_->clear();previewStatus_->SetLabel(wxui::toWx(e.what()));previewStatus_->Show();previewPage_->Layout();}
    if(type_==2025)itemPreview_->refreshItem();
}
bool BlueprintPanel::commit(std::size_t index) {
    try {
        auto& row=rows_[index];std::string value,path=row.field;
        if(row.kind==Kind::Flag)value=static_cast<wxCheckBox*>(row.control)->GetValue()?"1":"0";
        else if(row.kind==Kind::StoreMode)value=std::to_string(static_cast<wxChoice*>(row.control)->GetSelection()+1);
        else {auto* text=static_cast<wxTextCtrl*>(row.control);value=wxui::toStd(text->GetValue());
            if(row.kind==Kind::LocalizedRef)path+="(strref)";else if(row.kind==Kind::LocalizedText)path+="(lang0)";
        }
        actions_.edit(path,row.fieldType,value,row.label);
        if(auto* text=dynamic_cast<wxTextCtrl*>(row.control))text->DiscardEdits();
        return true;
    } catch(const std::exception& e){wxui::showError(this,e);return false;}
}
void BlueprintPanel::selectStock(int index) {
    const bool selected=index>=0;duplicate_->Enable(selected);remove_->Enable(selected);infinite_->Enable(selected);
    const auto* item=selected?inventory(*root_)->GetStruct(index):nullptr;const auto* ref=item?item->GetFieldByLabel("InventoryRes"):nullptr;itemFields_->Enable(ref&&!ref->GetString().empty());
    const auto* flag=item?static_cast<const neogff::GffByteField*>(item->GetFieldByLabel("Infinite")):nullptr;infinite_->SetValue(flag&&flag->value);
    stock_->SetToolTip(ref?wxui::toWx(stock_->itemDetails(ref->GetString(),flag&&flag->value)):wxString{});
    preview_->selectInventory(index);
}
void BlueprintPanel::addItem() {
    wxDialog dialog(this,wxID_ANY,"Add shop inventory item",wxDefaultPosition,FromDIP(wxSize(960,640)),wxDEFAULT_DIALOG_STYLE|wxRESIZE_BORDER);auto* box=new wxBoxSizer(wxVERTICAL);
    auto* picker=new ObjectPalette(&dialog,true);picker->setResources(session_,catalog_,directory_);box->Add(picker,1,wxEXPAND|wxALL,6);box->Add(dialog.CreateButtonSizer(wxOK|wxCANCEL),0,wxALIGN_RIGHT|wxALL,6);dialog.SetSizer(box);
    auto* ok=static_cast<wxButton*>(dialog.FindWindow(wxID_OK));ok->SetLabel("Add item");ok->Disable();picker->Bind(EVT_BLUEPRINT_SELECTION_CHANGED,[ok,picker](wxCommandEvent&){ok->Enable(picker->selectedType()==2025);});
    if(dialog.ShowModal()==wxID_OK)actions_.addItem(picker->selectedBlueprint(),picker->selectedResources());
}
void BlueprintPanel::loadPreview() {
    try {
        neomdl::ResourceLoadScope loading(neomdl::resourceLoadJob());
        const auto& install=*session_->installation();const auto path=neoshared::game::findExistingPathCaseInsensitive(install.installPath/"dialog.tlk");
        const auto game=neoshared::game::gameIdFromString(install.id).value()==neoshared::game::GameId::Kotor1?neomdl::model::GameVersion::Kotor1:neomdl::model::GameVersion::Kotor2;
        preview_->load(game==neomdl::model::GameVersion::Kotor1?"store":"store_p",resolver_,path,game);preview_->setStore(*root_,resolver_,int(stock_->GetNextItem(-1,wxLIST_NEXT_ALL,wxLIST_STATE_SELECTED)));
        previewStatus_->Hide();
    } catch(const std::exception& e){preview_->clear();previewStatus_->SetLabel(wxui::toWx(e.what()));previewStatus_->Show();wxui::showError(this,e);}
    previewPage_->Layout();
}
bool BlueprintPanel::applyPending() {
    if(object_&&!object_->applyPending())return false;
    for(std::size_t i=0;i<rows_.size();++i)if(auto* text=dynamic_cast<wxTextCtrl*>(rows_[i].control))
        if(text->IsModified()&&!commit(i))return false;
    return true;
}
void BlueprintPanel::clear() {for(auto& row:rows_)if(auto* text=dynamic_cast<wxTextCtrl*>(row.control))text->DiscardEdits();root_=nullptr;preview_->clear();itemPreview_->clear();if(object_)object_->clear();Hide();}
void BlueprintPanel::setSaveLabel(const std::string& label) {save_->SetLabel(wxui::toWx(label));}
void BlueprintPanel::setHistory(bool undo,bool redo) {undo_->Enable(undo);redo_->Enable(redo);}
void BlueprintPanel::selectInventory(std::size_t index) {stock_->SetItemState(long(index),wxLIST_STATE_SELECTED,wxLIST_STATE_SELECTED);stock_->EnsureVisible(long(index));}

BlueprintEditor::BlueprintEditor(wxWindow* parent,Actions actions)
    :wxDialog(parent,wxID_ANY,"Blueprint",wxDefaultPosition,parent->FromDIP(wxSize(1120,720)),wxDEFAULT_DIALOG_STYLE|wxRESIZE_BORDER) {
    auto fields=std::move(actions.fields);actions.fields=[this,fields=std::move(fields)]{Hide();fields();};
    panel_=new BlueprintPanel(this,std::move(actions));auto* box=new wxBoxSizer(wxVERTICAL);box->Add(panel_,1,wxEXPAND);SetSizer(box);
    Bind(wxEVT_CLOSE_WINDOW,[this](wxCloseEvent& event){if(applyPending())Hide();event.Veto();});
}
void BlueprintEditor::open(std::uint16_t type,neomdl::model::GameVersion game,const std::string& title,const neogff::GffStruct& root,
    neomdl::ResourceResolver resolver,std::shared_ptr<neomdl::GameResourceSession> session,
    std::shared_ptr<neoshared::bif::LooseArchiveCatalog> catalog,const std::filesystem::path& directory) {
    SetTitle(wxui::toWx(title));panel_->open(type,game,title,root,std::move(resolver),std::move(session),std::move(catalog),directory);Show();Raise();
}

}
