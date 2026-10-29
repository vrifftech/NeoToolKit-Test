#pragma once

#include <gff/GFFFile.hpp>
#include <wx/panel.h>
#include <functional>
#include <string>
#include <vector>

class wxChoice;
class wxComboBox;
class wxListBox;
class wxTextCtrl;
class wxButton;

namespace neotoolkit {

class ModuleControls final : public wxPanel {
public:
    enum class AreaAction {Add,Rename,Remove,Up,Down};
    using Edit=std::function<void(const std::string&,const std::string&,const std::string&,const std::string&)>;
    ModuleControls(wxWindow* parent,Edit edit,
        std::function<void(AreaAction,std::size_t,const std::string&)> areaEdit);
    void load(const neogff::GffStruct* root,const std::vector<std::string>& availableAreas);
private:
    struct Row {std::string field,type,label;wxTextCtrl* text;};
    struct LocalizedRow {std::string field,label;wxTextCtrl* reference;wxChoice* language;wxTextCtrl* text;std::vector<int> ids;};
    void apply(const Row& row);
    void showLocalized(std::size_t index);
    void updateAreaActions();
    std::vector<Row> rows_;
    std::vector<LocalizedRow> localized_;
    const neogff::GffStruct* root_{};
    Edit edit_;
    wxListBox* areas_{};
    wxComboBox* areaName_{};
    std::vector<wxButton*> areaButtons_;
};

}
