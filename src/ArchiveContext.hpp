#pragma once

#include "core/GameResourceSession.hpp"
#include <functional>
#include <map>

namespace neotoolkit {

// Captured on the UI thread; the resolver owns the archive's current bytes.
struct ArchiveContext {
    std::string identity, label;
    std::filesystem::path sourcePath;
    std::shared_ptr<const std::map<std::pair<std::string,std::uint16_t>,neoshared::ResourceDocument>> members;
    neomdl::ResourceResolver resources;
    std::shared_ptr<neomdl::GameResourceSession> game;
    std::function<void(const std::vector<neoshared::ResourceDocument>&)> update;
    std::function<void()> show;
};
using GameResources = std::function<std::shared_ptr<neomdl::GameResourceSession>(const std::string&,const std::filesystem::path&)>;

}
