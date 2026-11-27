#pragma once
#include "NeoModulePanel.hpp"
#include "ArchiveContext.hpp"

namespace neotoolkit {
class StoreEditorPanel : public neomodules::Panel {
public:
    using Panel::Panel;
    virtual bool openFile(const std::filesystem::path& path)=0;
    virtual bool openResource(neoshared::ResourceDocument source,std::shared_ptr<ArchiveContext> archive={})=0;
};
StoreEditorPanel* createStoreEditorPanel(wxWindow* parent,neomodules::Context context,GameResources games,
    std::function<void(neoshared::ResourceDocument,std::shared_ptr<ArchiveContext>)> open);
}
