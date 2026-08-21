#ifndef SKETCHSYNC_FILE_MANAGER_H
#define SKETCHSYNC_FILE_MANAGER_H

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

#include "common/canvas/draw_operation.h"
#include "engine/persistence/persistenceWriter.h"

struct canvas;

struct file_manager
{
    file_manager(canvas& surface, std::function<void(std::string)> set_status);

    file_manager(const file_manager&) = delete;
    file_manager& operator=(const file_manager&) = delete;

    void open_and_load();
    void save_to_file(const std::filesystem::path& path);
    void save_as();
    void toggle_auto_save();
    void enqueue_if_auto_save(const draw_operation& op) const;
    void close_auto_save();
    void shutdown();
    void mark_synced_for_save();
    [[nodiscard]] bool consume_synced_save();

    [[nodiscard]] const std::string& current_file() const { return current_file_; }
    [[nodiscard]] bool is_untitled() const { return current_file_ == "untitled"; }
    [[nodiscard]] bool auto_save_on() const { return auto_save_on_; }

private:
    canvas& surface_;
    std::function<void(std::string)> set_status_;
    std::string current_file_ = "untitled";
    bool auto_save_on_ = false;
    uint32_t file_saved_seq_ = 0;
    std::unique_ptr<persistence_writer> operation_log_;
    std::atomic<bool> canvas_synced_pending_{false};
};

#endif
