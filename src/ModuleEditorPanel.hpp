#pragma once

#include "NeoModulePanel.hpp"
#include <neoshared/ResourceDocument.hpp>

namespace neotoolkit {

class ModuleEditorPanel : public neomodules::Panel {
public:
    using Panel::Panel;
    virtual bool openFile(const std::filesystem::path& path,bool installed=false)=0;
    virtual bool openResource(neoshared::ResourceDocument resource)=0;
};

ModuleEditorPanel* createModuleEditorPanel(wxWindow* parent,neomodules::Context context);

}
