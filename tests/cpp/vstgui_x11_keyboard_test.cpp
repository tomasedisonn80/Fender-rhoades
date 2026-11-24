// Exercises the linked VSTGUI X11 converter, including the build overlay.
// Run on the private display supplied by tools/run_xvfb_test.py. No converter
// implementation or SDK source is included in this translation unit.
#include "vstgui/lib/events.h"
#include "vstgui/lib/platform/linux/linuxfactory.h"
#include "vstgui/lib/platform/linux/x11platform.h"
#include "vstgui/lib/vstguiinit.h"

#include <X11/keysym.h>
#include <xcb/xcb.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <poll.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {
namespace gui = VSTGUI;

void require(bool success, const std::string& message) {
    if (!success) { throw std::runtime_error(message); }
}

template <typename T>
auto xcb_owned(T* pointer) {
    return std::unique_ptr<T, decltype(&std::free)>(pointer, &std::free);
}

std::string hex(std::uint16_t value) {
    std::ostringstream result;
    result << "0x" << std::hex << value;
    return result.str();
}

std::string describe(const gui::Modifiers& modifiers) {
    std::string result;
    for (const auto& entry : std::array<std::pair<gui::ModifierKey, const char*>, 4>{{
             {gui::ModifierKey::Shift, "Shift"}, {gui::ModifierKey::Control, "Control"},
             {gui::ModifierKey::Alt, "Alt"}, {gui::ModifierKey::Super, "Super"}}}) {
        if (!modifiers.has(entry.first)) { continue; }
        if (!result.empty()) { result += '+'; }
        result += entry.second;
    }
    return result.empty() ? "none" : result;
}

void check_request(xcb_connection_t* connection, xcb_void_cookie_t cookie,
                   const std::string& operation) {
    const auto error = xcb_owned(xcb_request_check(connection, cookie));
    require(!error, operation + " failed with X11 error " +
            std::to_string(error ? error->error_code : 0));
    require(xcb_connection_has_error(connection) == 0,
            operation + ": X11 connection failed");
}

class TestRunLoop final : public gui::IRunLoop, public gui::AtomicReferenceCounted {
public:
    int descriptor{-1};
    gui::IEventHandler* handler{nullptr};

    bool registerEventHandler(int fd, gui::IEventHandler* value) override {
        if (handler != nullptr || fd < 0 || value == nullptr) { return false; }
        descriptor = fd;
        handler = value;
        return true;
    }
    bool unregisterEventHandler(gui::IEventHandler* value) override {
        if (handler != value) { return false; }
        descriptor = -1;
        handler = nullptr;
        return true;
    }
    bool registerTimer(std::uint64_t, gui::ITimerHandler*) override { return false; }
    bool unregisterTimer(gui::ITimerHandler*) override { return false; }
};

struct RecordedKey {
    std::uint8_t native_type;
    xcb_keycode_t native_key;
    std::uint16_t native_state;
    xcb_timestamp_t native_time;
    gui::EventType type;
    gui::Modifiers modifiers;
    gui::VirtualKey key;
    char32_t character;
};

class FrameRecorder final : public gui::X11::IFrameEventHandler {
public:
    std::vector<RecordedKey> keys;

    void onEvent(xcb_key_press_event_t& event) override {
        // KeyboardEvent is move-only. Copy its observable fields while the
        // converter's current event is valid, without moving or editing it.
        const auto& converted = gui::X11::RunLoop::instance().getCurrentKeyEvent();
        keys.push_back({event.response_type, event.detail, event.state, event.time,
                        converted.type, converted.modifiers, converted.virt,
                        converted.character});
    }
    void onEvent(xcb_map_notify_event_t&) override {}
    void onEvent(xcb_button_press_event_t&) override {}
    void onEvent(xcb_motion_notify_event_t&) override {}
    void onEvent(xcb_enter_notify_event_t&) override {}
    void onEvent(xcb_focus_in_event_t&) override {}
    void onEvent(xcb_expose_event_t&) override {}
    void onEvent(xcb_property_notify_event_t&) override {}
    void onEvent(xcb_selection_notify_event_t&) override {}
    void onEvent(xcb_client_message_event_t&, xcb_window_t) override {}
};

struct ExpectedKey {
    bool down;
    std::uint16_t state;
    gui::Modifiers modifiers;
};

class Fixture {
public:
    Fixture() {
        // The pinned RunLoop assumes a usable X server during initialization.
        // Diagnose a missing display before entering that initialization path.
        const auto probe = std::unique_ptr<xcb_connection_t, decltype(&xcb_disconnect)>(
            xcb_connect(nullptr, nullptr), &xcb_disconnect);
        require(probe && xcb_connection_has_error(probe.get()) == 0,
                "cannot open DISPLAY; run this test with tools/run_xvfb_test.py");
        gui::init(nullptr);
        platform_started_ = true;
        try {
            factory_ = gui::getPlatformFactory().asLinuxFactory();
            require(factory_ != nullptr, "VSTGUI did not provide LinuxFactory");
            previous_loop_ = factory_->getRunLoop();
            run_loop_ = gui::owned(new TestRunLoop);
            factory_->setRunLoop(run_loop_);
            gui::X11::RunLoop::init();
            native_started_ = true;
            connection_ = gui::X11::RunLoop::instance().getXcbConnection();
            require(connection_ && xcb_connection_has_error(connection_) == 0,
                    "VSTGUI X11 connection initialization failed");
            require(run_loop_->handler != nullptr && run_loop_->descriptor >= 0,
                    "VSTGUI did not register its X11 event handler with IRunLoop");
            const auto screens = xcb_setup_roots_iterator(xcb_get_setup(connection_));
            require(screens.rem > 0 && screens.data != nullptr, "X11 has no screen");
            root_ = screens.data->root;
            window_ = xcb_generate_id(connection_);
            constexpr std::uint32_t mask = XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE;
            check_request(connection_, xcb_create_window_checked(connection_,
                XCB_COPY_FROM_PARENT, window_, root_, 0, 0, 32, 32, 0,
                XCB_WINDOW_CLASS_INPUT_OUTPUT, XCB_COPY_FROM_PARENT,
                XCB_CW_EVENT_MASK, &mask), "create keyboard test window");
            gui::X11::RunLoop::instance().registerWindowEventHandler(window_, &recorder_);
            window_registered_ = true;
            keycode_ = right_arrow_keycode();
            require_physical_keys_clear("initial display state");
        } catch (...) {
            cleanup();
            throw;
        }
    }

    ~Fixture() { cleanup(); }
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;

    void exercise(const char* name, std::initializer_list<ExpectedKey> expected) {
        require_physical_keys_clear(name);
        const auto first = recorder_.keys.size();
        const auto first_time = next_time_;
        std::vector<xcb_void_cookie_t> requests;
        requests.reserve(expected.size());
        for (const auto& wanted : expected) {
            xcb_key_press_event_t packet{};
            packet.response_type = wanted.down ? XCB_KEY_PRESS : XCB_KEY_RELEASE;
            packet.detail = keycode_;
            packet.time = ++next_time_;
            packet.root = root_;
            packet.event = window_;
            packet.same_screen = 1;
            packet.state = wanted.state;
            const auto mask = wanted.down ? XCB_EVENT_MASK_KEY_PRESS : XCB_EVENT_MASK_KEY_RELEASE;
            requests.push_back(xcb_send_event_checked(connection_, false, window_, mask,
                reinterpret_cast<const char*>(&packet)));
        }
        require(xcb_flush(connection_) > 0, std::string(name) + ": flush failed");
        // Check/synchronize every request before invoking the captured handler.
        // Thus the entire batch is queued when event conversion begins.
        for (const auto request : requests) { check_request(connection_, request, name); }
        require(recorder_.keys.size() == first,
                std::string(name) + ": event dispatched before the test pumped IRunLoop");
        require_physical_keys_clear(name);
        pump_until(first + expected.size(), name);

        std::size_t index = 0;
        for (const auto& wanted : expected) {
            const auto& actual = recorder_.keys[first + index];
            const auto context = std::string(name) + (wanted.down ? " press" : " release") +
                " #" + std::to_string(index) + " state=" + hex(wanted.state);
            const auto type = wanted.down ? XCB_KEY_PRESS : XCB_KEY_RELEASE;
            require(actual.native_type == (type | 0x80) && actual.native_key == keycode_ &&
                    actual.native_state == wanted.state &&
                    actual.native_time == first_time + static_cast<xcb_timestamp_t>(index + 1),
                    context + ": native SendEvent packet/order mismatch");
            require(actual.type == (wanted.down ? gui::EventType::KeyDown : gui::EventType::KeyUp),
                    context + ": converted event type mismatch");
            require(actual.modifiers == wanted.modifiers,
                    context + ": expected " + describe(wanted.modifiers) + ", got " +
                    describe(actual.modifiers) + " while live physical modifiers were clear");
            require(actual.key == gui::VirtualKey::Right && actual.character == 0,
                    context + ": Right key was not converted through the VSTGUI key map");
            ++index;
        }
        require_physical_keys_clear(name);
        std::cout << "PASS " << name << " (" << expected.size() << " native packets)\n";
    }

    std::size_t event_count() const { return recorder_.keys.size(); }

private:
    void cleanup() noexcept {
        if (window_registered_) {
            gui::X11::RunLoop::instance().unregisterWindowEventHandler(window_);
            window_registered_ = false;
        }
        if (connection_ && window_ != XCB_WINDOW_NONE) {
            xcb_destroy_window(connection_, window_);
            window_ = XCB_WINDOW_NONE;
        }
        if (native_started_) {
            gui::X11::RunLoop::exit();
            native_started_ = false;
            connection_ = nullptr;
        }
        if (factory_) { factory_->setRunLoop(previous_loop_); }
        if (platform_started_) {
            gui::exit();
            platform_started_ = false;
        }
        factory_ = nullptr;
    }

    xcb_keycode_t right_arrow_keycode() const {
        const auto* setup = xcb_get_setup(connection_);
        const auto count = static_cast<std::uint8_t>(setup->max_keycode - setup->min_keycode + 1);
        const auto mapping = xcb_owned(xcb_get_keyboard_mapping_reply(connection_,
            xcb_get_keyboard_mapping(connection_, setup->min_keycode, count), nullptr));
        require(mapping && mapping->keysyms_per_keycode > 0, "cannot read X11 keyboard mapping");
        const auto* symbols = xcb_get_keyboard_mapping_keysyms(mapping.get());
        for (unsigned offset = 0; offset < count; ++offset) {
            if (symbols[offset * mapping->keysyms_per_keycode] == XK_Right) {
                return static_cast<xcb_keycode_t>(setup->min_keycode + offset);
            }
        }
        throw std::runtime_error("X11 key map has no unmodified Right key");
    }

    void require_physical_keys_clear(const std::string& context) const {
        constexpr std::uint16_t modifiers = XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_LOCK |
            XCB_MOD_MASK_CONTROL | XCB_MOD_MASK_1 | XCB_MOD_MASK_2 | XCB_MOD_MASK_3 |
            XCB_MOD_MASK_4 | XCB_MOD_MASK_5;
        const auto pointer = xcb_owned(xcb_query_pointer_reply(connection_,
            xcb_query_pointer(connection_, root_), nullptr));
        require(pointer != nullptr, context + ": cannot query live X11 modifiers");
        require((pointer->mask & modifiers) == 0,
                context + ": live X11 modifiers are " + hex(pointer->mask & modifiers) +
                "; use a private Xvfb display with modifiers released and locks clear");
        const auto keys = xcb_owned(xcb_query_keymap_reply(connection_,
            xcb_query_keymap(connection_), nullptr));
        require(keys != nullptr, context + ": cannot query physical X11 keys");
        for (const auto byte : keys->keys) {
            require(byte == 0, context + ": physical key is held; use a private Xvfb display");
        }
    }

    void pump_until(std::size_t expected, const std::string& context) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (recorder_.keys.size() < expected) {
            require(run_loop_->handler != nullptr, context + ": X11 handler was unregistered");
            // XCB replies can have already buffered events, so invoke the real
            // handler before waiting on its descriptor, even when poll is quiet.
            run_loop_->handler->onEvent();
            require(xcb_connection_has_error(connection_) == 0,
                    context + ": X11 connection failed while dispatching");
            if (recorder_.keys.size() >= expected) { break; }
            require(std::chrono::steady_clock::now() < deadline,
                    context + ": timed out after 2 seconds; received " +
                    std::to_string(recorder_.keys.size()) + " of " + std::to_string(expected) +
                    " total keyboard events");
            pollfd descriptor{run_loop_->descriptor, POLLIN, 0};
            const auto status = ::poll(&descriptor, 1, 10);
            require(status >= 0 || errno == EINTR, context + ": poll failed");
            require((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) == 0,
                    context + ": X11 descriptor closed or failed");
        }
        require(recorder_.keys.size() == expected, context + ": received extra keyboard events");
    }

    gui::SharedPointer<TestRunLoop> run_loop_;
    gui::SharedPointer<gui::IRunLoop> previous_loop_;
    const gui::LinuxFactory* factory_{nullptr};
    xcb_connection_t* connection_{nullptr};
    xcb_window_t root_{XCB_WINDOW_NONE};
    xcb_window_t window_{XCB_WINDOW_NONE};
    xcb_keycode_t keycode_{0};
    xcb_timestamp_t next_time_{0};
    FrameRecorder recorder_;
    bool platform_started_{false};
    bool native_started_{false};
    bool window_registered_{false};
};

void watchdog(int) {
    // XCB initialization/replies and VSTGUI's own synchronous flush are also
    // bounded. Only async-signal-safe calls are permitted in this handler.
    constexpr char message[] =
        "FAIL VSTGUI X11 keyboard: 20-second watchdog expired during X11 setup/dispatch\n";
    const auto ignored = ::write(STDERR_FILENO, message, sizeof(message) - 1);
    (void)ignored;
    ::_exit(1);
}

void run() {
    using M = gui::ModifierKey;
    constexpr std::uint16_t buttons = XCB_BUTTON_MASK_1 | XCB_BUTTON_MASK_2 |
        XCB_BUTTON_MASK_3 | XCB_BUTTON_MASK_4 | XCB_BUTTON_MASK_5;
    constexpr std::uint16_t ignored = XCB_MOD_MASK_LOCK | XCB_MOD_MASK_2 | XCB_MOD_MASK_3 | buttons;
    constexpr std::uint16_t all = XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_CONTROL |
        XCB_MOD_MASK_1 | XCB_MOD_MASK_4 | XCB_MOD_MASK_5;
    struct Scenario { const char* name; std::uint16_t state; gui::Modifiers expected; };
    // Explicit expectations remain independent of the converter implementation.
    const std::array<Scenario, 19> scenarios{{
        {"bare", 0, {}},
        {"shift", XCB_MOD_MASK_SHIFT, {M::Shift}},
        {"control", XCB_MOD_MASK_CONTROL, {M::Control}},
        {"mod1_alt", XCB_MOD_MASK_1, {M::Alt}},
        {"mod5_alt", XCB_MOD_MASK_5, {M::Alt}},
        {"mod4_super", XCB_MOD_MASK_4, {M::Super}},
        {"mod1_and_mod5", XCB_MOD_MASK_1 | XCB_MOD_MASK_5, {M::Alt}},
        {"shift_control", XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_CONTROL, {M::Shift, M::Control}},
        {"shift_mod4", XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_4, {M::Shift, M::Super}},
        {"control_mod4", XCB_MOD_MASK_CONTROL | XCB_MOD_MASK_4, {M::Control, M::Super}},
        {"mod1_mod4", XCB_MOD_MASK_1 | XCB_MOD_MASK_4, {M::Alt, M::Super}},
        {"mod5_mod4", XCB_MOD_MASK_5 | XCB_MOD_MASK_4, {M::Alt, M::Super}},
        {"all_modifiers", all, {M::Shift, M::Control, M::Alt, M::Super}},
        {"ignore_lock", XCB_MOD_MASK_LOCK, {}},
        {"ignore_mod2", XCB_MOD_MASK_2, {}},
        {"ignore_mod3", XCB_MOD_MASK_3, {}},
        {"ignore_buttons", buttons, {}},
        {"ignore_combined", ignored, {}},
        {"all_with_ignored_bits", all | ignored, {M::Shift, M::Control, M::Alt, M::Super}},
    }};
    Fixture fixture;
    for (const auto& scenario : scenarios) {
        fixture.exercise(scenario.name, {{true, scenario.state, scenario.expected},
                                        {false, scenario.state, scenario.expected}});
    }
    fixture.exercise("queued_mod4_then_plain", {
        {true, XCB_MOD_MASK_4, {M::Super}}, {false, XCB_MOD_MASK_4, {M::Super}},
        {true, 0, {}}, {false, 0, {}},
    });
    std::cout << "VSTGUI X11 keyboard: " << scenarios.size() + 1 << " cases, "
              << fixture.event_count() << " native packets passed\n";
}
} // namespace

int main() {
    if (std::signal(SIGALRM, watchdog) == SIG_ERR) {
        std::cerr << "FAIL VSTGUI X11 keyboard: cannot install timeout watchdog\n";
        return 1;
    }
    ::alarm(20);
    try {
        run();
        ::alarm(0);
        return 0;
    } catch (const std::exception& error) {
        ::alarm(0);
        std::cerr << "FAIL VSTGUI X11 keyboard: " << error.what() << '\n';
        return 1;
    }
}
