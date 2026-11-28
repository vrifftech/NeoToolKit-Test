#include "ItemPreview.hpp"
#include "NeoWxUi.hpp"
#include "wx/ModelCanvas.hpp"
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/image.h>
#include <wx/sizer.h>
#include <wx/splitter.h>
#include <wx/statbmp.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/wrapsizer.h>

namespace neotoolkit {
ItemPreview::ItemPreview(wxWindow* parent,std::function<const neogff::GffStruct&()> item,std::function<neomdl::ResourceResolver()> resources)
    :wxPanel(parent),item_(std::move(item)),resources_(std::move(resources)) {
    auto* box=new wxBoxSizer(wxVERTICAL);auto* tools=new wxWrapSizer(wxHORIZONTAL,wxREMOVE_LEADING_SPACES);
    modelMode_=new wxChoice(this,wxID_ANY);modelMode_->Append("Item model");modelMode_->Append("Ground model");modelMode_->SetSelection(0);tools->Add(modelMode_,0,wxALL,4);auto* frame=new wxButton(this,wxID_ANY,"Frame model");tools->Add(frame,0,wxALL,4);box->Add(tools,0,wxEXPAND);
    auto* heading=new wxBoxSizer(wxHORIZONTAL);icon_=new wxStaticBitmap(this,wxID_ANY,wxNullBitmap);icon_->SetMinSize(FromDIP(wxSize(96,96)));heading->Add(icon_,0,wxALL,6);title_=new wxStaticText(this,wxID_ANY,"Item preview");heading->Add(title_,1,wxALIGN_CENTER_VERTICAL|wxALL,6);box->Add(heading,0,wxEXPAND);
    auto* split=new wxSplitterWindow(this,wxID_ANY,wxDefaultPosition,wxDefaultSize,wxSP_LIVE_UPDATE);
    details_=new wxTextCtrl(split,wxID_ANY,{},wxDefaultPosition,wxDefaultSize,wxTE_MULTILINE|wxTE_READONLY);
    canvas_=new neomdl::ui::ModelCanvas(split);split->SetMinimumPaneSize(FromDIP(120));split->SplitHorizontally(details_,canvas_,FromDIP(230));split->SetSashGravity(0.0);box->Add(split,1,wxEXPAND);
    CallAfter([this,split]{split->SetSashPosition(FromDIP(160));});
    status_=new wxStaticText(this,wxID_ANY,"Choose Game resources to preview this item.");status_->Wrap(FromDIP(480));box->Add(status_,0,wxEXPAND|wxALL,6);SetSizer(box);
    frame->Bind(wxEVT_BUTTON,[this](wxCommandEvent&){canvas_->frameModel();});
    modelMode_->Bind(wxEVT_CHOICE,[this](wxCommandEvent&){modelKey_.clear();refreshItem();});
}
ItemPreview::~ItemPreview() {canvas_->setDocument(nullptr);DestroyChildren();}
void ItemPreview::setContext(const std::filesystem::path& talkTable,neomdl::model::GameVersion game) {clear();talkTable_=talkTable;game_=game;}
void ItemPreview::refreshItem() {
    if(talkTable_.empty()) {clearItem();return;}
    try {
        // Callers bind the current item and resolver after setting its context.
        if(!reader_) {
            tlk_.load(talkTable_);resolver_=resources_();
            assets_=neomdl::ModelDocument::emptySceneDocument("item-preview","Item preview",game_);
            reader_=std::make_unique<ItemReader>(resolver_,tlk_,game_==neomdl::model::GameVersion::Kotor2);
        }
        update();
    } catch(const std::exception& e){error(e.what());}
}
void ItemPreview::update() {
    neomdl::ResourceLoadScope loading(neomdl::resourceLoadJob());
    const auto data=reader_->read(item_());title_->SetLabel(wxui::toWx(data.name));details_->ChangeValue(wxui::toWx(data.description));
    std::string failure,errors;
    try {if(data.icon!=iconKey_) {
        icon_->SetBitmap(wxNullBitmap);iconKey_.clear();status_->SetLabel(wxui::toWx("Icon: "+data.icon));
        if(!assets_->texture(data.icon)&&!assets_->loadRuntimeTexture(data.icon,resolver_,failure))throw std::runtime_error(failure);
        const auto& pixels=assets_->texture(data.icon)->layers.front();wxImage image(int(pixels.width),int(pixels.height));image.InitAlpha();
        for(std::size_t i=0;i<pixels.rgba.size()/4;++i) {for(unsigned c=0;c<3;++c)image.GetData()[3*i+c]=pixels.rgba[4*i+c];image.GetAlpha()[i]=pixels.rgba[4*i+3];}
        icon_->SetBitmap(wxBitmap(image.Scale(FromDIP(96),FromDIP(96),wxIMAGE_QUALITY_HIGH)));iconKey_=data.icon;
    }} catch(const std::exception& e){errors="Icon "+data.icon+": "+e.what();}
    auto name=modelMode_->GetSelection()?data.groundModel:data.model;const auto texture=modelMode_->GetSelection()?std::string{}:data.texture;
    const auto key=name+"/"+texture;
    std::shared_ptr<neomdl::ModelDocument> visual;
    try {if(key!=modelKey_) {
        status_->SetLabel(wxui::toWx("Model: "+name));auto document=std::make_unique<neomdl::ModelDocument>();
        auto resource=resolver_(name,2002);
        if(!resource)throw std::runtime_error("Cannot find "+name+".mdl in this item's game resources.");
        document->openResource(std::move(*resource),resolver_,neomdl::resourceLoadJob(),game_,{},true,false);
        if(!texture.empty()&&!document->loadRuntimeTexture(texture,resolver_,failure))throw std::runtime_error(failure);
        visual=neomdl::ModelDocument::visualView(std::shared_ptr<neomdl::ModelDocument>(std::move(document)),"item-preview",texture);
        const bool preserve=modelKey_.substr(0,modelKey_.find('/'))==name;
        canvas_->setDocument(visual.get(),preserve);visual_=std::move(visual);modelKey_=key;
    }} catch(const std::exception& e) {
        canvas_->setDocument(nullptr);visual_.reset();modelKey_.clear();
        if(!errors.empty())errors+='\n';
        errors+="Model "+name+": "+e.what();
    }
    status_->SetLabel(wxui::toWx("Model: "+name+(texture.empty()?"":" | Texture: "+texture)+" | Stack: "+std::to_string(data.stack)+" | Cost: "+std::to_string(data.cost)+(data.modelType==1&&!modelMode_->GetSelection()?" | Male preview body":"")+(errors.empty()?"":"\n"+errors)));status_->Wrap(FromDIP(480));Layout();
}
void ItemPreview::error(const std::string& message) {clearItem();status_->SetLabel(wxui::toWx(message));status_->Wrap(FromDIP(480));Layout();}
void ItemPreview::clearItem() {canvas_->setDocument(nullptr);visual_.reset();modelKey_.clear();iconKey_.clear();title_->SetLabel("Item preview");details_->Clear();icon_->SetBitmap(wxNullBitmap);status_->SetLabel("Choose Game resources to preview this item.");}
void ItemPreview::clear() {clearItem();assets_.reset();reader_.reset();tlk_.clear();resolver_={};}
std::vector<std::filesystem::path> ItemPreview::openPaths() const {std::vector<std::filesystem::path> paths;if(tlk_.loaded())paths.push_back(tlk_.filename());if(visual_) {const auto inputs=visual_->openPaths();paths.insert(paths.end(),inputs.begin(),inputs.end());}if(assets_) {const auto inputs=assets_->openPaths();paths.insert(paths.end(),inputs.begin(),inputs.end());}return paths;}
}
