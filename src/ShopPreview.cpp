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
unsigned word(const GffStruct& record,const std::string& name,unsigned initial=0) {const auto* v=field<GffWordField>(record,name);return v?v->value:initial;}
double number(const GffStruct& record,const std::string& name,double initial=0) {const auto* v=field<GffFloatField>(record,name);return v?v->value:initial;}
std::string string(const GffStruct& record,const std::string& name) {const auto* v=record.GetFieldByLabel(name);return v?v->GetString():std::string{};}
neomdl::model::Vec3 color(const GffStruct& record) {const auto* v=field<GffPositionField>(record,"COLOR");return v?neomdl::model::Vec3{v->value[0],v->value[1],v->value[2]}:neomdl::model::Vec3{1,1,1};}
Rect extent(const GffStruct& record) {
    const auto& e=*field<GffStruct>(record,"EXTENT");return {double(integer(e,"LEFT")),double(integer(e,"TOP")),double(integer(e,"WIDTH")),double(integer(e,"HEIGHT"))};
}
std::string localized(const GffStruct& record,const std::string& name,const neotlk::TlkLookup& tlk) {
    const auto* loc=field<GffLocalizedStringField>(record,name);std::string value;bool embedded=false;
    if(loc) {
        for(const auto& sub:loc->substrings)if(unsigned(sub.stringid/2)==tlk.languageId()&&(sub.stringid%2==0||tlk.languageId()==0)) {value=sub.GetString();embedded=true;}
        if(!embedded&&loc->strref!=0xffffffffu)value=tlk.resolve(loc->strref).value();
    }
    return value;
}
}

ShopPreview::ShopPreview(wxWindow* parent,std::function<void(std::size_t)> selected):wxPanel(parent),selected_(std::move(selected)) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);SetMinSize(FromDIP(wxSize(500,370)));
    Bind(wxEVT_PAINT,&ShopPreview::paint,this);
    Bind(wxEVT_SIZE,[this](wxSizeEvent& e){Refresh();e.Skip();});
    Bind(wxEVT_LEFT_DOWN,[this](wxMouseEvent& e){
        const wxPoint2DDouble point((e.GetX()-offsetX_)/scale_,(e.GetY()-offsetY_)/scale_);
        for(const auto& [rect,index]:hits_)if(rect.Contains(point)) {selected_(index);break;}
    });
    Bind(wxEVT_MOUSEWHEEL,[this](wxMouseEvent& e){firstRow_=std::clamp(firstRow_+(e.GetWheelRotation()<0?1:-1),0,int(items_.size()));Refresh();});
}
void ShopPreview::clear() {textures_.reset();fonts_.clear();bitmaps_.clear();items_.clear();hits_.clear();sources_.clear();Refresh();}
const neoshared::texture::TextureData& ShopPreview::texture(const std::string& name) {
    if(const auto* cached=textures_->texture(name))return *cached;
    std::string error;if(!textures_->loadRuntimeTexture(name,resolver_,error))throw std::runtime_error(error);
    return *textures_->texture(name);
}
ShopPreview::Font& ShopPreview::font(const std::string& name) {
    auto found=fonts_.find(name);if(found!=fonts_.end())return found->second;
    const auto& source=texture(name);Font value;std::vector<wxPoint2DDouble>* list=nullptr;std::size_t remaining=0;
    for(const auto& line:neoshared::texture::parseTxiEntries(source.txi)) {
        if(line.listData&&remaining) {std::istringstream row(line.value);double x,y,z;row>>x>>y>>z;list->push_back({x,y});--remaining;continue;}
        if(line.key=="fontheight")value.height=std::stod(line.value)*100;
        else if(line.key=="texturewidth")value.width=std::stod(line.value)*100;
        else if(line.key=="spacingr")value.spacing=std::stod(line.value)*100;
        else if(line.key=="spacingb")value.lineSpacing=std::stod(line.value)*100;
        else if(line.key=="upperleftcoords"||line.key=="lowerrightcoords") {list=line.key=="upperleftcoords"?&value.upper:&value.lower;remaining=std::stoul(line.value);}
    }
    return fonts_.emplace(name,std::move(value)).first->second;
}
void ShopPreview::cacheControl(const GffStruct& control) {
    for(const auto* label:{"BORDER","HILIGHT","SCROLLBAR","PROTOITEM","DIR","THUMB"})if(const auto* sub=field<GffStruct>(control,label))cacheControl(*sub);
    for(const auto* label:{"CORNER","EDGE","FILL","IMAGE","BACKGROUND"}) {const auto name=string(control,label);if(!name.empty())texture(name);}
    if(const auto* t=field<GffStruct>(control,"TEXT")) {const auto name=string(*t,"FONT");font(name.empty()?"dialogfont16x16":name);}
}
void ShopPreview::load(const std::string& layout,const neomdl::ResourceResolver& resolver,const std::filesystem::path& talkTable,neomdl::model::GameVersion game) {
    neomdl::ResourceLoadScope loading(neomdl::resourceLoadJob());
    clear();resolver_=resolver;game_=game;tlk_.load(talkTable);
    auto source=resolver_(layout,2047).value();layout_.LoadBytes(source.bytes);sources_.push_back(source.fileName+" — "+source.sourceDescription);
    fontScale_=game==neomdl::model::GameVersion::Kotor1?extent(*layout_.root()).m_height*1.3/768:1;
    textures_=neomdl::ModelDocument::emptySceneDocument("shop-preview",layout,game);cacheControl(*layout_.root());
    const auto& controls=*field<GffList>(*layout_.root(),"CONTROLS");for(const auto& c:controls.allStructs())cacheControl(*c);
    if(game==neomdl::model::GameVersion::Kotor1)for(const auto* name:{"lbl_hex_3","lbl_hex_6","lbl_hex_7"})texture(name);
    else for(const auto* name:{"UIBIT_EQP_ITM1","UIBIT_EQP_ITM2","UIBIT_EQP_ITM3"})texture(name);
    font("fnt_d16x16");firstRow_=0;Refresh();
}
void ShopPreview::setStore(const GffStruct& store,const neomdl::ResourceResolver& resolver,int selected) {
    neomdl::ResourceLoadScope loading(neomdl::resourceLoadJob());
    resolver_=resolver;items_.clear();selection_=selected;
    if(!loaded())return;
    neomdl::detail::AreaLoadTables tables{resolver,{}};
    if(const auto* list=field<GffList>(store,"ItemList"))for(std::size_t i=0;i<list->count();++i) {
        const auto& record=*list->GetStruct(i);const auto resource=resolver_(string(record,"InventoryRes"),2025).value();GffFile item;item.LoadBytes(resource.bytes);const auto& root=*item.root();
        Item row;row.source=i;row.name=localized(root,"LocalizedName",tlk_);
        row.description=localized(root,byte(root,"Identified",1)&&root.GetFieldByLabel("DescIdentified")?"DescIdentified":"Description",tlk_);
        row.stack=word(root,"StackSize",1);row.infinite=(byte(record,"Infinite")&1)!=0;row.plot=byte(root,"Plot")!=0;
        const auto base=std::uint32_t(integer(root,"BaseItem",30));int modelType=0,varies=0,group=0;
        neomdl::engine2da::getInt(tables.table("baseitems"),int(base),"ModelType",modelType);
        neomdl::engine2da::getInt(tables.table("baseitems"),int(base),"GenderSpecific",varies);
        neomdl::engine2da::getInt(tables.table("baseitems"),int(base),"StorePanelSort",group);row.group=std::uint8_t(group);
        modelType=std::uint8_t(modelType);varies=std::uint8_t(varies);
        auto variation=byte(root,"ModelVariation",std::max(1u,byte(root,"ModelPart1",1)));
        if(modelType!=0)variation=modelType==1?byte(root,"TextureVar",1):0;
        auto itemClass=tables.stringValue("baseitems",base,"ItemClass");itemClass.resize(std::min<std::size_t>(11,itemClass.size()));
        std::ostringstream icon;icon<<'i'<<itemClass<<(varies?"_m_":"_")<<std::setfill('0')<<std::setw(3)<<variation;row.icon=neomdl::resourceKey(icon.str().substr(0,16));
        if(!row.plot)texture(row.icon);
        items_.push_back(std::move(row));
    }
    std::stable_sort(items_.begin(),items_.end(),[](const auto& a,const auto& b){return a.group<b.group;});Refresh();
}
void ShopPreview::stamp(wxGraphicsContext& dc,const std::string& name,const Rect& rect,const neomdl::model::Vec3& tint,double alpha,int turn,bool flipX,bool flipY) {
    if(name.empty()||rect.m_width<=0||rect.m_height<=0)return;
    const auto key=name+"/"+std::to_string(tint.x)+"/"+std::to_string(tint.y)+"/"+std::to_string(tint.z)+"/"+std::to_string(alpha)+"/"+std::to_string(turn)+std::to_string(flipX)+std::to_string(flipY);
    auto bitmap=bitmaps_.find(key);
    if(bitmap==bitmaps_.end()) {
        const auto& pixels=texture(name).layers.front();wxImage image(int(pixels.width),int(pixels.height));image.InitAlpha();
        for(std::size_t i=0;i<pixels.rgba.size()/4;++i) {image.GetData()[3*i]=std::uint8_t(pixels.rgba[4*i]*tint.x);image.GetData()[3*i+1]=std::uint8_t(pixels.rgba[4*i+1]*tint.y);image.GetData()[3*i+2]=std::uint8_t(pixels.rgba[4*i+2]*tint.z);image.GetAlpha()[i]=std::uint8_t(pixels.rgba[4*i+3]*alpha);}
        if(flipX)image=image.Mirror();
        if(flipY)image=image.Mirror(false);
        for(int i=0;i<turn;++i)image=image.Rotate90();
        bitmap=bitmaps_.emplace(key,wxBitmap(image)).first;
    }
    dc.DrawBitmap(bitmap->second,rect.m_x,rect.m_y,rect.m_width,rect.m_height);
}
Rect ShopPreview::borderInner(const GffStruct& record,const std::string& label,Rect rect) {
    const auto* style=field<GffStruct>(record,label);if(!style)return rect;
    const auto corner=string(*style,"CORNER"),edge=string(*style,"EDGE");double dimension=integer(*style,"DIMENSION");
    if(dimension==0&&!corner.empty()&&!edge.empty())dimension=texture(corner).layers.front().height;
    Rect inner(rect.m_x+dimension,rect.m_y+dimension,rect.m_width-2*dimension,rect.m_height-2*dimension);
    if(inner.m_width<1||inner.m_width<dimension/2) {inner.m_x=rect.m_x+rect.m_width/2;inner.m_width=0;}
    if(inner.m_height<1||inner.m_height<dimension/2) {inner.m_y=rect.m_y+rect.m_height/2;inner.m_height=0;}
    const auto offset=integer(*style,"INNEROFFSET"),offsetY=game_==neomdl::model::GameVersion::Kotor1?offset:integer(*style,"INNEROFFSETY",offset);
    const double padX=std::min(double(offset),dimension),padY=offsetY?offsetY:offset;
    return {inner.m_x-padX,inner.m_y-padY,inner.m_width+2*padX,inner.m_height+2*padY};
}
Rect ShopPreview::border(wxGraphicsContext& dc,const GffStruct& record,const std::string& label,Rect rect,std::optional<neomdl::model::Vec3> overrideTint) {
    const auto* style=field<GffStruct>(record,label);if(!style)return rect;
    const auto corner=string(*style,"CORNER"),edge=string(*style,"EDGE"),fill=string(*style,"FILL");const auto tint=overrideTint.value_or(color(*style));
    double dimension=integer(*style,"DIMENSION");
    if(dimension==0&&!corner.empty()&&!edge.empty())dimension=texture(corner).layers.front().height;
    double x=0,y=0;if(!corner.empty()) {const auto& p=texture(corner).layers.front();x=dimension?dimension:p.width;y=dimension?dimension:p.height;x=std::min(x,rect.m_width/2);y=std::min(y,rect.m_height/2);}
    const Rect inner(rect.m_x+x,rect.m_y+y,rect.m_width-2*x,rect.m_height-2*y);
    stamp(dc,corner,{rect.m_x,rect.m_y,x,y},tint);stamp(dc,corner,{rect.GetRight()-x,rect.m_y,x,y},tint,1,0,true);
    stamp(dc,corner,{rect.m_x,rect.GetBottom()-y,x,y},tint,1,0,false,true);stamp(dc,corner,{rect.GetRight()-x,rect.GetBottom()-y,x,y},tint,1,2);
    if(!edge.empty()) {
        const auto tile=dimension?dimension:double(texture(edge).layers.front().width);
        int columns=int(inner.m_width/tile),rows=int(inner.m_height/tile);
        if(columns==0&&inner.m_width>=tile/2)columns=1;
        if(rows==0&&inner.m_height>=tile/2)rows=1;
        for(int i=0;i<columns;++i) {const auto w=inner.m_width/columns;stamp(dc,edge,{inner.m_x+i*w,rect.m_y,w,y},tint);stamp(dc,edge,{inner.m_x+i*w,rect.GetBottom()-y,w,y},tint,1,2);}
        for(int i=0;i<rows;++i) {const auto h=inner.m_height/rows;stamp(dc,edge,{rect.m_x,inner.m_y+i*h,x,h},tint,1,1);stamp(dc,edge,{rect.GetRight()-x,inner.m_y+i*h,x,h},tint,1,3);}
    }
    if(!fill.empty()) {
        const auto styleId=integer(*style,"FILLSTYLE");
        if(styleId==2||(styleId==3&&game_==neomdl::model::GameVersion::Kotor2))stamp(dc,fill,styleId==3?rect:inner,tint);
        else if(styleId==0||styleId==1) {
            const auto& p=texture(fill).layers.front();dc.PushState();dc.Clip(inner.m_x,inner.m_y,inner.m_width,inner.m_height);
            if(styleId==1)stamp(dc,fill,{inner.m_x+(inner.m_width-p.width)/2,inner.m_y+(inner.m_height-p.height)/2,double(p.width),double(p.height)},tint);
            else for(double yy=inner.m_y;yy<inner.GetBottom();yy+=p.height)for(double xx=inner.m_x;xx<inner.GetRight();xx+=p.width)stamp(dc,fill,{xx,yy,double(p.width),double(p.height)},tint);
            dc.PopState();
        }
    }
    return borderInner(record,label,rect);
}
void ShopPreview::scrollbar(wxGraphicsContext& dc,const GffStruct& style,Rect rect,int count,int top,int visible) {
    const double arrow=std::min(rect.m_width,rect.m_height/2);const Rect track(rect.m_x,rect.m_y+arrow,rect.m_width,rect.m_height-2*arrow);
    const int maximum=std::max(1,count-visible+1);const bool drawMode=byte(style,"DRAWMODE")!=0;
    const auto image=[&](const char* label,Rect area,bool flipY) {
        const auto& params=*field<GffStruct>(style,label);const auto turn=field<GffFloatField>(params,"ROTATE")?int(number(params,"ROTATE")/90):integer(params,"ROTATESTYLE");
        const auto name=string(params,"IMAGE");
        if(!name.empty()&&integer(params,"DRAWSTYLE")) {
            const auto& pixels=texture(name).layers.front();const double width=std::min(area.m_width,double(pixels.width)),height=std::min(area.m_height,double(pixels.height));const auto alignment=integer(params,"ALIGNMENT",18);
            if(alignment&2)area.m_x+=(area.m_width-width)/2;else if(alignment&4)area.m_x+=area.m_width-width;
            if(alignment&16)area.m_y+=(area.m_height-height)/2;else if(alignment&32)area.m_y+=area.m_height-height;
            area.m_width=width;area.m_height=height;
        }
        const int flip=std::string(label)=="DIR"?(flipY?4:0):integer(params,"FLIPSTYLE");
        stamp(dc,name,area,{1,1,1},1,(turn%4+4)%4,(flip&8)!=0,(flip&4)!=0);
    };
    if(!drawMode) {
        const auto inner=border(dc,style,"BORDER",track);const double height=maximum>=2?std::max(4,int(double(visible)/(visible+maximum)*inner.m_height)):inner.m_height;
        const double y=maximum>=2?inner.m_y+(inner.m_height-height)*top/(maximum-1):inner.m_y;
        image("THUMB",{inner.m_x,y,inner.m_width,height},false);
    }
    if(!drawMode||top>0)image("DIR",{rect.m_x,rect.m_y,rect.m_width,arrow},false);
    if(!drawMode||top<maximum-1)image("DIR",{rect.m_x,rect.GetBottom()-arrow,rect.m_width,arrow},true);
}
void ShopPreview::text(wxGraphicsContext& dc,const GffStruct& style,const std::string& value,Rect rect,double scale,std::optional<neomdl::model::Vec3> overrideTint) {
    if(value.empty())return;
    auto name=string(style,"FONT");if(name.empty())name="dialogfont16x16";auto& f=font(name);const auto& pixels=texture(name).layers.front();
    const auto bytes=neotlk::encodeTextBytes(value,neotlk::detectClassicPreferredEncoding(tlk_.languageId(),false,{}));
    const auto advance=[&](unsigned char c){const auto i=std::min<std::size_t>(c,f.upper.size()-1);return ((f.lower.at(i).m_x-f.upper.at(i).m_x)*f.width+f.spacing)*scale;};
    std::vector<std::string> lines(1);double width=0;
    for(unsigned char c:bytes) {
        if(c=='\n') {lines.emplace_back();width=0;continue;}
        if(width+advance(c)>rect.m_width&&!lines.back().empty()) {
            const auto at=lines.back().find_last_of(" -");std::string tail;
            if(at!=std::string::npos) {const bool space=lines.back()[at]==' ';tail=lines.back().substr(at+1);lines.back().resize(at+(space?0:1));}
            lines.push_back(std::move(tail));width=0;for(unsigned char previous:lines.back())width+=advance(previous);
            if(c==' '&&lines.back().empty())continue;
        }
        lines.back().push_back(char(c));width+=advance(c);
    }
    const auto tint=overrideTint.value_or(color(style));const int alignment=integer(style,"ALIGNMENT",9);const double height=f.height*scale;
    double y=rect.m_y;if(alignment&16)y+=(rect.m_height-height*lines.size())/2;else if(alignment&32)y+=rect.m_height-height*lines.size();
    dc.PushState();dc.Clip(rect.m_x,rect.m_y,rect.m_width,rect.m_height);
    for(const auto& line:lines) {
        double w=0;for(unsigned char c:line)w+=advance(c);double x=rect.m_x;if(alignment&2)x+=(rect.m_width-w)/2;else if(alignment&4)x+=rect.m_width-w;
        for(unsigned char c:line) {
            const auto i=std::min<std::size_t>(c,f.upper.size()-1);const auto a=f.upper.at(i),b=f.lower.at(i);
            const auto key=name+" glyph "+std::to_string(i)+"/"+std::to_string(tint.x)+"/"+std::to_string(tint.y)+"/"+std::to_string(tint.z);
            auto glyph=bitmaps_.find(key);const int xx=int(std::lround(a.m_x*pixels.width)),yy=int(std::lround((1-a.m_y)*pixels.height)),ww=int(std::lround((b.m_x-a.m_x)*pixels.width)),hh=int(std::lround((a.m_y-b.m_y)*pixels.height));
            if(ww>0&&hh>0) {
                if(glyph==bitmaps_.end()) {wxImage image(ww,hh);image.InitAlpha();for(int v=0;v<hh;++v)for(int u=0;u<ww;++u) {const auto src=std::size_t((yy+v)*pixels.width+xx+u)*4,dst=std::size_t(v*ww+u);image.GetData()[dst*3]=std::uint8_t(pixels.rgba.at(src)*tint.x);image.GetData()[dst*3+1]=std::uint8_t(pixels.rgba.at(src+1)*tint.y);image.GetData()[dst*3+2]=std::uint8_t(pixels.rgba.at(src+2)*tint.z);image.GetAlpha()[dst]=pixels.rgba.at(src+3);}glyph=bitmaps_.emplace(key,wxBitmap(image)).first;}
                dc.DrawBitmap(glyph->second,x,y,(b.m_x-a.m_x)*f.width*scale,height);
            }
            x+=advance(c);
        }
        y+=height+f.lineSpacing*scale;
    }
    dc.PopState();
}
void ShopPreview::paint(wxPaintEvent&) {
    wxAutoBufferedPaintDC paint(this);paint.SetBackground(wxBrush(wxColour(14,16,20)));paint.Clear();hits_.clear();if(!loaded())return;
    try {
        std::unique_ptr<wxGraphicsContext> dc(wxGraphicsContext::Create(paint));const auto root=extent(*layout_.root());const auto size=GetClientSize();scale_=std::min(size.x/root.m_width,size.y/root.m_height);offsetX_=(size.x-root.m_width*scale_)/2;offsetY_=(size.y-root.m_height*scale_)/2;dc->Translate(offsetX_,offsetY_);dc->Scale(scale_,scale_);dc->BeginLayer(std::clamp(number(*layout_.root(),"ALPHA",1),0.0,1.0));
        if(field<GffStruct>(*layout_.root(),"BORDER"))border(*dc,*layout_.root(),"BORDER",{0,0,root.m_width,root.m_height});
        else stamp(*dc,string(*layout_.root(),"BACKGROUND"),{0,0,root.m_width,root.m_height},{1,1,1});
        const auto& controls=*field<GffList>(*layout_.root(),"CONTROLS");const Item* selected=nullptr;for(const auto& item:items_)if(int(item.source)==selection_)selected=&item;
        for(const auto& control:controls.allStructs()) {
            const auto tag=string(*control,"TAG");if(tag=="LB_INVITEMS")continue;const auto rect=extent(*control);auto contentRect=rect;
            const auto* bar=field<GffStruct>(*control,"SCROLLBAR");
            if(bar) {const auto width=extent(*bar).m_width;contentRect.m_width-=width;if(byte(*control,"LEFTSCROLLBAR",1))contentRect.m_x+=width;}
            auto inner=border(*dc,*control,"BORDER",contentRect);
            if(field<GffStruct>(*control,"HILIGHT"))inner.Intersect(borderInner(*control,"HILIGHT",contentRect));
            if(tag=="LB_SHOPITEMS") {
                const auto& proto=*field<GffStruct>(*control,"PROTOITEM");const auto& fontStyle=*field<GffStruct>(proto,"TEXT");const double unit=game_==neomdl::model::GameVersion::Kotor1?root.m_height/480:1,pad=integer(*control,"PADDING"),rowHeight=game_==neomdl::model::GameVersion::Kotor1?56*unit:36;
                const int capacity=int(inner.m_height/(rowHeight+pad));const auto count=std::count_if(items_.begin(),items_.end(),[](const auto& item){return !item.plot;});firstRow_=std::clamp(firstRow_,0,std::max(0,int(count)-capacity));
                const int slack=int(inner.m_height-pad-capacity*(rowHeight+pad)),extra=capacity?slack/capacity:0;int remainder=slack-extra*capacity;
                double y=inner.m_y+pad;int visible=0,drawn=0;dc->PushState();dc->Clip(inner.m_x,inner.m_y,inner.m_width,inner.m_height);
                for(const auto& item:items_)if(!item.plot&&visible++>=firstRow_&&drawn<capacity) {
                    double height=rowHeight+extra;if(remainder!=0) {++height;++y;--remainder;}++drawn;
                    const Rect row(inner.m_x+pad,y,inner.m_width-2*pad,height),icon(row.m_x,row.m_y,rowHeight,rowHeight),caption(row.m_x+rowHeight,row.m_y,row.m_width-rowHeight,rowHeight);
                    const bool k1=game_==neomdl::model::GameVersion::Kotor1,highlight=int(item.source)==selection_;
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
                    hits_.push_back({row,item.source});y+=height+pad;
                }
                dc->PopState();
                if(bar)scrollbar(*dc,*bar,{byte(*control,"LEFTSCROLLBAR",1)?rect.m_x:contentRect.GetRight(),rect.m_y,extent(*bar).m_width,rect.m_height},int(count),firstRow_,std::min(capacity,int(count)));
            } else if(tag=="LB_DESCRIPTION") {
                if(selected) {const auto& proto=*field<GffStruct>(*control,"PROTOITEM");const auto pad=integer(*control,"PADDING");text(*dc,*field<GffStruct>(proto,"TEXT"),selected->description,{inner.m_x+pad,inner.m_y+pad,inner.m_width-2*pad,inner.m_height-2*pad},fontScale_);}
                if(bar)scrollbar(*dc,*bar,{byte(*control,"LEFTSCROLLBAR",1)?rect.m_x:contentRect.GetRight(),rect.m_y,extent(*bar).m_width,rect.m_height},1,0,1);
            }
            else if(const auto* style=field<GffStruct>(*control,"TEXT")) {
                std::string value=string(*style,"TEXT");if(const auto* ref=field<GffUInt32Field>(*style,"STRREF");ref&&ref->value!=0xffffffffu)value=tlk_.resolve(ref->value).value();
                if(tag=="LBL_BUYSELL")value=tlk_.resolve(32132).value();
                if(tag=="BTN_Accept")value=tlk_.resolve(game_==neomdl::model::GameVersion::Kotor1?32132:125552).value();
                if(tag=="BTN_Examine")value=tlk_.resolve(41938).value();
                if(tag=="LBL_COST")value=tlk_.resolve(41943).value();
                if(tag=="LBL_STOCK_VALUE")value=selected?(selected->infinite?tlk_.resolve(41951).value():std::to_string(selected->stack)):"";
                if(tag=="LBL_COST_VALUE"||tag=="LBL_ONHAND_VALUE"||tag=="LBL_CREDITS_VALUE")value="";
                text(*dc,*style,value,inner,fontScale_);
            }
        }
        dc->EndLayer();
    } catch(const std::exception& e) {clear();wxui::showError(this,e);}
}
std::string ShopPreview::sources() const {std::string report;for(const auto& source:sources_)report+=source+"\n";report+="Talk table: "+neoshared::pathToUtf8(tlk_.filename())+"\n";if(textures_) {for(const auto& path:textures_->openPaths())report+="Archive/file: "+neoshared::pathToUtf8(path)+"\n";for(const auto& name:textures_->textureLoadOrder())report+=name+" — game texture\n";}return report;}

}
