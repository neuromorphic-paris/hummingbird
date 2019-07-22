#pragma once

#include <cstdint>
#include <gstreamermm/buffer.h>
#include <vector>

/// hummingbird bundles tools to create and play high framerate videos.
namespace hummingbird {
    /// interleave converts a decoded YUV420 buffer to RGB bytes.
    inline void interleave(const Glib::RefPtr<Gst::Buffer>& buffer, std::vector<uint8_t>& bytes) {
        if (buffer->get_size() != 608 * 684 * 3) {
            throw std::logic_error("unexpected buffer size");
        }
        bytes.resize(buffer->get_size());
        auto memory = buffer->peek_memory(0);
        Gst::MapInfo info;
        if (!gst_memory_map(memory->gobj(), info.gobj(), GST_MAP_READ)) {
            throw std::logic_error("mapping the buffer memory failed");
        }
        uint16_t* rgs = reinterpret_cast<uint16_t*>(info.get_data());
        uint8_t* active_b = reinterpret_cast<uint8_t*>(rgs) + ((608 * 2) * 684);
        uint8_t* idle_b = reinterpret_cast<uint8_t*>(active_b) + ((608 * 2) * 684 / 4);
        uint8_t* rgbs = bytes.data();
        for (std::size_t y = 0; y < 684; ++y) {
            for (std::size_t x = 0; x < 608; ++x) {
                *reinterpret_cast<uint16_t*>(rgbs) = *rgs;
                ++rgs;
                rgbs += 2;
                *rgbs = *active_b;
                ++active_b;
                ++rgbs;
            }
            std::swap(active_b, idle_b);
        }
        gst_memory_unmap(memory->gobj(), info.gobj());
    }
}
