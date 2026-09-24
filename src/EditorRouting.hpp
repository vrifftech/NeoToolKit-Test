#pragma once
#include <core/KeyBifArchive.hpp>
#include <gff/AppModel.hpp>
#include <core/ArchiveDocument.hpp>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>
namespace neotoolkit {
enum class EditorKind : std::size_t { Tables, Soundsets, TalkTables, Gff, Dialogues, Journals, Textures, Archives };
inline constexpr std::size_t kEditorCount=8;
struct EditorChoice { EditorKind kind; std::string id; std::string label; };
inline std::vector<EditorChoice> editorsForExtension(std::string ext) {
    std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    if(!ext.empty() && ext.front()!='.')ext='.'+ext;
    if(neoerf::isArchiveExtension(ext))return {{EditorKind::Archives,"neoerf","Open with NeoERF"}};
    if(ext==".2da"||ext==".gda")return {{EditorKind::Tables,"neo2da","Open with Neo2DA"}};
    if(ext==".ssf")return {{EditorKind::Soundsets,"neossf","Open with NeoSSF"}};
    if(ext==".tlk")return {{EditorKind::TalkTables,"neotlk","Open with NeoTLK"}};
    if(ext==".tpc"||ext==".txb"||ext==".tga"||ext==".dds"||ext==".png"||
       ext==".jpg"||ext==".jpeg"||ext==".jpe"||ext==".bmp"||ext==".txi")
        return {{EditorKind::Textures,"neotpc","Open with NeoTPC"}};
    if(ext==".dlg")return {{EditorKind::Dialogues,"neodlg","Open with NeoDLG"},
                           {EditorKind::Gff,"neogff","Open with NeoGFF"}};
    if(ext==".jrl")return {{EditorKind::Journals,"neojrl","Open with NeoJRL"},
                           {EditorKind::Gff,"neogff","Open with NeoGFF"}};
    if(neogff::isKnownGffResourceExtension(ext))return {{EditorKind::Gff,"neogff","Open with NeoGFF"}};
    return {};
}
inline std::vector<EditorChoice> editorsForType(std::uint16_t type) {
    return editorsForExtension(neobif::resourceTypeExtension(type));
}
inline EditorKind selectResourceEditor(const std::vector<EditorChoice>& choices,const std::string& requested={}) {
    if(choices.empty())throw std::runtime_error("No integrated editor for this resource type.");
    if(requested.empty())return choices.front().kind;
    for(const auto& choice:choices)if(choice.id==requested)return choice.kind;
    throw std::runtime_error("The requested editor cannot open this resource type.");
}
} // namespace neotoolkit
