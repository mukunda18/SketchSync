#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

// NOGDI is useful while including raylib, but commdlg.h requires the GDI
// declarations that NOGDI suppresses.
#ifdef NOGDI
#undef NOGDI
#endif

#include "engine/ui/file_dialog.h"
#include <cstring>

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
        ofn.lpstrFilter = "SketchSync files (*.sketchsync)\0*.sketchsync\0All files\0*.*\0";
        ofn.nFilterIndex = 1;
        ofn.lpstrDefExt = "sketchsync";
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;

        if (GetOpenFileNameA(&ofn) != 0)
            return std::filesystem::path(file_name);
#endif
        return std::nullopt;
    }

    std::optional<std::filesystem::path> save_binary_file_dialog(const std::string& default_name)
    {
#ifdef _WIN32
        char file_name[MAX_PATH] = {};
        std::strncpy(file_name, default_name.c_str(), sizeof(file_name) - 1);
        OPENFILENAMEA ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.lpstrFile = file_name;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrFilter = "SketchSync files (*.sketchsync)\0*.sketchsync\0All files\0*.*\0";
        ofn.nFilterIndex = 1;
        ofn.lpstrDefExt = "sketchsync";
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY;

        if (GetSaveFileNameA(&ofn) != 0)
            return std::filesystem::path(file_name);
#endif
        return std::nullopt;
    }
}
