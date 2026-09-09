#include "WorkspaceFrame.hpp"
#include <wx/app.h>

class NeoToolKitApp final : public wxApp {
public:
    bool OnInit() override {
        SetAppName("NeoToolKit-Test");
        SetVendorName("Neo Tools");
        wxInitAllImageHandlers();
        auto* frame = new neotoolkit::WorkspaceFrame;
        frame->Show();
        SetTopWindow(frame);
        for (int i = 1; i < argc; ++i) {
            const auto path = neosettings::pathFromWx(wxString(argv[i]));
            frame->CallAfter([frame, path] { frame->openPath(path); });
        }
        return true;
    }
};
wxIMPLEMENT_APP(NeoToolKitApp);
