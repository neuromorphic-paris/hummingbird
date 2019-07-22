#include "../third_party/pontella/source/pontella.hpp"
#include "lightcrafter.hpp"

int main(int argc, char* argv[]) {
    return pontella::main(
        {
            "change_lightcrafter_ip modifies the LightCrafter's IP address",
            "    the lighcrafter must be restarted afterwards",
            "Syntax: ./change_lightcrafter_ip [options] current_ip new_ip",
            "    'current_ip' and 'new_ip' must be in dot-decimal notation",
            "Available options:",
            "    -h, --help    shows this help message",
        },
        argc,
        argv,
        2,
        {},
        {},
        [](pontella::command command) {
            const auto current_ip = hummingbird::lightcrafter::parse_ip(command.arguments[0]);
            const auto new_ip = hummingbird::lightcrafter::parse_ip(command.arguments[1]);
            hummingbird::lightcrafter lightcrafter(current_ip, hummingbird::lightcrafter::default_settings());
            if (current_ip.byte_0 != new_ip.byte_0 || current_ip.byte_1 != new_ip.byte_1
                || current_ip.byte_2 != new_ip.byte_2 || current_ip.byte_3 != new_ip.byte_3) {
                lightcrafter.message({2, 8, 0, 0, 4, 0, new_ip.byte_0, new_ip.byte_1, new_ip.byte_2, new_ip.byte_3});
            }
        });
}
