#pragma once

#include <gff/GFFFile.hpp>
#include <wx/panel.h>
#include <functional>
#include <string>
#include <vector>

class wxChoice;
class wxSlider;
class wxTextCtrl;

namespace neotoolkit {

class AreaControls final : public wxPanel {
public:
    AreaControls(wxWindow* parent,
        std::function<void(const std::string&,const std::string&,const std::string&)> edit,
        std::function<void()> preview);
    void load(const neogff::GffStruct* root,bool k2);
    void enablePreview(bool enabled);
    int previewPhase() const;
private:
    enum class Kind {Color,Number,Flag,Wind,Text,WeatherOff};
    struct Row {
        Kind kind;
        std::string field,label;
        float step{},maximum{};
        wxWindow* control{};
        wxSlider* slider{};
        wxTextCtrl* text{};
    };
    void apply(std::size_t index,std::string value);
    void sliderChanged(std::size_t index,bool commit);
    std::vector<Row> rows_;
    const neogff::GffStruct* root_{};
    wxChoice* phase_{};
    std::function<void(const std::string&,const std::string&,const std::string&)> edit_;
};

}
