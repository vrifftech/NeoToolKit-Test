#include "WorkspaceFrame.hpp"
#include "neotoolkit_icon.xpm"
#include <wx/app.h>
#include <wx/icon.h>

class NeoToolKitApp final : public wxApp {
public:
    bool OnInit() override {
        SetAppName("NeoToolKit-Test");
        SetVendorName("Neo Tools");
        wxInitAllImageHandlers();
        auto* frame = new neotoolkit::WorkspaceFrame;
        frame->SetIcon(wxIcon(neotoolkit_icon_xpm));
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
