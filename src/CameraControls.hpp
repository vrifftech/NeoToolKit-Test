#pragma once

#include "core/AreaVisualRecords.hpp"
#include <gff/GFFFile.hpp>
#include <wx/scrolwin.h>
#include <array>
#include <functional>
#include <optional>

class wxButton;
class wxCheckBox;
class wxChoice;
class wxSlider;
class wxTextCtrl;

namespace neotoolkit {

class CameraControls final : public wxScrolledWindow {
public:
    struct Actions {
        std::function<void(float,float)> direction;
        std::function<void(std::size_t)> select;
        std::function<void(std::optional<neomdl::AreaCameraRecord>)> preview;
        std::function<void(const neomdl::AreaCameraRecord&)> edit;
    };
    CameraControls(wxWindow* parent,Actions actions);
    void load(const neogff::GffStruct* root,bool k2);
    void select(std::size_t sourceIndex);
    void enablePreview(bool enabled);
    void stopPreview();
    bool previewing() const;
    const std::vector<neomdl::AreaCameraRecord>& records() const {return cameras_;}
    neomdl::AreaCameraRecord pendingCamera() const;
private:
    void showCamera();
    void preview();
    Actions actions_;
    std::vector<neomdl::AreaCameraRecord> cameras_;
    wxChoice* camera_{};
    wxCheckBox* through_{};
    wxSlider* fov_{};
    std::array<wxTextCtrl*,7> fields_{};
    wxButton* apply_{};
    bool previewEnabled_{};
};

}
