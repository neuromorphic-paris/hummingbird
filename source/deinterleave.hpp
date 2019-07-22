#pragma once

#include <bitset>
#include <cstdlib>
#include <istream>
#include <ostream>
#include <vector>

/// hummingbird bundles tools to create and play high framerate videos.
namespace hummingbird {
    /// deinterleave converts a 1440 fps raw stream to a 60 fps YUV420 stream.
    inline void deinterleave(std::istream& input, std::ostream& output, bool bit_input) {
        std::vector<uint8_t> frame(608 * 684 * 3, 0);
        uint8_t frame_index = 0;
        uint8_t mask = 1;
        output << "YUV4MPEG2 W1216 H684 F60:1 Ip C420\n";
        if (bit_input) {
            for (;;) {
                std::vector<uint8_t> bytes(608 * 684 / 8);
                std::size_t byte_index = 0;
                input.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
                if (input.eof()) {
                    break;
                }
                {
                    std::size_t pixel_index = 608 * 684 * 2;
                    for (std::size_t y = 0; y < 684 / 2; ++y) {
                        for (std::size_t x = 0; x < 608; x += 8) {
                            std::bitset<8> bits(bytes[byte_index]);
                            ++byte_index;
                            for (uint8_t bit_index = 0; bit_index < bits.size(); ++bit_index) {
                                if (bits[bit_index]) {
                                    frame[pixel_index] |= mask;
                                } else {
                                    frame[pixel_index] &= (~mask);
                                }
                                ++pixel_index;
                            }
                        }
                        pixel_index += 608;
                    }
                }
                {
                    std::size_t pixel_index = 608 * 684 * 2 + 608;
                    for (std::size_t y = 0; y < 684 / 2; ++y) {
                        for (std::size_t x = 0; x < 608; x += 8) {
                            std::bitset<8> bits(bytes[byte_index]);
                            ++byte_index;
                            for (uint8_t bit_index = 0; bit_index < bits.size(); ++bit_index) {
                                if (bits[bit_index]) {
                                    frame[pixel_index] |= mask;
                                } else {
                                    frame[pixel_index] &= (~mask);
                                }
                                ++pixel_index;
                            }
                        }
                        pixel_index += 608;
                    }
                }
                byte_index = 0;
                input.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
                if (input.eof()) {
                    break;
                }
                {
                    std::size_t pixel_index = 0;
                    for (std::size_t y = 0; y < 684; ++y) {
                        for (std::size_t x = 0; x < 608; x += 8) {
                            std::bitset<8> bits(bytes[byte_index]);
                            ++byte_index;
                            for (uint8_t bit_index = 0; bit_index < bits.size(); ++bit_index) {
                                if (bits[bit_index]) {
                                    frame[pixel_index] |= mask;
                                } else {
                                    frame[pixel_index] &= (~mask);
                                }
                                pixel_index += 2;
                            }
                        }
                    }
                }
                byte_index = 0;
                input.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
                if (input.eof()) {
                    break;
                }
                {
                    std::size_t pixel_index = 1;
                    for (std::size_t y = 0; y < 684; ++y) {
                        for (std::size_t x = 0; x < 608; x += 8) {
                            std::bitset<8> bits(bytes[byte_index]);
                            ++byte_index;
                            for (uint8_t bit_index = 0; bit_index < bits.size(); ++bit_index) {
                                if (bits[bit_index]) {
                                    frame[pixel_index] |= mask;
                                } else {
                                    frame[pixel_index] &= (~mask);
                                }
                                pixel_index += 2;
                            }
                        }
                    }
                }
                if (frame_index == 21) {
                    output << "FRAME\n";
                    output.write(reinterpret_cast<const char*>(frame.data()), frame.size());
                    frame_index = 0;
                } else {
                    frame_index += 3;
                }
                mask = (1 << (frame_index % 8));
            }
        } else {
            for (;;) {
                std::vector<uint8_t> bytes(608 * 684);
                std::size_t byte_index = 0;
                input.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
                if (input.eof()) {
                    break;
                }
                {
                    std::size_t pixel_index = 608 * 684 * 2;
                    for (std::size_t y = 0; y < 684 / 2; ++y) {
                        for (std::size_t x = 0; x < 608; ++x) {
                            if (bytes[byte_index] > 127) {
                                frame[pixel_index] |= mask;
                            } else {
                                frame[pixel_index] &= (~mask);
                            }
                            ++byte_index;
                            ++pixel_index;
                        }
                        pixel_index += 608;
                    }
                }
                {
                    std::size_t pixel_index = 608 * 684 * 2 + 608;
                    for (std::size_t y = 0; y < 684 / 2; ++y) {
                        for (std::size_t x = 0; x < 608; ++x) {
                            if (bytes[byte_index] > 127) {
                                frame[pixel_index] |= mask;
                            } else {
                                frame[pixel_index] &= (~mask);
                            }
                            ++byte_index;
                            ++pixel_index;
                        }
                        pixel_index += 608;
                    }
                }
                byte_index = 0;
                input.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
                if (input.eof()) {
                    break;
                }
                {
                    std::size_t pixel_index = 0;
                    for (std::size_t y = 0; y < 684; ++y) {
                        for (std::size_t x = 0; x < 608; ++x) {
                            if (bytes[byte_index] > 127) {
                                frame[pixel_index] |= mask;
                            } else {
                                frame[pixel_index] &= (~mask);
                            }
                            ++byte_index;
                            pixel_index += 2;
                        }
                    }
                }
                byte_index = 0;
                input.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
                if (input.eof()) {
                    break;
                }
                {
                    std::size_t pixel_index = 1;
                    for (std::size_t y = 0; y < 684; ++y) {
                        for (std::size_t x = 0; x < 608; ++x) {
                            if (bytes[byte_index] > 127) {
                                frame[pixel_index] |= mask;
                            } else {
                                frame[pixel_index] &= (~mask);
                            }
                            ++byte_index;
                            pixel_index += 2;
                        }
                    }
                }
                if (frame_index == 21) {
                    output << "FRAME\n";
                    output.write(reinterpret_cast<const char*>(frame.data()), frame.size());
                    frame_index = 0;
                } else {
                    frame_index += 3;
                }
                mask = (1 << (frame_index % 8));
            }
        }
    }
}
