#pragma once

#include <algorithm>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <numeric>
#include <stdexcept>
#include <string>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

/// hummingbird bundles tools to create and play high framerate videos.
namespace hummingbird {
    /// lightcrafter manages the RNDIS communication with a LightCrafter.
    class lightcrafter {
        public:
        /// setting defines a LightCrafter parameter.
        struct setting {
            std::string name;
            std::vector<uint8_t> message;
            std::vector<uint8_t> expected_response;
        };

        /// ip represents an IP address.
        struct ip {
            uint8_t byte_0;
            uint8_t byte_1;
            uint8_t byte_2;
            uint8_t byte_3;
        };

        /// parse_ip creates an IP address from a string in dot-decimal notation.
        static ip parse_ip(const std::string& ip_as_string) {
            std::vector<std::size_t> bytes;
            std::string byte_as_string;
            for (auto character : ip_as_string) {
                if (std::isdigit(character)) {
                    byte_as_string.push_back(character);
                } else if (character == '.') {
                    if (byte_as_string.empty()) {
                        throw std::runtime_error("unexpected character '.' in the IP address");
                    }
                    bytes.push_back(std::stoull(byte_as_string));
                    byte_as_string.clear();
                } else {
                    throw std::runtime_error(std::string("unexpected character '") + character + "' in the IP address");
                }
            }
            if (byte_as_string.empty()) {
                throw std::runtime_error("unexpected end of the IP address");
            }
            bytes.push_back(std::stoull(byte_as_string));
            if (bytes.size() != 4) {
                throw std::runtime_error("unexpected number of bytes in the IP address");
            }
            for (auto byte : bytes) {
                if (byte > 255) {
                    throw std::runtime_error("unexpected byte value in the IP address");
                }
            }
            return {static_cast<uint8_t>(bytes[0]),
                    static_cast<uint8_t>(bytes[1]),
                    static_cast<uint8_t>(bytes[2]),
                    static_cast<uint8_t>(bytes[3])};
        }

        /// amend_settings creates a new collection of settings from a base and
        /// changes.
        static std::vector<setting>
        amend_settings(std::vector<setting> base_settings, const std::vector<setting>& changes) {
            for (const auto& change : changes) {
                auto setting_iterator =
                    std::find_if(base_settings.begin(), base_settings.end(), [&](const setting& setting) {
                        return setting.name == change.name;
                    });
                if (setting_iterator == base_settings.end()) {
                    throw std::logic_error(std::string("unknown setting '") + change.name + "'");
                }
                setting_iterator->message = change.message;
                setting_iterator->expected_response = change.expected_response;
            }
            return base_settings;
        }

        /// high_framerate_settings returns the high framerate settings used by the
        /// library.
        static std::vector<setting> high_framerate_settings() {
            return {
                {"display mode", {2, 1, 1, 0, 1, 0, 2}, {3, 1, 1, 0, 0, 0}},
                {"led current", {2, 1, 4, 0, 6, 0, 18, 1, 18, 1, 18, 1}, {3, 1, 4, 0, 0, 0}},
                {"display", {2, 1, 7, 0, 3, 0, 0, 1, 0}, {3, 1, 7, 0, 0, 0}},
                {"video input", {2, 2, 0, 0, 12, 0, 96, 2, 172, 2, 0, 0, 0, 0, 96, 2, 172, 2}, {3, 2, 0, 0, 0, 0}},
                {"video mode", {2, 2, 1, 0, 3, 0, 60, 1, 3}, {3, 2, 1, 0, 0, 0}},
                {"trigger output", {2, 4, 4, 0, 11, 0, 1, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0}, {3, 4, 4, 0, 0, 0}},
            };
        }

        /// default_settings returns the default settings to use the LightCrafter as a
        /// regular projector.
        static std::vector<setting> default_settings() {
            return {
                {"display mode", {2, 1, 1, 0, 1, 0, 2}, {3, 1, 1, 0, 0, 0}},
                {"led current", {2, 1, 4, 0, 6, 0, 18, 1, 18, 1, 18, 1}, {3, 1, 4, 0, 0, 0}},
                {"display", {2, 1, 7, 0, 3, 0, 0, 1, 0}, {3, 1, 7, 0, 0, 0}},
                {"video input", {2, 2, 0, 0, 12, 0, 96, 2, 172, 2, 0, 0, 0, 0, 96, 2, 172, 2}, {3, 2, 0, 0, 0, 0}},
                {"video mode", {2, 2, 1, 0, 3, 0, 60, 8, 1}, {3, 2, 1, 0, 0, 0}},
                {"trigger output", {2, 4, 4, 0, 11, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {3, 4, 4, 0, 0, 0}},
            };
        }

        lightcrafter(ip ip, const std::vector<setting>& settings = high_framerate_settings()) :
            _socket_file_descriptor(socket(AF_INET, SOCK_STREAM, 0)) {
            if (_socket_file_descriptor < 0) {
                throw std::logic_error("creating a socket failed");
            }
            sockaddr_in server_address;
            server_address.sin_family = AF_INET;
            server_address.sin_port = htons(0x5555);
            server_address.sin_addr.s_addr =
                (static_cast<uint32_t>(ip.byte_0) | (static_cast<uint32_t>(ip.byte_1) << 8)
                 | (static_cast<uint32_t>(ip.byte_2) << 16) | (static_cast<uint32_t>(ip.byte_3) << 24));
            {
                const auto flags = fcntl(_socket_file_descriptor, F_GETFL, 0);
                if (flags < 0) {
                    throw std::logic_error("retrieving the socket flags failed");
                }
                if (fcntl(_socket_file_descriptor, F_SETFL, flags | O_NONBLOCK) < 0) {
                    throw std::logic_error("setting the socket mode to non-blocking failed");
                }
            }
            timeval timeout_duration;
            timeout_duration.tv_sec = 1;
            timeout_duration.tv_usec = 0;
            {
                const auto error = connect(
                    _socket_file_descriptor, reinterpret_cast<sockaddr*>(&server_address), sizeof(server_address));
                if (error < 0) {
                    if (errno != EINPROGRESS) {
                        throw std::runtime_error("connecting to the LightCrafter failed");
                    }
                    fd_set read_file_descriptor_set;
                    fd_set write_file_descriptor_set;
                    FD_ZERO(&read_file_descriptor_set);
                    FD_ZERO(&write_file_descriptor_set);
                    FD_SET(_socket_file_descriptor, &read_file_descriptor_set);
                    FD_SET(_socket_file_descriptor, &write_file_descriptor_set);
                    const auto changes = select(
                        _socket_file_descriptor + 1,
                        &read_file_descriptor_set,
                        &write_file_descriptor_set,
                        nullptr,
                        &timeout_duration);
                    int error;
                    socklen_t length = sizeof(error);
                    if (changes <= 0
                        || (FD_ISSET(_socket_file_descriptor, &read_file_descriptor_set) == 0
                            && FD_ISSET(_socket_file_descriptor, &write_file_descriptor_set) == 0)
                        || getsockopt(_socket_file_descriptor, SOL_SOCKET, SO_ERROR, &error, &length) < 0) {
                        throw std::runtime_error("connecting to the LightCrafter failed");
                    }
                }
            }
            {
                const auto flags = fcntl(_socket_file_descriptor, F_GETFL, 0);
                if (flags < 0) {
                    throw std::logic_error("retrieving the socket flags failed");
                }
                if (fcntl(_socket_file_descriptor, F_SETFL, flags & (~O_NONBLOCK)) < 0) {
                    throw std::logic_error("setting the socket mode to blocking failed");
                }
            }
            if (setsockopt(
                    _socket_file_descriptor, SOL_SOCKET, SO_RCVTIMEO, &timeout_duration, sizeof(timeout_duration))
                < 0) {
                throw std::logic_error("setting the socket receive timeout failed");
            }
            if (setsockopt(
                    _socket_file_descriptor, SOL_SOCKET, SO_SNDTIMEO, &timeout_duration, sizeof(timeout_duration))
                < 0) {
                throw std::logic_error("setting the socket send timeout failed");
            }
            load_settings(settings);
        }
        lightcrafter(const lightcrafter&) = delete;
        lightcrafter(lightcrafter&&) = default;
        lightcrafter& operator=(const lightcrafter&) = delete;
        lightcrafter& operator=(lightcrafter&&) = default;
        virtual ~lightcrafter() {
            try {
                load_settings(default_settings());
            } catch (const std::runtime_error&) {
            }
            close(_socket_file_descriptor);
        }

        /// message sends a message to the LightCrafter and waits for the answer.
        virtual std::vector<uint8_t> message(std::vector<uint8_t> bytes) {
            bytes.push_back(static_cast<uint8_t>(
                std::accumulate(
                    bytes.begin(),
                    bytes.end(),
                    static_cast<uint64_t>(0),
                    [](uint64_t sum, uint8_t byte) { return sum + byte; })
                % 0x100));
            if (::write(_socket_file_descriptor, bytes.data(), bytes.size()) != bytes.size()) {
                throw std::runtime_error("sending a message to the LightCrafter failed");
            }
            std::vector<uint8_t> response(6);
            {
                const auto bytes_read = ::read(_socket_file_descriptor, response.data(), response.size());
                if (bytes_read != response.size()) {
                    throw std::runtime_error("reading from the LightCrafter failed");
                }
                response.resize(6 + (response[4] | (response[5] << 8)) + 1);
            }
            {
                const auto bytes_read = ::read(_socket_file_descriptor, response.data() + 6, response.size() - 6);
                if (bytes_read != response.size() - 6) {
                    throw std::runtime_error("reading from the LightCrafter failed");
                }
            }
            if (response.back()
                != static_cast<uint8_t>(
                    std::accumulate(
                        response.begin(),
                        std::prev(response.end()),
                        static_cast<uint64_t>(0),
                        [](uint64_t sum, uint8_t byte) { return sum + byte; })
                    % 0x100)) {
                throw std::runtime_error("the LightCrafter response is corrupted");
            }
            response.resize(response.size() - 1);
            return response;
        }

        /// load_settings sets all the LightCrafter's parameters.
        virtual void load_settings(const std::vector<setting>& settings) {
            for (const auto& setting : settings) {
                if (message(setting.message) != setting.expected_response) {
                    throw std::runtime_error(
                        std::string("unexpected LightCrafter response to ") + setting.name + " setting");
                }
            }
        }

        protected:
        int _socket_file_descriptor;
    };
}
