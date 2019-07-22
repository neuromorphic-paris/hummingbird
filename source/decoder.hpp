#pragma once

#include <atomic>
#include <functional>
#include <glibmm/main.h>
#include <gstreamermm.h>
#include <gstreamermm/appsink.h>
#include <gstreamermm/bin.h>
#include <gstreamermm/caps.h>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

/// hummingbird bundles tools to create and play high framerate videos.
namespace hummingbird {
    /// decoder reads and decodes a H.264 stream inside any container known by
    /// avcodec.
    template <typename HandleFrame>
    class decoder {
        public:
        /// jetson_h264_to_i420 returns a hardware implementation of the element
        /// compatible with the Jetson TX1 board.
        static Glib::RefPtr<Gst::Element> jetson_h264_to_i420() {
            auto bin = Gst::Bin::create();
            auto omxh264dec = create("omxh264dec");
            auto nvvidconv = create("nvvidconv");
            nvvidconv->set_property("silent", true);
            auto capsfilter = create("capsfilter");
            auto caps = Gst::Caps::create_simple("video/x-raw");
            caps->set_value("format", Glib::ustring("I420"));
            capsfilter->set_property("caps", caps);
            bin->add(omxh264dec)->add(nvvidconv)->add(capsfilter);
            bin->add_ghost_pad(omxh264dec, "sink");
            omxh264dec->link(nvvidconv)->link(capsfilter);
            bin->add_ghost_pad(capsfilter, "src");
            return bin;
        }

        /// software_h264_to_i420 returns a software implementation of the element.
        static Glib::RefPtr<Gst::Element> software_h264_to_i420() {
            return create("avdec_h264");
        }

        /// h264_to_i420_candidates returns the candidate factories for creating a
        /// H.264 to I420 element.
        static std::vector<std::function<Glib::RefPtr<Gst::Element>()>> h264_to_i420_candidates() {
            return {jetson_h264_to_i420, software_h264_to_i420};
        }

        decoder(HandleFrame handle_frame, Glib::RefPtr<Gst::Element> h264_to_i420 = Glib::RefPtr<Gst::Element>()) :
            _handle_frame(std::forward<HandleFrame>(handle_frame)) {
            Gst::init_check();
            _main_context = Glib::MainContext::create();
            _main_loop = Glib::MainLoop::create(_main_context);
            _pipeline = Gst::Pipeline::create();
            _filesrc = create("filesrc");
            auto demux = create("qtdemux");
            demux->signal_pad_added().connect(sigc::mem_fun(*this, &decoder<HandleFrame>::handle_pad_added));
            _queue = create("queue");
            auto h264parse = create("h264parse");
            if (!h264_to_i420) {
                for (const auto& h264_to_i420_candidate : h264_to_i420_candidates()) {
                    try {
                        h264_to_i420 = h264_to_i420_candidate();
                        break;
                    } catch (const std::logic_error&) {
                    }
                }
                if (!h264_to_i420) {
                    throw std::logic_error("all the H.264 to I420 candidates failed");
                }
            }
            _sink = Gst::AppSink::create();
            if (!_sink) {
                throw std::logic_error("creating the app sink failed");
            }
            _sink->set_property("sync", false);
            _sink->set_property("emit_signals", true);
            _sink->set_property("drop", false);
            _sink->set_property<guint>("max_buffers", 64);
            _sink->signal_new_sample().connect(sigc::mem_fun(*this, &decoder<HandleFrame>::handle_sample));
            _pipeline->add(_filesrc)->add(demux)->add(_queue)->add(h264parse)->add(h264_to_i420)->add(_sink);
            _filesrc->link(demux);
            _queue->link(h264parse)->link(h264_to_i420)->link(_sink);
            _loop = std::thread([this]() {
                _main_context->acquire();
                _main_loop->run();
            });
            set_state(Gst::STATE_READY);
        }
        decoder(const decoder&) = delete;
        decoder(decoder&&) = default;
        decoder& operator=(const decoder&) = delete;
        decoder& operator=(decoder&&) = default;
        virtual ~decoder() {
            set_state(Gst::STATE_NULL);
            _main_loop->quit();
            _loop.join();
        }

        /// read opens a H.264 file and decodes its frames.
        virtual void read(const std::string& filename) {
            _running.store(true, std::memory_order_release);
            set_state(Gst::STATE_READY);
            _filesrc->set_property("location", filename);
            set_state(Gst::STATE_PLAYING);
            auto bus = _pipeline->get_bus();
            while (_running.load(std::memory_order_acquire)) {
                auto message = bus->pop(static_cast<Gst::ClockTime>(10 * Gst::MILLI_SECOND));
                if (message) {
                    switch (message->get_message_type()) {
                        case Gst::MESSAGE_EOS:
                            _running.store(false, std::memory_order_release);
                            break;
                        case Gst::MESSAGE_ERROR: {
                            Glib::Error error;
                            std::string debug;
                            Glib::RefPtr<Gst::MessageError>::cast_static(message)->parse(error, debug);
                            throw std::runtime_error(error.what());
                        }
                        default:
                            break;
                    }
                }
            }
            set_state(Gst::STATE_PAUSED);
        }

        /// stop interrupts the stream being played.
        virtual void stop() {
            _running.store(false, std::memory_order_release);
        }

        protected:
        /// create builds a pipeline element.
        static Glib::RefPtr<Gst::Element> create(const std::string& name) {
            auto element = Gst::ElementFactory::create_element(name);
            if (!element) {
                throw std::logic_error(std::string("creating the element '") + name + "' failed");
            }
            return element;
        }

        /// set_state changes the pipeline state and waits for the change to happen.
        virtual void set_state(Gst::State state) {
            if (_pipeline->set_state(state) == Gst::STATE_CHANGE_ASYNC) {
                Gst::State current_state;
                Gst::State pending_state;
                if (_pipeline->get_state(current_state, pending_state, Gst::CLOCK_TIME_NONE)
                    == Gst::STATE_CHANGE_FAILURE) {
                    throw std::logic_error("changing the state failed");
                }
            }
        }

        /// handle_sample is called by the pipeline when a sample is available.
        virtual Gst::FlowReturn handle_sample() {
            _handle_frame(_sink->pull_sample()->get_buffer());
            return Gst::FLOW_OK;
        }

        /// handle_pad_added connects the demuxer and the queue.
        void handle_pad_added(const Glib::RefPtr<Gst::Pad>& pad) {
            pad->link(_queue->get_static_pad("sink"));
        }

        HandleFrame _handle_frame;
        Glib::RefPtr<Glib::MainContext> _main_context;
        Glib::RefPtr<Glib::MainLoop> _main_loop;
        Glib::RefPtr<Gst::Pipeline> _pipeline;
        Glib::RefPtr<Gst::Element> _filesrc;
        Glib::RefPtr<Gst::Element> _queue;
        Glib::RefPtr<Gst::AppSink> _sink;
        std::thread _loop;
        std::atomic_bool _running;
    };

    /// make_decoder generates a decoder from a functor.
    template <typename HandleFrame>
    std::unique_ptr<decoder<HandleFrame>> make_decoder(HandleFrame handle_frame) {
        return std::unique_ptr<decoder<HandleFrame>>(new decoder<HandleFrame>(std::forward<HandleFrame>(handle_frame)));
    }
}
