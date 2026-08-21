#include "engine/file/file_manager.h"

#include <array>
#include <fstream>
#include <utility>
#include <vector>

#include "common/bytes.h"
#include "engine/canvas/canvas.h"
#include "engine/persistence/persistenceWriter.h"
#include "engine/ui/file_dialog.h"
#include "engine/ui/ui.h"

file_manager::file_manager(canvas& surface, std::function<void(std::string)> set_status)
    : surface_(surface), set_status_(std::move(set_status))
{
}

void file_manager::open_and_load()
{
    const auto path = ui::open_binary_file_dialog();
    if (!path.has_value())
        return;

    std::string load_status;
    uint32_t saved_seq = 0;
    if (!ui::load_binary_replay(path.value(), surface_, load_status, saved_seq))
    {
        set_status_(std::move(load_status));
        return;
    }

    current_file_ = path->string();
    file_saved_seq_ = saved_seq;
    set_status_(std::move(load_status));

    close_auto_save();
    if (auto_save_on_)
        operation_log_ = std::make_unique<persistence_writer>(path->string(), file_saved_seq_);
}

void file_manager::save_to_file(const std::filesystem::path& path)
{
    const bool was_auto_save = auto_save_on_;
    if (was_auto_save)
        close_auto_save();

    std::ofstream file(path, std::ios::binary);
    if (!file.is_open())
    {
        set_status_("Failed to save file");
        if (was_auto_save)
            operation_log_ = std::make_unique<persistence_writer>(path.string(), file_saved_seq_);
        return;
    }

    const std::vector<draw_operation> ops = surface_.snapshot();
    uint32_t max_seq = 0;
    for (const auto& op : ops)
    {
        if (op.seq > max_seq)
            max_seq = op.seq;
    }

    constexpr std::array<uint8_t, 4> MAGIC{'S', 'K', 'S', 'Y'};
    file.write(reinterpret_cast<const char*>(MAGIC.data()), MAGIC.size());
    constexpr std::array<uint8_t, 4> VERSION{0, 0, 0, 1};
    file.write(reinterpret_cast<const char*>(VERSION.data()), VERSION.size());

    std::vector<uint8_t> seq_buf(4);
    size_t off = 0;
    bytes::write32(seq_buf, off, max_seq);
    file.write(reinterpret_cast<const char*>(seq_buf.data()), seq_buf.size());

    constexpr std::array<uint8_t, 4> RESERVED{0, 0, 0, 0};
    file.write(reinterpret_cast<const char*>(RESERVED.data()), RESERVED.size());

    for (const auto& op : ops)
    {
        const auto payload = serializeDrawOperation(op);
        std::vector<uint8_t> length_prefix(4);
        size_t off_op = 0;
        bytes::write32(length_prefix, off_op, static_cast<uint32_t>(payload.size()));
        file.write(reinterpret_cast<const char*>(length_prefix.data()), static_cast<std::streamsize>(length_prefix.size()));
        file.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
    }

    file.flush();
    file.close();

    file_saved_seq_ = max_seq;
    current_file_ = path.string();
    set_status_("Saved canvas state");

    if (was_auto_save)
        operation_log_ = std::make_unique<persistence_writer>(path.string(), file_saved_seq_);
}

void file_manager::save_as()
{
    std::string default_name = "untitled.sketchsync";
    if (current_file_ != "untitled")
        default_name = std::filesystem::path(current_file_).filename().string();
    if (const auto path = ui::save_binary_file_dialog(default_name); path.has_value())
        save_to_file(path.value());
}

void file_manager::toggle_auto_save()
{
    if (auto_save_on_)
    {
        auto_save_on_ = false;
        close_auto_save();
        set_status_("Auto-Save disabled");
        return;
    }

    if (current_file_ == "untitled")
    {
        set_status_("Save the file first to enable Auto-Save");
        save_as();
        if (current_file_ == "untitled")
            return;
    }

    auto_save_on_ = true;
    file_saved_seq_ = ui::read_saved_seq(current_file_);
    operation_log_ = std::make_unique<persistence_writer>(current_file_, file_saved_seq_);

    for (const std::vector<draw_operation> ops = surface_.snapshot(); const auto& op : ops)
    {
        if (op.seq > file_saved_seq_)
            operation_log_->enqueue(op);
    }
    set_status_("Auto-Save enabled");
}

void file_manager::enqueue_if_auto_save(const draw_operation& op) const
{
    if (operation_log_)
        operation_log_->enqueue(op);
}

void file_manager::close_auto_save()
{
    if (!operation_log_)
        return;
    operation_log_->stop();
    operation_log_.reset();
}

void file_manager::shutdown()
{
    close_auto_save();
}

void file_manager::mark_synced_for_save()
{
    if (auto_save_on_ && current_file_ != "untitled")
        canvas_synced_pending_.store(true);
}

bool file_manager::consume_synced_save()
{
    return canvas_synced_pending_.exchange(false);
}
