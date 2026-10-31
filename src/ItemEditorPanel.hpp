#pragma once
#include "NeoModulePanel.hpp"
namespace neotoolkit {
class ItemEditorPanel : public neomodules::Panel {
public:
    using Panel::Panel;
    virtual bool openFile(const std::filesystem::path&)=0;
    virtual bool openResource(neoshared::ResourceDocument)=0;
};
ItemEditorPanel* createItemEditorPanel(wxWindow*,neomodules::Context);
}
