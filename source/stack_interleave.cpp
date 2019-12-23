#include "../third_party/pontella/source/pontella.hpp"
#include "deinterleave.hpp"
#include <iostream>

int main(int argc, char* argv[]) {
    return pontella::main(
        {
            "stack_interleave converts 608x684@1440Hz binary frames to a YUV4MPEG2 stream",
            "    the app reads a stream of raw, row-major frames from stdin and writes to stdout",
            "Syntax: ./generate [options]",
            "Available options",
            "    -g , --grey    switches the input mode to grey",
            "                       without the flag, raw frames must be 608 * 684 / 8 bytes long",
            "                       with the flag, raw frames must be 608 * 684 bytes long",
            "                       and a value larger than 127 means ON",
            "    -h, --help     shows this help message",
        },
        argc,
        argv,
        0,
        {},
        {{"grey", {"g"}}},
        [](pontella::command command) {
            hummingbird::deinterleave(std::cin, std::cout, command.flags.find("grey") == command.flags.end());
        });
}
