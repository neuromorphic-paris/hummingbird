#include "../third_party/pontella/source/pontella.hpp"
#include "decoder.hpp"
#include "display.hpp"
#include "interleave.hpp"
#include "lightcrafter.hpp"
#include <array>
#include <fstream>
#include <iostream>
#include <thread>

int main(int argc, char* argv[]) {
    return pontella::main(
        {
            "play reads one or several video files and displays them with a "
            "LightCrafter",
            "Syntax: ./play [options] /path/to/first/video.mp4 "
            "[/path/to/second/video.mp4...]",
            "Available options:",
            "    -l, --loop                        plays the files in a loop",
            "    -w, --windowed                    uses a window instead of "
            "going fullscreen",
            "                                          if this flag is not used, "
            "a LightCrafter is required",
            "    -p [index], --prefer [index]      if several connected screens "
            "have",
            "                                          the expected resolution "
            "(608 x 684),",
            "                                          or if the flag 'window' "
            "is used,",
            "                                          uses the one at 'index'",
            "                                          defaults to 0",
            "    -b [frames], --buffer [frames]    sets the number of frames "
            "buffered",
            "                                          defaults to 64",
            "                                          the smaller the buffer, "
            "the smaller the delay between videos",
            "                                          however, small buffers "
            "increase the risk",
            "                                          to miss frames",
            "    -i [ip], --ip [ip]                sets the LightCrafter IP "
            "address",
            "                                          defaults to 10.10.10.100",
            "                                          ignored in windowed mode",
            "    -h, --help                        shows this help message",
        },
        argc,
        argv,
        -1,
        {{"prefer", {"p"}}, {"buffer", {"b"}}, {"ip", {"i"}}},
        {{"loop", {"l"}}, {"windowed", {"w"}}},
        [](pontella::command command) {
            if (command.arguments.empty()) {
                throw std::runtime_error("at least one video path is required");
            }
            for (const auto& filename : command.arguments) {
                std::ifstream input(filename);
                if (!input.good()) {
                    throw std::runtime_error(std::string("'") + filename + "' could not be open for reading");
                }
            }
            std::size_t prefer = 0;
            {
                const auto name_and_value = command.options.find("prefer");
                if (name_and_value != command.options.end()) {
                    prefer = std::stoull(name_and_value->second);
                }
            }
            std::size_t fifo_size = 64;
            {
                const auto name_and_value = command.options.find("buffer");
                if (name_and_value != command.options.end()) {
                    fifo_size = std::stoull(name_and_value->second);
                }
            }
            hummingbird::lightcrafter::ip ip{10, 10, 10, 100};
            {
                const auto name_and_value = command.options.find("ip");
                if (name_and_value != command.options.end()) {
                    ip = hummingbird::lightcrafter::parse_ip(name_and_value->second);
                }
            }
            std::unique_ptr<hummingbird::lightcrafter> lightcrafter;
            if (command.flags.find("windowed") == command.flags.end()) {
                lightcrafter.reset(new hummingbird::lightcrafter(ip));
            }
            auto display = hummingbird::make_display(
                command.flags.find("windowed") != command.flags.end(),
                608,
                684,
                prefer,
                fifo_size,
                [](hummingbird::display_event display_event) {
                    if (display_event.empty_fifo) {
                        std::cout << "warning: empty fifo\n";
                    } else if (
                        display_event.loop_duration > 0
                        && (display_event.loop_duration < 4000 || display_event.loop_duration > 30000)) {
                        std::cout << std::string("warning: throttling (loop duration: ")
                                         + std::to_string(display_event.loop_duration) + " microseconds)\n";
                    }
                    std::cout.flush();
                });
            std::size_t index = 0;
            auto started = false;
            std::vector<uint8_t> data;
            std::atomic_bool running(true);
            auto decoder = hummingbird::make_decoder([&](const Glib::RefPtr<Gst::Buffer>& buffer) {
                hummingbird::interleave(buffer, data);
                while (running.load(std::memory_order_acquire)) {
                    if (display->push(data, index)) {
                        ++index;
                        break;
                    } else {
                        if (!started) {
                            started = true;
                            display->start();
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(20));
                    }
                }
            });
            std::exception_ptr play_exception;
            std::thread play_loop([&]() {
                try {
                    const auto loop = command.flags.find("loop") != command.flags.end();
                    std::size_t video_index = 0;
                    while (running.load(std::memory_order_acquire)) {
                        std::cout << std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                        std::chrono::system_clock::now().time_since_epoch())
                                                        .count())
                                         + " " + command.arguments[video_index] + "\n";
                        std::cout.flush();
                        decoder->read(command.arguments[video_index]);
                        ++video_index;
                        if (video_index >= command.arguments.size()) {
                            if (loop) {
                                video_index = 0;
                            } else {
                                display->close();
                                break;
                            }
                        }
                    }
                } catch (...) {
                    play_exception = std::current_exception();
                    display->close();
                }
            });
            display->run();
            running.store(false, std::memory_order_release);
            decoder->stop();
            play_loop.join();
            if (play_exception) {
                std::rethrow_exception(play_exception);
            }
        });
}
