#include "ItemData.hpp"
#include <gff/GffText.hpp>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <tuple>

namespace neotoolkit {
namespace {
using namespace neogff;
template<class T> const T* field(const GffStruct& root,const std::string& name) {return dynamic_cast<const T*>(root.GetFieldByLabel(name));}
template<class T> unsigned value(const GffStruct& root,const std::string& name,unsigned initial=0) {const auto* f=field<T>(root,name);return f?f->value:initial;}
std::string numbered(const std::string& prefix,unsigned n,int digits) {std::ostringstream s;s<<prefix<<std::setfill('0')<<std::setw(digits)<<n;return neomdl::resourceKey(s.str().substr(0,16));}
}
int ItemReader::integer(const std::string& file,int row,const std::string& column,int initial) {neomdl::engine2da::getInt(tables_.table(file),row,column,initial);return initial;}
std::string ItemReader::caption(const std::string& file,int row,const std::string& column,int minimum) {int ref=-1;const bool present=neomdl::engine2da::getInt(tables_.table(file),row,column,ref);return present&&ref>=minimum?tlk_.resolveKotor(unsigned(ref)).value():std::string{};}
std::string ItemReader::localized(const GffStruct& root,const std::string& name) {
    const auto* f=field<GffLocalizedStringField>(root,name);std::string text;bool embedded=false;
    if(f) {for(const auto& s:f->substrings)if(unsigned(s.stringid)==tlk_.languageId()*2) {text=neotlk::decodeTextBytes(s.GetString(),tlk_.textEncoding());embedded=true;}if(!embedded&&f->strref!=0xffffffffu)text=tlk_.resolveKotor(f->strref).value();}
    return text;
}
ItemData ItemReader::read(const GffStruct& root) {
    ItemData item;item.baseItem=int(value<GffIntField>(root,"BaseItem",30));
    item.name=localized(root,"LocalizedName");
    if(const auto* f=field<GffLocalizedStringField>(root,"LocalizedName")) {item.nameRef=f->strref;for(const auto& s:f->substrings)item.names[s.stringid]=s.GetString();}
    if(const auto* f=field<GffExoStringField>(root,"Tag"))item.tag=f->GetString();
    item.modelType=std::uint8_t(integer("baseitems",item.baseItem,"ModelType"));
    if(const auto* f=field<GffByteField>(root,"ModelVariation"))item.modelVariation=f->value;
    else {item.modelVariation=value<GffByteField>(root,"ModelPart1",1);if(item.modelVariation==0)item.modelVariation=1;}
    if(item.modelType==1) {
        item.bodyVariation=1;const auto body=tables_.stringValue("baseitems",item.baseItem,"BodyVar");
        if(body.size()==1&&body[0]>='A'&&body[0]<='Z')item.bodyVariation=unsigned(body[0]-'@');
        item.textureVariation=value<GffByteField>(root,"TextureVar",1);
    }
    item.upgrades=value<GffUInt32Field>(root,"Upgrades");item.charges=value<GffByteField>(root,"Charges",50);
    item.stack=value<GffWordField>(root,"StackSize",1);item.plot=value<GffByteField>(root,"Plot");item.stolen=value<GffByteField>(root,"Stolen")&1u;
    item.cost=item.plot==1?0:std::max(1u,std::uint32_t(float(value<GffUInt32Field>(root,"AddCost"))));
    item.maximumStack=std::uint16_t(integer("baseitems",item.baseItem,"Stacking"));item.group=std::uint8_t(integer("baseitems",item.baseItem,"StorePanelSort"));
    auto stem=tables_.stringValue("baseitems",item.baseItem,"ItemClass");stem.resize(std::min<std::size_t>(11,stem.size()));
    if(std::uint8_t(integer("baseitems",item.baseItem,"GenderSpecific")))stem+="_m";
    item.icon=numbered("i"+stem+"_",item.modelType?item.textureVariation:item.modelVariation,3);
    if(item.modelType==1) {
        const char body=char('A'+std::max(1u,std::min(item.bodyVariation,k2_?14u:10u))-1);
        item.model=neomdl::resourceKey(tables_.stringValue("appearance",137,std::string("model")+body));
        item.texture=numbered(tables_.stringValue("appearance",137,std::string("tex")+body),std::max(1u,item.textureVariation),2);
    } else item.model=numbered(stem+"_",item.modelVariation,3);
    item.groundModel=neomdl::resourceKey(tables_.stringValue("baseitems",item.baseItem,"DefaultModel"));
    if(const auto* list=field<GffList>(root,"PropertiesList"))for(const auto& record:list->allStructs()) {
        if(record->allFields().empty())continue;
        ItemProperty p;p.fields={value<GffWordField>(*record,"PropertyName"),value<GffWordField>(*record,"Subtype"),value<GffByteField>(*record,"CostTable"),value<GffWordField>(*record,"CostValue"),value<GffByteField>(*record,"Param1"),value<GffByteField>(*record,"Param1Value"),value<GffByteField>(*record,"UsesPerDay",255),value<GffByteField>(*record,"UpgradeType",255)};
        p.active=p.type()==10||p.type()==37||p.type()==46||p.type()==53;
        if(p.type()==10&&p.fields[6]==255&&p.value()>=8&&p.value()<=12)p.fields[6]=p.value()-7;
        p.enabled=p.fields[7]==255||(item.upgrades&(1u<<(p.fields[7]&31)));
        item.properties.push_back(p);
    }
    std::stable_partition(item.properties.begin(),item.properties.end(),[](const auto& p){return p.active;});
    auto body=localized(root,field<GffLocalizedStringField>(root,"DescIdentified")?"DescIdentified":"Description");
    if(body.empty())body=tlk_.resolveKotor(32172).value();
    item.description=details(item)+body;return item;
}
bool stackCompatible(const ItemData& a,const ItemData& b) {
    if(std::tie(a.upgrades,a.charges,a.baseItem,a.plot,a.stolen,a.modelVariation,a.bodyVariation,a.textureVariation,a.tag,a.nameRef,a.names)!=std::tie(b.upgrades,b.charges,b.baseItem,b.plot,b.stolen,b.modelVariation,b.bodyVariation,b.textureVariation,b.tag,b.nameRef,b.names)||a.properties.size()!=b.properties.size())return false;
    return std::equal(a.properties.begin(),a.properties.end(),b.properties.begin(),[](const auto& x,const auto& y){return x.active==y.active&&x.fields==y.fields;});
}
std::string ItemReader::propertyText(const ItemProperty& p) {
    const auto& definitions=tables_.table("itempropdef");std::string subTable;neomdl::engine2da::getString(definitions,int(p.type()),"SubTypeResRef",subTable);
    auto text=caption("itempropdef",int(p.type()));
    const auto append=[&](const std::string& s){if(!s.empty()) {if(!text.empty())text+=": ";text+=s;}};
    if(!subTable.empty())append(caption(neomdl::resourceKey(subTable),int(p.subtype())));
    int cost=-1;
    if(p.value()!=0xffff&&neomdl::engine2da::getInt(definitions,int(p.type()),"CostTableResRef",cost)) {const auto table=tables_.stringValue("iprp_costtable",std::uint8_t(cost),"Name");append(caption(neomdl::resourceKey(table),int(p.value()),"Name",k2_?1:0));}
    if(p.fields[5]!=255) {
        int param=-1;bool present=neomdl::engine2da::getInt(definitions,int(p.type()),"Param1ResRef",param);
        if(!present&&!subTable.empty())present=neomdl::engine2da::getInt(tables_.table(neomdl::resourceKey(subTable)),int(p.subtype()),"Param1ResRef",param);
        if(present)append(caption(neomdl::resourceKey(tables_.stringValue("iprp_paramtable",std::uint8_t(param),"TableResRef")),int(p.fields[5])));
    }
    return text;
}
std::pair<int,int> ItemReader::damageRange(unsigned value) {
    int dice=0;
    if(!neomdl::engine2da::getInt(tables_.table("iprp_damagecost"),int(value),"NumDice",dice))return {int(value),int(value)};
    if(dice==0)return {1,int(value)};
    return {dice,dice*integer("iprp_damagecost",int(value),"Die")};
}
std::string ItemReader::details(const ItemData& item) {
    std::string out;const auto label=[&](unsigned ref){return tlk_.resolveKotor(ref).value();};
    const auto base=[&](const std::string& key,int initial=0){return integer("baseitems",item.baseItem,key,initial);};
    const auto itemType=std::uint8_t(base("ItemType"));
    if(itemType==46||itemType==6||(k2_&&itemType==49))return {};
    const auto has=[&](unsigned type){return std::any_of(item.properties.begin(),item.properties.end(),[&](const auto& p){return p.enabled&&p.type()==type;});};
    const auto line=[&](unsigned ref,const std::string& text){out+=label(ref)+": "+text+"\n\n";};
    std::vector<unsigned> feats;
    for(unsigned i=0;i<5;++i) {int feat=0;if(neomdl::engine2da::getInt(tables_.table("baseitems"),item.baseItem,"ReqFeat"+std::to_string(i),feat)&&std::uint16_t(feat)!=0)feats.push_back(std::uint16_t(feat));}
    unsigned armorFeat=5;for(const auto feat:feats) {if(feat==6&&armorFeat==5)armorFeat=6;if(feat==4&&armorFeat>=5&&armorFeat<=6)armorFeat=4;}
    feats.erase(std::remove_if(feats.begin(),feats.end(),[&](auto feat){return feat>=4&&feat<=6&&feat!=armorFeat;}),feats.end());
    for(const auto& p:item.properties)if(p.enabled&&p.type()==57&&p.subtype())feats.push_back(p.subtype());
    if(!feats.empty()) {out+=label(38536)+":\n";for(const auto feat:feats) {auto name=caption("feat",int(feat),"NAME");const auto colon=name.find(':');if(colon!=std::string::npos)name.replace(colon,1," - ");out+=name+"\n";}out+='\n';}
    if(std::uint8_t(base("WeaponType"))|| (k2_&&item.baseItem==45)) {
        int low=std::uint8_t(base("NumDice")),high=low*std::uint8_t(base("DieToRoll"));const auto flags=std::uint32_t(base("DamageFlags")),damage=flags?flags:(k2_?1u:8u);
        for(const auto& p:item.properties)if(p.enabled) {
            if(p.type()==5) {low+=int(p.value());high+=int(p.value());}
            if(p.type()==15) {low-=int(p.value());high-=int(p.value());}
            if(p.type()==11&&(damage&(1u<<(p.subtype()&31)))) {const auto range=damageRange(p.value());low+=range.first;high+=range.second;}
        }
        if(k2_)low=std::max(1,low);
        std::string damageType;if(damage&7)damageType=label(38552);
        for(unsigned bit=3;bit<=12;++bit)if(damage&(1u<<bit)) {if(!damageType.empty())damageType+=", ";damageType+=label(38553+bit-3);}
        if(damage&0x2000) {if(!damageType.empty())damageType+=", ";damageType+=label(41903);}
        if(has(31))line(31385,k2_?"1":"0");
        else if(!k2_||high!=0)line(31385,(damageType.empty()?"":damageType+", ")+(k2_&&item.baseItem==45?"+"+std::to_string(high):std::to_string(low)+"-"+std::to_string(high)));
        if(std::uint8_t(base("RangedWeapon"))) {float range=0;neomdl::engine2da::getFloat(tables_.table("baseitems"),item.baseItem,"MaxAttackRange",range);line(38565,std::to_string(int(range))+"m");}
        const int threat=std::uint8_t(base("CritThreat"))*(has(28)?2:1),mult=std::uint8_t(base("CritHitMult"));
        int criticalLow=0,criticalHigh=0;
        for(const auto& p:item.properties)if(p.enabled&&p.type()==49) {
            const bool best=!k2_&&std::any_of(item.properties.begin(),item.properties.end(),[&](const auto& q){return q.enabled&&q.type()==49&&q.subtype()==p.subtype()&&q.value()>p.value();});
            if(!best) {const auto range=damageRange(p.value());criticalLow+=range.first;criticalHigh+=range.second;}
        }
        if(!k2_||item.baseItem!=45)line(38571,std::to_string(21-threat)+"-20,x"+std::to_string(mult)+(criticalLow||criticalHigh?" +"+std::to_string(criticalLow)+"-"+std::to_string(criticalHigh):""));
        if(std::uint8_t(base("WeaponSize"))==2)out+=label(42145)+"\n\n";
    }
    int attack=0,defence=std::uint8_t(base("BaseAC")),acEnchant=-1;bool decreased=false;
    if(!neomdl::engine2da::getInt(tables_.table("baseitems"),item.baseItem,"AC_Enchant",acEnchant))acEnchant=-1;
    for(const auto& p:item.properties)if(p.enabled) {
        if(p.type()==5||p.type()==38)attack+=int(p.value());
        if(p.type()==8||p.type()==41)attack-=int(p.value());
        const bool best=std::none_of(item.properties.begin(),item.properties.end(),[&](const auto& q){return q.enabled&&q.type()==p.type()&&q.subtype()==p.subtype()&&q.value()>p.value();});
        if(p.type()==1&&(acEnchant==0||best))defence+=int(p.value());
        if(p.type()==20&&!decreased) {if(best)defence-=int(p.value());decreased=true;}
    }
    if(attack)line(38566,(attack>0?"+":"")+std::to_string(attack));
    if(defence)line(38593,std::to_string(defence));
    int maxDex=255;if(!neomdl::engine2da::getInt(tables_.table("baseitems"),item.baseItem,"DEXBONUS",maxDex))maxDex=255;
    unsigned dex=std::uint8_t(maxDex);if(k2_) {int bonus=0;for(const auto& p:item.properties)if(p.enabled&&p.type()==68)bonus+=int(p.value());dex=std::uint8_t(bonus&&dex==255?bonus:int(dex)+bonus);}
    if(dex!=255&&itemType!=15)line(42144,"+"+std::to_string(dex));
    for(const auto& p:item.properties)if(p.enabled) {const auto text=propertyText(p);if(!text.empty())out+=text+"\n";}
    if(!item.properties.empty())out+='\n';
    return out;
}
}
