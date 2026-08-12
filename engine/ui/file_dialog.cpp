#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

// NOGDI is useful while including raylib, but commdlg.h requires the GDI
// declarations that NOGDI suppresses.
#ifdef NOGDI
#undef NOGDI
#endif

#include "engine/ui/file_dialog.h"

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#endif

namespace ui
{
    std::optional<std::filesystem::path> open_binary_file_dialog()
    {
#ifdef _WIN32
        char file_name[MAX_PATH] = {};
        OPENFILENAMEA ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.lpstrFile = file_name;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrFilter = "SketchSync files\0*.bin;*.sketchsync;*.dat\0All files\0*.*\0";
        ofn.nFilterIndex = 1;
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;

        if (GetOpenFileNameA(&ofn) != 0)
            return std::filesystem::path(file_name);
#endif
        return std::nullopt;
    }
}
