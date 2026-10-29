#pragma once

#include "core/ModelDocument.hpp"
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

class wxWindow;

namespace neotoolkit {

struct ModulePackageSources {
    std::vector<std::filesystem::path> archives,extraFiles;
    bool collectDependencies{true};
};
struct ModulePackageInput {
    neoshared::ResourceDocument source;
    neomdl::ResourceResolver dependencies;
};
struct ModulePackage {
    std::map<std::pair<std::string,std::uint16_t>,neoshared::ResourceDocument> resources;
    std::string report;
};

std::optional<ModulePackageSources> chooseModulePackageSources(wxWindow* parent,
    std::vector<std::filesystem::path> archives,const std::filesystem::path& looseDirectory,bool dark);
ModulePackage collectModulePackage(const ModulePackageSources& sources,const std::filesystem::path& looseDirectory,
    std::vector<ModulePackageInput> edits,const std::vector<ModulePackageInput>& inlineSources,const neomdl::ResourceResolver& resolver,
    const std::function<void(const std::string&)>& progress);

}
