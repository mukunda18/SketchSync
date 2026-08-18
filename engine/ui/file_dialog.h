#ifndef SKETCHSYNC_FILE_DIALOG_H
#define SKETCHSYNC_FILE_DIALOG_H

#include <filesystem>
#include <optional>
#include <string>

namespace ui
{
    std::optional<std::filesystem::path> open_binary_file_dialog();
    std::optional<std::filesystem::path> save_binary_file_dialog(const std::string& default_name);
}

#endif
