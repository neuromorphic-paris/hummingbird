#pragma once

#include "../third_party/glad/include/glad/glad.h"
#include <GLFW/glfw3.h>
#include <array>
#include <atomic>
#include <chrono>
#include <functional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

/// hummingbird bundles tools to create and play high framerate videos.
namespace hummingbird {
    /// frame reprsents a 2D frame with an id.
    struct frame {
        std::vector<uint8_t> bytes;
        std::size_t id;
    };

    /// display_event bundles feedback data from the display, sent everytime a frame is swapped.
    struct display_event {
        uint32_t tick;
        uint64_t loop_duration;
        bool has_id;
        std::size_t id;
        bool empty_fifo;
    };

    /// display manages a single-window application.
    /// Expected thread management and call order:
    ///     *make_display* must be called from the main thread.
    ///     *run* must be called from the main thread.
    ///     *start* must be called from a secondary thread, and may be called before
    ///     or after *run*. *push* must be called from a secondary thread, and may
    ///     be called before or after *run* and *start*.
    ///         It is recommended to call *push* until it returns false before
    ///         calling start.
    ///     *pause_and_clear* must be called from a secondary thread,
    ///         and may be called before or after *run*, *start* and *push*.
    ///         *pause_and_clear* will not return if *run* has not been called (it
    ///         waits for a FIFO flush).
    ///     *start*, *push* and *pause_and_clear* must be called from the same
    ///     secondary thread.
    ///         In order to replace the secondary thread with another for these
    ///         calls, one must call *pause_and_clear* from the original secondary
    ///         thread, wait for the function to return, then call *start* or *push*
    ///         from the new one.
    ///     *close* can be called from any thread.
    class display {
        public:
        display(uint16_t width, uint16_t height, std::size_t fifo_size) :
            _width(width),
            _height(height),
            _clear_colors(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3, 0),
            _clear_colors_available(false),
            _head(0),
            _tail(0),
            _frames(fifo_size),
            _started(false),
            _accessing_clear_colors(false),
            _window_should_close(false),
            _pause_and_clear_on_empty_fifo(false) {
            _accessing_clear_colors.clear(std::memory_order_release);
            for (auto& frame : _frames) {
                frame.bytes.resize(_width * _height * 3);
            }
        }
        display(const display&) = delete;
        display(display&&) = default;
        display& operator=(const display&) = delete;
        display& operator=(display&&) = default;
        virtual ~display() {}

        /// start activates the display, or ractivates it after a pause.
        /// It must be called by the secondary thread responsible for generating the
        /// frames.
        virtual void start() {
            _started.store(true, std::memory_order_release);
        }

        /// push sends a frame to the display.
        /// If the frame could not be inserted (FIFO full), false is returned.
        /// It must be called by the secondary thread responsible for generating the
        /// frames.
        virtual bool push(std::vector<uint8_t>& bytes, std::size_t id = 0) {
            const auto current_tail = _tail.load(std::memory_order_relaxed);
            const auto next_tail = (current_tail + 1) % _frames.size();
            if (next_tail != _head.load(std::memory_order_acquire)) {
                _frames[current_tail].bytes.swap(bytes);
                _frames[current_tail].id = id;
                _tail.store(next_tail, std::memory_order_release);
                return true;
            }
            return false;
        }

        /// pause_and_clear stops the display, flushes its cache and shows the given
        /// background. It must be called by the secondary thread responsible for
        /// generating the frames.
        virtual void
        pause_and_clear(std::vector<uint8_t> clear_colors, std::atomic_bool* wait_for_empty_fifo = nullptr) {
            while (_accessing_clear_colors.test_and_set(std::memory_order_acquire)) {
            }
            _clear_colors = std::move(clear_colors);
            _clear_colors_available = true;
            _accessing_clear_colors.clear(std::memory_order_release);
            if (wait_for_empty_fifo) {
                _wait_for_empty_fifo = wait_for_empty_fifo;
                _pause_and_clear_on_empty_fifo.store(true, std::memory_order_release);
            } else {
                _started.store(false, std::memory_order_release);
            }
            for (;;) {
                while (_accessing_clear_colors.test_and_set(std::memory_order_acquire)) {
                }
                if (!_clear_colors_available) {
                    _accessing_clear_colors.clear(std::memory_order_release);
                    break;
                }
                _accessing_clear_colors.clear(std::memory_order_release);
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            _wait_for_empty_fifo = nullptr;
            _pause_and_clear_on_empty_fifo.store(false, std::memory_order_release);
        }

        /// close stops the display immediately.
        virtual void close() {
            _window_should_close.store(true, std::memory_order_release);
        }

        protected:
        /// error_callback is called when GLFW encounters an error.
        static void error_callback(int error, const char* description) {
            throw std::logic_error(std::string(description) + " (error " + std::to_string(error) + ")");
        }

        const uint16_t _width;
        const uint16_t _height;
        std::vector<uint8_t> _clear_colors;
        std::atomic_flag _accessing_clear_colors;
        bool _clear_colors_available;
        std::atomic<std::size_t> _head;
        std::atomic<std::size_t> _tail;
        std::vector<frame> _frames;
        std::atomic_bool _started;
        std::atomic_bool _window_should_close;
        std::atomic_bool _pause_and_clear_on_empty_fifo;
        std::atomic_bool* _wait_for_empty_fifo;
    };

    /// specialized_display specializes a display with a template callback.
    template <typename HandleEvent>
    class specialized_display : public display {
        public:
        specialized_display(
            bool windowed,
            uint16_t width,
            uint16_t height,
            std::size_t prefer,
            std::size_t fifo_size,
            HandleEvent handle_event) :
            display(width, height, fifo_size),
            _windowed(windowed),
            _handle_event(std::forward<HandleEvent>(handle_event)) {
            if (!glfwInit()) {
                throw std::runtime_error("initializing GLFW failed");
            }
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
            glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
            glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
            glfwWindowHint(GLFW_AUTO_ICONIFY, 0);
            glfwSetErrorCallback(display::error_callback);
            {
                int monitors_count;
                auto monitors = glfwGetMonitors(&monitors_count);
                if (_windowed) {
                    if (prefer >= monitors_count) {
                        throw std::runtime_error("the preferred index overflows the number of monitors");
                    }
                    _monitor = monitors[prefer];
                } else {
                    std::vector<GLFWmonitor*> candidate_monitors;
                    for (auto monitor_index = 0; monitor_index < monitors_count; ++monitor_index) {
                        const auto mode = glfwGetVideoMode(monitors[monitor_index]);
                        if (mode->width == _width && mode->height == _height) {
                            candidate_monitors.push_back(monitors[monitor_index]);
                        }
                    }
                    if (candidate_monitors.empty()) {
                        throw std::runtime_error("there are no monitors with the requested dimensions");
                    } else if (prefer >= candidate_monitors.size()) {
                        throw std::runtime_error("the preferred indexed overflows the number "
                                                 "of monitors with the requested dimensions");
                    }
                    _monitor = candidate_monitors[prefer];
                    const auto mode = glfwGetVideoMode(_monitor);
                    if (mode->redBits != 8 || mode->greenBits != 8 || mode->blueBits != 8) {
                        throw std::runtime_error("the chosen monitor does not have the expected color depth");
                    }
                    if (mode->refreshRate != 60) {
                        throw std::runtime_error("the chosen monitor does not have the expected refresh rate");
                    }
                }
            }
            {
                const std::size_t size = 256;
                std::vector<unsigned short> r(size);
                std::vector<unsigned short> g(size);
                std::vector<unsigned short> b(size);
                for (std::size_t index = 0; index < size; ++index) {
                    r[index] = static_cast<unsigned short>(index * 64 * 4);
                    g[index] = static_cast<unsigned short>(index * 64 * 4);
                    b[index] = static_cast<unsigned short>(index * 64 * 4);
                }
                GLFWgammaramp ramp{r.data(), g.data(), b.data(), size};
                glfwSetGammaRamp(_monitor, &ramp);
            }
        }
        specialized_display(const specialized_display&) = delete;
        specialized_display(specialized_display&&) = default;
        specialized_display& operator=(const specialized_display&) = delete;
        specialized_display& operator=(specialized_display&&) = default;
        virtual ~specialized_display() {
            glfwTerminate();
        }

        /// run loops until the display is stopped.
        /// It must be called by the main thread.
        virtual void run(std::size_t number_of_initialization_frames = 0) {
            auto window = glfwCreateWindow(_width, _height, "Hummingbird", _windowed ? nullptr : _monitor, nullptr);
            if (!window) {
                throw std::runtime_error("creating a GLFW window failed");
            }
            glfwMakeContextCurrent(window);
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
            glfwSetInputMode(window, GLFW_STICKY_KEYS, 1);
            gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress));
            glfwSwapInterval(1);

            // compile the vertex shader
            const auto vertex_shader_id = glCreateShader(GL_VERTEX_SHADER);
            {
                const std::string vertex_shader(R""(
                    #version 330 core
                    in vec2 coordinates;
                    out vec2 uv;
                    uniform float width;
                    uniform float height;
                    void main() {
                        gl_Position = vec4(coordinates, 0.0, 1.0);
                        uv = vec2((coordinates.x + 1) / 2 * width, (1 - coordinates.y) / 2 * height);
                    }
                )"");
                auto vertex_shader_content = vertex_shader.c_str();
                auto vertex_shader_size = vertex_shader.size();
                glShaderSource(
                    vertex_shader_id,
                    1,
                    static_cast<const GLchar**>(&vertex_shader_content),
                    reinterpret_cast<const GLint*>(&vertex_shader_size));
            }
            glCompileShader(vertex_shader_id);
            check_shader_error(vertex_shader_id);

            // compile the fragment shader
            const auto fragment_shader_id = glCreateShader(GL_FRAGMENT_SHADER);
            {
                const std::string fragment_shader(R""(
                    #version 330 core
                    in vec2 uv;
                    out vec4 color;
                    uniform sampler2DRect sampler;
                    void main() {
                        color = texture(sampler, uv);
                    }
                )"");
                auto fragment_shader_content = fragment_shader.c_str();
                auto fragment_shader_size = fragment_shader.size();
                glShaderSource(
                    fragment_shader_id,
                    1,
                    static_cast<const GLchar**>(&fragment_shader_content),
                    reinterpret_cast<const GLint*>(&fragment_shader_size));
            }
            glCompileShader(fragment_shader_id);
            check_shader_error(fragment_shader_id);

            // create the shaders pipeline
            auto program_id = glCreateProgram();
            glAttachShader(program_id, vertex_shader_id);
            glAttachShader(program_id, fragment_shader_id);
            glLinkProgram(program_id);
            glDeleteShader(vertex_shader_id);
            glDeleteShader(fragment_shader_id);
            glUseProgram(program_id);
            check_program_error(program_id);

            // create the vertex array object
            GLuint vertex_array_id;
            glGenVertexArrays(1, &vertex_array_id);
            glBindVertexArray(vertex_array_id);
            std::array<GLuint, 3> vertex_buffer_ids;
            glGenBuffers(2, vertex_buffer_ids.data());
            {
                glBindBuffer(GL_ARRAY_BUFFER, std::get<0>(vertex_buffer_ids));
                std::array<float, 8> coordinates{-1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f, 1.0f};
                glBufferData(
                    GL_ARRAY_BUFFER,
                    coordinates.size() * sizeof(decltype(coordinates)::value_type),
                    coordinates.data(),
                    GL_STATIC_DRAW);
                glEnableVertexAttribArray(glGetAttribLocation(program_id, "coordinates"));
                glVertexAttribPointer(glGetAttribLocation(program_id, "coordinates"), 2, GL_FLOAT, GL_FALSE, 0, 0);
            }
            {
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, std::get<1>(vertex_buffer_ids));
                std::array<GLuint, 4> indices{0, 1, 2, 3};
                glBufferData(
                    GL_ELEMENT_ARRAY_BUFFER,
                    indices.size() * sizeof(decltype(indices)::value_type),
                    indices.data(),
                    GL_STATIC_DRAW);
            }
            glBindVertexArray(0);

            // set uniforms
            glUniform1f(glGetUniformLocation(program_id, "width"), static_cast<GLfloat>(_width));
            glUniform1f(glGetUniformLocation(program_id, "height"), static_cast<GLfloat>(_height));

            // create the texture
            GLuint texture_id;
            glGenTextures(1, &texture_id);
            glBindTexture(GL_TEXTURE_RECTANGLE, texture_id);
            std::vector<uint8_t> colors(static_cast<std::size_t>(_width) * static_cast<std::size_t>(_height) * 3, 255);
            glTexImage2D(GL_TEXTURE_RECTANGLE, 0, GL_RGB, _width, _height, 0, GL_RGB, GL_UNSIGNED_BYTE, colors.data());
            glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glBindTexture(GL_TEXTURE_RECTANGLE, 0);

            // start the swap loop
            for (std::size_t index = 0; index < number_of_initialization_frames; ++index) {
                glUseProgram(program_id);
                glBindTexture(GL_TEXTURE_RECTANGLE, texture_id);
                glTexImage2D(
                    GL_TEXTURE_RECTANGLE, 0, GL_RGB, _width, _height, 0, GL_RGB, GL_UNSIGNED_BYTE, colors.data());
                glBindVertexArray(vertex_array_id);
                glDrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_INT, 0);
                glBindTexture(GL_TEXTURE_RECTANGLE, 0);
                glBindVertexArray(0);
                glUseProgram(0);
                check_opengl_error();
                glfwSwapBuffers(window);
            }
            std::chrono::high_resolution_clock::time_point previous_loop_time_point;
            uint32_t tick = 0;
            while (!glfwWindowShouldClose(window) && !_window_should_close.load(std::memory_order_acquire)) {
                auto displayed_frame = false;
                auto empty_fifo = false;
                std::size_t frame_id = 0;
                auto local_started = _started.load(std::memory_order_acquire);
                if (local_started) {
                    const auto current_head = _head.load(std::memory_order_relaxed);
                    if (current_head == _tail.load(std::memory_order_acquire)) {
                        if (_pause_and_clear_on_empty_fifo.load(std::memory_order_acquire)) {
                            _pause_and_clear_on_empty_fifo.store(false, std::memory_order_release);
                            _started.store(false, std::memory_order_release);
                            local_started = false;
                        } else {
                            empty_fifo = true;
                        }
                    } else {
                        if (_pause_and_clear_on_empty_fifo.load(std::memory_order_acquire)
                            && !_wait_for_empty_fifo->load(std::memory_order_acquire)) {
                            _pause_and_clear_on_empty_fifo.store(false, std::memory_order_release);
                            _started.store(false, std::memory_order_release);
                            local_started = false;
                        } else {
                            colors.swap(_frames[current_head].bytes);
                            frame_id = _frames[current_head].id;
                            _head.store((current_head + 1) % _frames.size(), std::memory_order_release);
                            displayed_frame = true;
                        }
                    }
                }
                if (!local_started) {
                    while (_accessing_clear_colors.test_and_set(std::memory_order_acquire)) {
                    }
                    if (_clear_colors_available) {
                        _clear_colors_available = false;
                        colors.swap(_clear_colors);
                        _head.store(_tail.load(std::memory_order_acquire), std::memory_order_release);
                    }
                    _accessing_clear_colors.clear(std::memory_order_release);
                }
                glUseProgram(program_id);
                glBindTexture(GL_TEXTURE_RECTANGLE, texture_id);
                glTexImage2D(
                    GL_TEXTURE_RECTANGLE, 0, GL_RGB, _width, _height, 0, GL_RGB, GL_UNSIGNED_BYTE, colors.data());
                glBindVertexArray(vertex_array_id);
                glDrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_INT, 0);
                glBindTexture(GL_TEXTURE_RECTANGLE, 0);
                glBindVertexArray(0);
                glUseProgram(0);
                check_opengl_error();
                glfwSwapBuffers(window);
                const auto now = std::chrono::high_resolution_clock::now();
                _handle_event(display_event{
                    tick,
                    previous_loop_time_point.time_since_epoch().count() > 0 ? static_cast<uint64_t>(
                        std::chrono::duration_cast<std::chrono::microseconds>(now - previous_loop_time_point).count()) :
                                                                              0,
                    displayed_frame,
                    frame_id,
                    empty_fifo,
                });
                ++tick;
                previous_loop_time_point = now;
                glfwPollEvents();
                if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
                    break;
                }
            }
            glDeleteTextures(1, &texture_id);
            glDeleteBuffers(2, vertex_buffer_ids.data());
            glDeleteVertexArrays(1, &vertex_array_id);
            glDeleteProgram(program_id);
            glfwDestroyWindow(window);
        }

        protected:
        /// check_opengl_error throws if openGL generated an error.
        static void check_opengl_error() {
            switch (glGetError()) {
                case GL_NO_ERROR:
                    break;
                case GL_INVALID_ENUM:
                    throw std::logic_error("OpenGL error: GL_INVALID_ENUM");
                case GL_INVALID_VALUE:
                    throw std::logic_error("OpenGL error: GL_INVALID_VALUE");
                case GL_INVALID_OPERATION:
                    throw std::logic_error("OpenGL error: GL_INVALID_OPERATION");
                case GL_OUT_OF_MEMORY:
                    throw std::logic_error("OpenGL error: GL_OUT_OF_MEMORY");
            }
        }

        /// check_shader_error checks for shader compilation errors.
        static void check_shader_error(GLuint shader_id) {
            GLint status = 0;
            glGetShaderiv(shader_id, GL_COMPILE_STATUS, &status);
            if (status != GL_TRUE) {
                GLint message_length = 0;
                glGetShaderiv(shader_id, GL_INFO_LOG_LENGTH, &message_length);
                auto error_message = std::vector<char>(message_length);
                glGetShaderInfoLog(shader_id, message_length, nullptr, error_message.data());
                throw std::logic_error("Shader error: " + std::string(error_message.data()));
            }
        }

        /// check_program_error checks for program errors.
        static void check_program_error(GLuint program_id) {
            GLint status = 0;
            glGetProgramiv(program_id, GL_LINK_STATUS, &status);
            if (status != GL_TRUE) {
                GLint message_length = 0;
                glGetProgramiv(program_id, GL_INFO_LOG_LENGTH, &message_length);
                std::vector<char> error_message(message_length);
                glGetShaderInfoLog(program_id, message_length, nullptr, error_message.data());
                throw std::logic_error("program error: " + std::string(error_message.data()));
            }
        }

        const bool _windowed;
        GLFWmonitor* _monitor;
        HandleEvent _handle_event;
    };

    /// make_display generates a display from a functor.
    template <typename HandleEvent>
    std::unique_ptr<specialized_display<HandleEvent>> make_display(
        bool windowed,
        uint16_t width,
        uint16_t height,
        std::size_t prefer,
        std::size_t fifo_size,
        HandleEvent handle_event) {
        return std::unique_ptr<specialized_display<HandleEvent>>(new specialized_display<HandleEvent>(
            windowed, width, height, prefer, fifo_size, std::forward<HandleEvent>(handle_event)));
    }
}
