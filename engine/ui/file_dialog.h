#ifndef SKETCHSYNC_FILE_DIALOG_H
#define SKETCHSYNC_FILE_DIALOG_H

#include <filesystem>
#include <optional>

namespace ui
{
    std::optional<std::filesystem::path> open_binary_file_dialog();
}

#endif
