#pragma once

#include <cstdint>
#include <vector>

/// hummingbird bundles tools to create and play high framerate videos.
namespace hummingbird {
    /// rotate converts a 343 x 342 RGB frame to a 608 x 684 RGB frame.
    inline void rotate(const std::vector<uint8_t>& input, std::vector<uint8_t>& output) {
        if (input.size() != 343 * 342 * 3) {
            throw std::logic_error("unexpected rotate input size");
        }
        output.resize(608 * 684 * 3, 0);
        for (uint16_t y = 0; y < 342; ++y) {
            for (uint16_t x = 0; x < 343; ++x) {
                for (uint8_t channel = 0; channel < 3; ++channel) {
                    output[(133 + (x + y) / 2 + (342 - x + y) * 608) * 3 + channel] =
                        input[(x + y * 343) * 3 + channel];
                }
            }
        }
    }
}
