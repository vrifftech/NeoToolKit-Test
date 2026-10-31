#pragma once
#include "core/AreaLoadTables.hpp"
#include <gff/GFFFile.hpp>
#include <neotlk/TlkLookup.hpp>
#include <array>

namespace neotoolkit {
struct ItemProperty {
    std::array<unsigned,8> fields{};
    bool active{},enabled{};
    unsigned type() const {return fields[0];}
    unsigned subtype() const {return fields[1];}
    unsigned value() const {return fields[3];}
};
struct ItemData {
    std::string name,description,icon,model,groundModel,texture,tag;
    std::uint32_t upgrades{},nameRef{0xffffffffu},cost{};
    std::map<int,std::string> names;
    std::vector<ItemProperty> properties;
    int baseItem{30};
    unsigned stack{1},maximumStack{},group{},modelType{},modelVariation{1},bodyVariation{},textureVariation{},charges{50},plot{},stolen{};
};
class ItemReader {
public:
    ItemReader(neomdl::ResourceResolver resolver,const neotlk::TlkLookup& tlk,bool k2)
        :tables_{std::move(resolver),{}},tlk_(tlk),k2_(k2) {}
    ItemData read(const neogff::GffStruct&);
private:
    int integer(const std::string& file,int row,const std::string& column,int initial=0);
    std::string caption(const std::string& file,int row,const std::string& column="Name",int minimum=0);
    std::string localized(const neogff::GffStruct&,const std::string&);
    std::string propertyText(const ItemProperty&);
    std::pair<int,int> damageRange(unsigned);
    std::string details(const ItemData&);
    neomdl::detail::AreaLoadTables tables_;
    const neotlk::TlkLookup& tlk_;
    bool k2_{};
};
bool stackCompatible(const ItemData&,const ItemData&);
}
