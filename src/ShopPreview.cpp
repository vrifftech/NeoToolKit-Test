#include "ShopPreview.hpp"
#include "NeoWxUi.hpp"
#include "core/AreaLoadTables.hpp"
#include <neoshared/texture/Txi.hpp>
#include <wx/dcbuffer.h>
#include <wx/graphics.h>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace neotoolkit {
namespace {
using namespace neogff;
using Rect=wxRect2DDouble;
template<class T> const T* field(const GffStruct& record,const std::string& name) {return dynamic_cast<const T*>(record.GetFieldByLabel(name));}
int integer(const GffStruct& record,const std::string& name,int initial=0) {const auto* v=field<GffIntField>(record,name);return v?v->value:initial;}
unsigned byte(const GffStruct& record,const std::string& name,unsigned initial=0) {const auto* v=field<GffByteField>(record,name);return v?v->value:initial;}
double number(const GffStruct& record,const std::string& name,double initial=0) {const auto* v=field<GffFloatField>(record,name);return v?v->value:initial;}
std::string string(const GffStruct& record,const std::string& name) {const auto* v=record.GetFieldByLabel(name);return v?v->GetString():std::string{};}
neomdl::model::Vec3 color(const GffStruct& record) {const auto* v=field<GffPositionField>(record,"COLOR");return v?neomdl::model::Vec3{v->value[0],v->value[1],v->value[2]}:neomdl::model::Vec3{1,1,1};}
Rect extent(const GffStruct& record) {
    const auto& e=*field<GffStruct>(record,"EXTENT");return {double(integer(e,"LEFT")),double(integer(e,"TOP")),double(integer(e,"WIDTH")),double(integer(e,"HEIGHT"))};
}
}

ShopPreview::ShopPreview(wxWindow* parent,std::function<void(std::size_t)> selected):wxPanel(parent),renderer_([this](const auto& name)->const auto&{return texture(name);}),selected_(std::move(selected)) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);SetMinSize(FromDIP(wxSize(500,370)));
    Bind(wxEVT_PAINT,&ShopPreview::paint,this);
    Bind(wxEVT_SIZE,[this](wxSizeEvent& e){Refresh();e.Skip();});
    Bind(wxEVT_LEFT_DOWN,[this](wxMouseEvent& e){
        const wxPoint2DDouble point((e.GetX()-offsetX_)/scale_,(e.GetY()-offsetY_)/scale_);
        for(const auto& [rect,index]:hits_)if(rect.Contains(point)) {selected_(index);break;}
    });
    Bind(wxEVT_MOUSEWHEEL,[this](wxMouseEvent& e){const wxPoint2DDouble point((e.GetX()-offsetX_)/scale_,(e.GetY()-offsetY_)/scale_);const int direction=e.GetWheelRotation()<0?1:-1;if(descriptionRect_.Contains(point))descriptionScroll_=std::clamp(descriptionScroll_+direction*36,0.0,descriptionMaximum_);else firstRow_=std::clamp(firstRow_+direction,0,int(items_.size()));Refresh();});
}
void ShopPreview::clear() {reader_.reset();textures_.reset();renderer_.clear();items_.clear();hits_.clear();sources_.clear();descriptionRect_={};descriptionScroll_=descriptionMaximum_=0;Refresh();}
const neoshared::texture::TextureData& ShopPreview::texture(const std::string& name) {
    if(const auto* cached=textures_->texture(name))return *cached;
    std::string error;if(!textures_->loadRuntimeTexture(name,resolver_,error))throw std::runtime_error(error);
    return *textures_->texture(name);
}
void ShopPreview::load(const std::string& layout,const neomdl::ResourceResolver& resolver,const std::filesystem::path& talkTable,neomdl::model::GameVersion game) {
    neomdl::ResourceLoadScope loading(neomdl::resourceLoadJob());
    clear();resolver_=resolver;game_=game;renderer_.setK2(game==neomdl::model::GameVersion::Kotor2);tlk_.load(talkTable);renderer_.setLanguage(tlk_.languageId());
    auto source=resolver_(layout,2047).value();layout_.LoadBytes(source.bytes);sources_.push_back(source.fileName+" — "+source.sourceDescription);
    renderer_.setLayout(layout,source.identity.find('#')==std::string::npos);fontScale_=renderer_.fontScale();
    reader_=std::make_unique<ItemReader>(resolver_,tlk_,game==neomdl::model::GameVersion::Kotor2);
    textures_=neomdl::ModelDocument::emptySceneDocument("shop-preview",layout,game);renderer_.cacheControl(*layout_.root());
    const auto& controls=*field<GffList>(*layout_.root(),"CONTROLS");for(const auto& c:controls.allStructs())renderer_.cacheControl(*c);
    if(game==neomdl::model::GameVersion::Kotor1)for(const auto* name:{"lbl_hex_3","lbl_hex_6","lbl_hex_7"})texture(name);
    else for(const auto* name:{"UIBIT_EQP_ITM1","UIBIT_EQP_ITM2","UIBIT_EQP_ITM3"})texture(name);
    renderer_.cacheFont("fnt_d16x16");firstRow_=0;Refresh();
}
void ShopPreview::setStore(const GffStruct& store,const neomdl::ResourceResolver& resolver,int selected) {
    neomdl::ResourceLoadScope loading(neomdl::resourceLoadJob());
    resolver_=resolver;items_.clear();selection_=selected;
    if(!loaded())return;
    markUp_=integer(store,"MarkUp");
    std::map<std::string,ItemData> templates;std::vector<Item> pending;
    if(const auto* list=field<GffList>(store,"ItemList"))for(std::size_t i=0;i<list->count();++i) {
        const auto& record=*list->GetStruct(i);const auto name=neomdl::resourceKey(string(record,"InventoryRes"));
        auto item=templates.find(name);
        if(item==templates.end()) {
            const auto resource=resolver_(name,2025).value();
            GffFile file;file.LoadBytes(resource.bytes);item=templates.emplace(name,reader_->read(*file.root())).first;
        }
        Item row;static_cast<ItemData&>(row)=item->second;row.sources={i};row.infinite=(byte(record,"Infinite")&1)!=0;
        if(!row.plot)texture(row.icon);
        pending.push_back(std::move(row));
    }
    std::stable_sort(pending.begin(),pending.end(),[](const auto& a,const auto& b){return a.cost>b.cost;});
    for(auto i=pending.rbegin();i!=pending.rend();++i) {
        auto incoming=std::move(*i);bool absorbed=false;
        for(auto& row:items_)if(stackCompatible(row,incoming)) {
            const auto total=row.stack+incoming.stack;
            if(total>row.maximumStack) {
                if(row.maximumStack>row.stack)row.sources.insert(row.sources.end(),incoming.sources.begin(),incoming.sources.end());
                else if(row.maximumStack<row.stack)incoming.sources.insert(incoming.sources.end(),row.sources.begin(),row.sources.end());
                row.stack=row.maximumStack;incoming.stack=total-row.maximumStack;
            } else {row.stack=total;row.sources.insert(row.sources.end(),incoming.sources.begin(),incoming.sources.end());absorbed=true;break;}
        }
        if(!absorbed)items_.push_back(std::move(incoming));
    }
    std::stable_sort(items_.begin(),items_.end(),[](const auto& a,const auto& b){return a.group<b.group;});Refresh();
}
void ShopPreview::stamp(wxGraphicsContext& dc,const std::string& name,const Rect& rect,const neomdl::model::Vec3& tint,double alpha,int turn,bool flipX,bool flipY) {
    renderer_.stamp(dc,name,rect,{tint.x,tint.y,tint.z},alpha,turn,flipX,flipY);
}
Rect ShopPreview::borderInner(const GffStruct& record,const std::string& label,Rect rect) {return renderer_.borderInner(record,label,rect);}
Rect ShopPreview::border(wxGraphicsContext& dc,const GffStruct& record,const std::string& label,Rect rect,std::optional<neomdl::model::Vec3> tint) {
    return renderer_.border(dc,record,label,rect,tint?std::optional<neogui::Color>{{tint->x,tint->y,tint->z}}:std::nullopt);
}
void ShopPreview::scrollbar(wxGraphicsContext& dc,const GffStruct& style,Rect rect,int count,int top,int visible) {renderer_.scrollbar(dc,style,rect,std::max(1,count-visible+1),count?top:1,count?visible:1);}
void ShopPreview::text(wxGraphicsContext& dc,const GffStruct& style,const std::string& value,Rect rect,double scale,std::optional<neomdl::model::Vec3> tint) {
    renderer_.text(dc,style,value,rect,scale,tint?std::optional<neogui::Color>{{tint->x,tint->y,tint->z}}:std::nullopt);
}
void ShopPreview::paint(wxPaintEvent&) {
    wxAutoBufferedPaintDC paint(this);paint.SetBackground(wxBrush(wxColour(14,16,20)));paint.Clear();hits_.clear();if(!loaded())return;
    try {
        std::unique_ptr<wxGraphicsContext> dc(wxGraphicsContext::Create(paint));const auto root=renderer_.layoutExtent(extent(*layout_.root()));const auto size=GetClientSize();scale_=std::min(size.x/root.m_width,size.y/root.m_height);offsetX_=(size.x-root.m_width*scale_)/2;offsetY_=(size.y-root.m_height*scale_)/2;dc->Translate(offsetX_,offsetY_);dc->Scale(scale_,scale_);dc->BeginLayer(std::clamp(number(*layout_.root(),"ALPHA",1),0.0,1.0));
        if(field<GffStruct>(*layout_.root(),"BORDER"))border(*dc,*layout_.root(),"BORDER",{0,0,root.m_width,root.m_height});
        else stamp(*dc,string(*layout_.root(),"BACKGROUND"),{0,0,root.m_width,root.m_height},{1,1,1});
        const auto& controls=*field<GffList>(*layout_.root(),"CONTROLS");const Item* selected=nullptr;for(const auto& item:items_)if(std::find(item.sources.begin(),item.sources.end(),std::size_t(selection_))!=item.sources.end()) {selected=&item;break;}
        for(const auto& control:controls.allStructs()) {
            const auto tag=string(*control,"TAG");if(tag=="LB_INVITEMS")continue;const auto rect=renderer_.layoutExtent(extent(*control));auto contentRect=rect;
            const auto* bar=field<GffStruct>(*control,"SCROLLBAR");
            if(bar) {const auto width=renderer_.layoutExtent(extent(*bar)).m_width;contentRect.m_width-=width;if(byte(*control,"LEFTSCROLLBAR",1))contentRect.m_x+=width;}
            auto inner=border(*dc,*control,"BORDER",contentRect);
            if(field<GffStruct>(*control,"HILIGHT"))inner.Intersect(borderInner(*control,"HILIGHT",contentRect));
            if(tag=="LB_SHOPITEMS") {
                const auto& proto=*field<GffStruct>(*control,"PROTOITEM");const auto& fontStyle=*field<GffStruct>(proto,"TEXT");const double unit=renderer_.itemScale(),pad=renderer_.padding(*control),rowHeight=game_==neomdl::model::GameVersion::Kotor1?int(56*unit):36;
                const int capacity=int(inner.m_height/(rowHeight+pad));const auto count=std::count_if(items_.begin(),items_.end(),[](const auto& item){return !item.plot;});firstRow_=std::clamp(firstRow_,0,std::max(0,int(count)-capacity));
                const int slack=int(inner.m_height-pad-capacity*(rowHeight+pad)),extra=capacity?slack/capacity:0;int remainder=slack-extra*capacity;
                double y=inner.m_y+pad;int visible=0,drawn=0;dc->PushState();dc->Clip(inner.m_x,inner.m_y,inner.m_width,inner.m_height);
                for(const auto& item:items_)if(!item.plot&&visible++>=firstRow_&&drawn<capacity) {
                    double height=rowHeight+extra;if(remainder!=0) {++height;++y;--remainder;}++drawn;
                    const Rect row(inner.m_x+pad,y,inner.m_width-2*pad,height),icon(row.m_x,row.m_y,rowHeight,rowHeight),caption(row.m_x+rowHeight,row.m_y,row.m_width-rowHeight,rowHeight);
                    const bool k1=game_==neomdl::model::GameVersion::Kotor1,highlight=std::find(item.sources.begin(),item.sources.end(),std::size_t(selection_))!=item.sources.end();
                    const neomdl::model::Vec3 tint=k1?(highlight?neomdl::model::Vec3{0.98f,1,0}:neomdl::model::Vec3{0,0.66f,0.98f}):(highlight?neomdl::model::Vec3{0.7f,0.7f,0.6f}:neomdl::model::Vec3{0.31f,0.25f,0.09f});
                    auto content=border(*dc,proto,highlight?"HILIGHT":"BORDER",caption,tint);
                    content.Intersect(borderInner(proto,highlight?"BORDER":"HILIGHT",caption));
                    const auto* frame=k1?(item.stack>=100?"lbl_hex_7":item.stack>=2?"lbl_hex_6":"lbl_hex_3"):(item.stack>=100?"UIBIT_EQP_ITM3":item.stack>=2?"UIBIT_EQP_ITM2":"UIBIT_EQP_ITM1");
                    stamp(*dc,frame,icon,tint);stamp(*dc,item.icon,icon,{1,1,1});text(*dc,fontStyle,item.name,content,fontScale_,highlight?tint:color(fontStyle));
                    if(item.stack>=2) {
                        const auto countTint=k1?neomdl::model::Vec3{0,0.66f,0.98f}:color(fontStyle);const double width=item.stack>=100?55:21;
                        GffStruct count;count.AddField(std::make_unique<GffResRefField>("FONT","fnt_d16x16"));count.AddField(std::make_unique<GffPositionField>("COLOR",countTint.x,countTint.y,countTint.z));count.AddField(std::make_unique<GffIntField>("ALIGNMENT",k1||item.stack>=100?36:34));
                        const Rect countRect=k1?Rect(icon.m_x,icon.m_y+32*unit,rowHeight-(item.stack>=10?2:1)*unit,24*unit):Rect(icon.GetRight()-width,icon.m_y+17,width+1,20);
                        text(*dc,count,std::to_string(item.stack),countRect,unit);
                    }
                    hits_.push_back({row,item.sources.front()});y+=height+pad;
                }
                dc->PopState();
                if(bar)scrollbar(*dc,*bar,{byte(*control,"LEFTSCROLLBAR",1)?rect.m_x:contentRect.GetRight(),rect.m_y,renderer_.layoutExtent(extent(*bar)).m_width,rect.m_height},int(count),firstRow_,std::min(capacity,int(count)));
            } else if(tag=="LB_DESCRIPTION") {
                const auto pad=renderer_.padding(*control);descriptionRect_={inner.m_x+pad,inner.m_y+pad,inner.m_width-2*pad,inner.m_height-2*pad};descriptionMaximum_=0;
                if(selected) {const auto& proto=*field<GffStruct>(*control,"PROTOITEM");const auto height=renderer_.text(*dc,*field<GffStruct>(proto,"TEXT"),selected->description,descriptionRect_,fontScale_,{},descriptionScroll_);descriptionMaximum_=std::max(0.0,height-descriptionRect_.m_height);}
                descriptionScroll_=std::min(descriptionScroll_,descriptionMaximum_);
                if(bar)renderer_.scrollbar(*dc,*bar,{byte(*control,"LEFTSCROLLBAR",1)?rect.m_x:contentRect.GetRight(),rect.m_y,renderer_.layoutExtent(extent(*bar)).m_width,rect.m_height},int(descriptionMaximum_)+1,descriptionMaximum_&&descriptionScroll_==descriptionMaximum_?int(descriptionMaximum_)+1:int(descriptionScroll_),descriptionMaximum_?int(descriptionRect_.m_height):1);
            }
            else if(const auto* style=field<GffStruct>(*control,"TEXT")) {
                std::string value=string(*style,"TEXT");if(const auto* ref=field<GffUInt32Field>(*style,"STRREF");ref&&ref->value!=0xffffffffu)value=tlk_.resolve(ref->value).value();
                if(tag=="LBL_BUYSELL")value=tlk_.resolve(32132).value();
                if(tag=="BTN_Accept")value=tlk_.resolve(game_==neomdl::model::GameVersion::Kotor1?32132:125552).value();
                if(tag=="BTN_Examine")value=tlk_.resolve(41938).value();
                if(tag=="LBL_COST")value=tlk_.resolve(41943).value();
                if(tag=="LBL_STOCK_VALUE")value=selected?(selected->infinite?tlk_.resolve(41951).value():std::to_string(selected->stack)):"";
                if(tag=="LBL_COST_VALUE")value=selected?std::to_string(std::int32_t(selected->cost*(std::uint32_t(markUp_)+std::uint32_t(bonusMarkup_))/100u)):"";
                if(tag=="LBL_ONHAND_VALUE")value=selected?std::to_string(onHand_):"";
                if(tag=="LBL_CREDITS_VALUE")value=std::to_string(credits_);
                text(*dc,*style,value,inner,fontScale_);
            }
        }
        dc->EndLayer();
    } catch(const std::exception& e) {clear();wxui::showError(this,e);}
}
std::string ShopPreview::sources() const {std::string report;for(const auto& source:sources_)report+=source+"\n";report+="Talk table: "+neoshared::pathToUtf8(tlk_.filename())+"\n";if(textures_) {for(const auto& path:textures_->openPaths())report+="Archive/file: "+neoshared::pathToUtf8(path)+"\n";for(const auto& name:textures_->textureLoadOrder())report+=name+" — game texture\n";}return report;}

}
