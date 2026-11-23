#include "NeoUpdater.hpp"
#include "WorkspaceFrame.hpp"
#include "neotoolkit_icon.xpm"
#include <wx/app.h>
#include <wx/icon.h>

class NeoToolKitApp final : public wxApp {
public:
    bool OnInit() override {
        SetAppName("NeoToolKit-Test");
        SetVendorName("Neo Tools");
        wxui::initializeTheme("NeoToolKit-Test");
        wxInitAllImageHandlers();
        auto* frame = new neotoolkit::WorkspaceFrame;
        frame->SetIcon(wxIcon(neotoolkit_icon_xpm));
        frame->Show();
#if !defined(__EMSCRIPTEN__)
        neoupdate::install(*frame, NEO_UPDATE_APPLICATION, NEO_UPDATE_VERSION);
#endif

        SetTopWindow(frame);
        for (int i = 1; i < argc; ++i) {
            const auto path = neosettings::pathFromWx(wxString(argv[i]));
            frame->CallAfter([frame, path] { frame->openPath(path); });
        }
        return true;
    }
};
wxIMPLEMENT_APP(NeoToolKitApp);
