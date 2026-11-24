// Linux native lifecycle regression. Only SDK hosting/public interfaces are
// linked; the editor and VSTGUI execute exclusively in the loaded real bundle.
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"

#include <X11/keysym.h>
#include <xcb/xcb.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

namespace {
namespace sdk = Steinberg;
namespace vst = Steinberg::Vst;
namespace linux_api = Steinberg::Linux;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

void require(bool success, std::string_view what) {
    if (!success) { throw std::runtime_error(std::string{what}); }
}
template<class T> auto xowned(T* p) {
    return std::unique_ptr<T, decltype(&std::free)>{p, &std::free};
}

enum class Stage { begin, perform, end };

// Stack-owned host outlives every plug-in interface. Registered handlers have
// strong references and generation numbers; snapshots permit unregistering or
// registering handlers from inside an event/timer callback, including nested
// pumping, without retaining a stale vector iterator or calling retired work.
class Host final : public vst::HostApplication, public sdk::IPlugFrame,
                   public linux_api::IRunLoop, public vst::IComponentHandler {
public:
    struct Event {
        std::uint64_t generation;
        sdk::IPtr<linux_api::IEventHandler> handler;
        int fd;
    };
    struct Timer {
        std::uint64_t generation;
        sdk::IPtr<linux_api::ITimerHandler> handler;
        std::chrono::milliseconds interval;
        Clock::time_point next;
    };
    std::vector<Event> events;
    std::vector<Timer> timers;
    std::function<void(Stage)> hook;
    bool refuse_close_timer = false;
    unsigned refused = 0, close_ticks = 0, begins = 0, performs = 0, ends = 0;
    std::vector<Stage> order;

    sdk::tresult PLUGIN_API queryInterface(const sdk::TUID iid, void** object) override {
        if (!object) { return sdk::kInvalidArgument; }
        *object = nullptr;
        if (sdk::FUnknownPrivate::iidEqual(iid, sdk::IPlugFrame::iid)) {
            *object = static_cast<sdk::IPlugFrame*>(this);
        } else if (sdk::FUnknownPrivate::iidEqual(iid, linux_api::IRunLoop::iid)) {
            *object = static_cast<linux_api::IRunLoop*>(this);
        } else if (sdk::FUnknownPrivate::iidEqual(iid, vst::IComponentHandler::iid)) {
            *object = static_cast<vst::IComponentHandler*>(this);
        } else {
            return HostApplication::queryInterface(iid, object);
        }
        addRef();
        return sdk::kResultOk;
    }
    sdk::uint32 PLUGIN_API addRef() override { return ++references_; }
    sdk::uint32 PLUGIN_API release() override { return --references_; }
    sdk::tresult PLUGIN_API resizeView(sdk::IPlugView* view, sdk::ViewRect* size) override {
        return view && size ? view->onSize(size) : sdk::kInvalidArgument;
    }
    sdk::tresult PLUGIN_API registerEventHandler(linux_api::IEventHandler* h, int fd) override {
        if (!h || fd < 0) { return sdk::kInvalidArgument; }
        events.push_back({++generation_, sdk::IPtr<linux_api::IEventHandler>{h}, fd});
        return sdk::kResultOk;
    }
    sdk::tresult PLUGIN_API unregisterEventHandler(linux_api::IEventHandler* h) override {
        const auto old = events.size();
        std::erase_if(events, [h](const auto& e) { return e.handler.get() == h; });
        return events.size() < old ? sdk::kResultOk : sdk::kResultFalse;
    }
    sdk::tresult PLUGIN_API registerTimer(linux_api::ITimerHandler* h,
                                        linux_api::TimerInterval ms) override {
        if (!h || ms == 0 || ms > 60000) { return sdk::kInvalidArgument; }
        if (refuse_close_timer && ms == 1) { ++refused; return sdk::kResultFalse; }
        const auto interval = std::chrono::milliseconds{ms};
        timers.push_back({++generation_, sdk::IPtr<linux_api::ITimerHandler>{h},
                          interval, Clock::now() + interval});
        return sdk::kResultOk;
    }
    sdk::tresult PLUGIN_API unregisterTimer(linux_api::ITimerHandler* h) override {
        const auto old = timers.size();
        std::erase_if(timers, [h](const auto& t) { return t.handler.get() == h; });
        return timers.size() < old ? sdk::kResultOk : sdk::kResultFalse;
    }
    sdk::tresult PLUGIN_API beginEdit(vst::ParamID id) override {
        require(id == 0, "unexpected edited parameter");
        ++begins; return callback(Stage::begin);
    }
    sdk::tresult PLUGIN_API performEdit(vst::ParamID id, vst::ParamValue) override {
        require(id == 0, "unexpected performed parameter");
        ++performs; return callback(Stage::perform);
    }
    sdk::tresult PLUGIN_API endEdit(vst::ParamID id) override {
        require(id == 0, "unexpected ended parameter");
        ++ends; return callback(Stage::end);
    }
    sdk::tresult PLUGIN_API restartComponent(sdk::int32) override { return sdk::kResultOk; }

    void pump(bool allow_timers = true) {
        const auto event_snapshot = events;
        for (const auto& e : event_snapshot) {
            if (std::none_of(events.begin(), events.end(), [&](const auto& live) {
                return live.generation == e.generation;
            })) { continue; }
            // XCB may already have buffered packets while servicing a reply.
            // Its registered native handler drains nonblocking queued events.
            e.handler->onFDIsSet(e.fd);
        }
        if (!allow_timers) { return; }
        const auto timer_snapshot = timers;
        for (const auto& t : timer_snapshot) {
            const auto live = std::find_if(timers.begin(), timers.end(), [&](const auto& entry) {
                return entry.generation == t.generation;
            });
            if (live == timers.end() || live->next > Clock::now()) { continue; }
            live->next = Clock::now() + live->interval;
            if (t.interval == 1ms) { ++close_ticks; }
            t.handler->onTimer();
        }
    }
    void until(const std::function<bool()>& done, std::string_view what) {
        const auto deadline = Clock::now() + 2s;
        while (!done()) {
            require(Clock::now() < deadline, what);
            pump();
            const auto result = ::poll(nullptr, 0, 2);
            require(result >= 0 || errno == EINTR, "poll failed");
        }
    }
    bool close_timer_registered() const {
        return std::any_of(timers.begin(), timers.end(), [](const auto& t) {
            return t.interval == 1ms;
        });
    }
    void reset_counts() { begins = performs = ends = 0; order.clear(); }
    sdk::uint32 reference_count() const { return references_; }
private:
    sdk::tresult callback(Stage stage) {
        order.push_back(stage);
        if (hook) { hook(stage); }
        return sdk::kResultOk;
    }
    sdk::uint32 references_ = 1;
    std::uint64_t generation_ = 0;
};

class Fixture {
public:
    explicit Fixture(const char* bundle) {
        connection_ = xcb_connect(nullptr, nullptr);
        require(connection_ && !xcb_connection_has_error(connection_),
                "cannot open DISPLAY; use tools/run_xvfb_test.py");
        const auto screens = xcb_setup_roots_iterator(xcb_get_setup(connection_));
        require(screens.rem > 0, "X11 screen unavailable");
        root_ = screens.data->root;
        parent_ = xcb_generate_id(connection_);
        check(xcb_create_window_checked(connection_, XCB_COPY_FROM_PARENT, parent_, root_,
            0, 0, 1024, 768, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, XCB_COPY_FROM_PARENT,
            0, nullptr), "create native parent");
        check(xcb_map_window_checked(connection_, parent_), "map native parent");
        std::string error;
        module_ = VST3::Hosting::Module::create(bundle, error);
        require(module_ != nullptr, "load bundle: " + error);
        module_->getFactory().setHostContext(static_cast<vst::IHostApplication*>(&host));
        for (const auto& info : module_->getFactory().classInfos()) {
            if (info.category() == kVstComponentControllerClass) {
                require(!controller_, "multiple controller classes");
                controller_ = module_->getFactory().createInstance<vst::IEditController>(info.ID());
            }
        }
        require(controller_ != nullptr, "controller factory instance");
        require(controller_->initialize(static_cast<vst::IHostApplication*>(&host)) == sdk::kResultOk,
                "initialize real controller");
        initialized_ = true;
        require(controller_->setComponentHandler(&host) == sdk::kResultOk, "set component handler");
        view = sdk::owned(controller_->createView(vst::ViewType::kEditor));
        require(view != nullptr, "create real editor");
        require(view->setFrame(&host) == sdk::kResultOk, "set frame/runloop");
        attach();
        require(!host.events.empty(), "native editor did not register event handler");
        // Warm-up a complete mouse gesture and settle focus before arming any
        // removal hook. Hard-coded photo anchor is independent of source geometry.
        button(true, 1); host.until([&] { return host.begins == 1; }, "mouse press not dispatched");
        button(false, 1); host.until([&] { return host.ends == 1; }, "mouse release not dispatched");
        require(host.begins == host.ends && host.performs == 0, "warm-up gesture balance");
        host.reset_counts();
    }
    ~Fixture() { cleanup(); }
    void finish() {
        cleanup();
        require(host.events.empty() && host.timers.empty(), "native registrations survived unload");
        require(host.reference_count() == 1, "plug-in retained host ownership after unload");
    }
    void cleanup() {
        host.hook = {};
        if (view) { (void)view->removed(); (void)view->setFrame(nullptr); view = nullptr; }
        if (controller_) {
            (void)controller_->setComponentHandler(nullptr);
            if (initialized_) { (void)controller_->terminate(); }
            controller_ = nullptr;
        }
        if (module_) { module_->getFactory().setHostContext(nullptr); module_.reset(); }
        if (connection_) {
            if (parent_) { xcb_destroy_window(connection_, parent_); }
            xcb_disconnect(connection_);
            connection_ = nullptr;
            parent_ = XCB_WINDOW_NONE;
        }
    }
    void* parent_handle() const { return reinterpret_cast<void*>(static_cast<std::uintptr_t>(parent_)); }
    void attach() {
        require(view->attached(parent_handle(), sdk::kPlatformTypeX11EmbedWindowID) == sdk::kResultOk,
                "attach editor to real X11 parent");
        host.until([&] {
            const auto tree = xowned(xcb_query_tree_reply(connection_,
                xcb_query_tree(connection_, parent_), nullptr));
            if (!tree || xcb_query_tree_children_length(tree.get()) != 1) { return false; }
            child_ = xcb_query_tree_children(tree.get())[0]; return true;
        }, "native child not created");
        require(exists(child_), "attached native child is not usable");
    }
    bool exists(xcb_window_t window) const {
        xcb_generic_error_t* error = nullptr;
        const auto geometry = xowned(xcb_get_geometry_reply(connection_,
            xcb_get_geometry(connection_, window), &error));
        const auto owned_error = xowned(error);
        require(!xcb_connection_has_error(connection_), "X11 connection error");
        return geometry != nullptr && !owned_error;
    }
    bool child_exists() const { return exists(child_); }
    bool parent_exists() const { return exists(parent_); }
    void require_closed(std::string_view what) {
        // Structural teardown must already be complete. The pinned X11 backend
        // retires its registrations and disconnects its own XCB connection;
        // server-side child destruction is unordered with this host connection.
        require(host.events.empty() && host.timers.empty(),
                "structural teardown retained native registrations");
        const auto deadline = Clock::now() + 2s;
        while (child_exists()) {
            require(Clock::now() < deadline, what);
            const auto result = ::poll(nullptr, 0, 2);
            require(result >= 0 || errno == EINTR, "close observation poll failed");
        }
    }
    void destroy_parent() {
        check(xcb_destroy_window_checked(connection_, parent_), "destroy parent after removal");
        parent_ = XCB_WINDOW_NONE;
    }
    void button(bool down, std::uint8_t detail) {
        xcb_button_press_event_t event{};
        event.response_type = down ? XCB_BUTTON_PRESS : XCB_BUTTON_RELEASE;
        // Separate independent button presses beyond X11Frame's double-click
        // interval, without making the test wait in wall-clock time.
        timestamp_ += down ? 300 : 1;
        event.detail = detail; event.time = timestamp_;
        event.root = root_; event.event = child_; event.same_screen = 1;
        event.event_x = 213; event.event_y = 305;
        event.root_x = event.event_x; event.root_y = event.event_y;
        event.state = down ? 0 : XCB_BUTTON_MASK_1;
        send(event, down ? XCB_EVENT_MASK_BUTTON_PRESS : XCB_EVENT_MASK_BUTTON_RELEASE);
    }
    void right_key() {
        const auto* setup = xcb_get_setup(connection_);
        const auto count = static_cast<std::uint8_t>(setup->max_keycode - setup->min_keycode + 1);
        const auto mapping = xowned(xcb_get_keyboard_mapping_reply(connection_,
            xcb_get_keyboard_mapping(connection_, setup->min_keycode, count), nullptr));
        require(mapping != nullptr, "read keyboard mapping");
        const auto* symbols = xcb_get_keyboard_mapping_keysyms(mapping.get());
        xcb_keycode_t code = 0;
        for (unsigned offset = 0; offset < count; ++offset) {
            if (symbols[offset * mapping->keysyms_per_keycode] == XK_Right) {
                code = static_cast<xcb_keycode_t>(setup->min_keycode + offset); break;
            }
        }
        require(code != 0, "Right key missing");
        for (const bool down : {true, false}) {
            xcb_key_press_event_t event{};
            event.response_type = down ? XCB_KEY_PRESS : XCB_KEY_RELEASE;
            event.detail = code; event.time = ++timestamp_;
            event.root = root_; event.event = child_; event.same_screen = 1;
            send(event, down ? XCB_EVENT_MASK_KEY_PRESS : XCB_EVENT_MASK_KEY_RELEASE);
        }
    }
    Host host;
    sdk::IPtr<sdk::IPlugView> view;
private:
    void check(xcb_void_cookie_t cookie, std::string_view what) {
        const auto error = xowned(xcb_request_check(connection_, cookie));
        require(!error && !xcb_connection_has_error(connection_), what);
    }
    template<class Packet> void send(const Packet& event, std::uint32_t mask) {
        check(xcb_send_event_checked(connection_, false, child_, mask,
            reinterpret_cast<const char*>(&event)), "send native event");
        require(xcb_flush(connection_) > 0, "flush native input");
    }
    VST3::Hosting::Module::Ptr module_;
    sdk::IPtr<vst::IEditController> controller_;
    xcb_connection_t* connection_ = nullptr;
    xcb_window_t root_ = XCB_WINDOW_NONE, parent_ = XCB_WINDOW_NONE, child_ = XCB_WINDOW_NONE;
    xcb_timestamp_t timestamp_ = 1000;
    bool initialized_ = false;
};

void run(const char* bundle, std::string_view selector) {
    const std::vector<std::string_view> supported{"normal_close", "remove_begin", "remove_perform",
        "remove_end", "nested_pump", "timer_refused", "repeated_remove", "reopen",
        "parent_destroy_after_remove", "release_host_reference_after_remove", "focus_remove"};
    require(std::find(supported.begin(), supported.end(), selector) != supported.end(),
            "unknown lifecycle selector");
    Fixture fixture{bundle};
    auto& host = fixture.host;
    if (selector == "normal_close") {
        fixture.button(true, 1);
        host.until([&] { return host.begins == 1; }, "normal press not dispatched");
        require(fixture.view->removed() == sdk::kResultOk, "ordinary removed failed");
        fixture.require_closed("ordinary close left a native child");
        require(host.ends == 1 && host.performs == 0, "ordinary close gesture balance");
        require(!host.close_timer_registered(), "ordinary close scheduled deferred timer");
        fixture.finish();
        return;
    }
    const Stage trigger = selector == "remove_perform" ? Stage::perform :
                          selector == "remove_end" || selector == "focus_remove" ? Stage::end :
                          Stage::begin;
    bool hooked = false;
    if (selector == "focus_remove") {
        fixture.button(true, 1);
        host.until([&] { return host.begins == 1; }, "focus gesture not started");
    }
    host.hook = [&](Stage stage) {
        if (hooked || stage != trigger) { return; }
        hooked = true;
        host.refuse_close_timer = selector == "timer_refused";
        const auto result = fixture.view->removed();
        host.refuse_close_timer = false;
        require(result == (selector == "timer_refused" ? sdk::kResultFalse : sdk::kResultOk),
                "synchronous public removed returned unexpected result");
        require(fixture.child_exists() && fixture.parent_exists(),
                "native attachment destroyed while its callback is active");
        if (selector == "timer_refused") {
            require(host.refused == 1 && !host.close_timer_registered(),
                    "refused actual close timer was not observed or leaked registration");
            sdk::ViewRect rect;
            require(fixture.view->getSize(&rect) == sdk::kResultOk && rect.getWidth() == 1024,
                    "refused close left public view unusable");
            return;
        }
        require(host.close_timer_registered(), "successful removal did not register close timer");
        if (selector == "repeated_remove") {
            require(fixture.view->removed() == sdk::kResultOk, "repeated pending removal failed");
            require(std::count_if(host.timers.begin(), host.timers.end(), [](const auto& t) {
                return t.interval == 1ms;
            }) == 1, "repeated removal registered duplicate close timers");
        }
        if (selector == "reopen") {
            require(fixture.view->attached(fixture.parent_handle(), sdk::kPlatformTypeX11EmbedWindowID)
                    == sdk::kResultFalse, "attachment succeeded while close was pending");
        }
        if (selector == "nested_pump") {
            const auto before = host.close_ticks;
            host.until([&] { return host.close_ticks >= before + 2; }, "nested timer did not fire");
            require(fixture.child_exists() && host.close_timer_registered(),
                    "nested timer tore down active native callback");
        }
        if (selector == "parent_destroy_after_remove") { fixture.destroy_parent(); }
        if (selector == "release_host_reference_after_remove") { fixture.view = nullptr; }
    };
    if (selector == "remove_perform") { fixture.button(true, 4); }
    else if (selector == "remove_end") { fixture.right_key(); }
    else if (selector == "focus_remove") {
        require(fixture.view->onFocus(false) == sdk::kResultOk, "public focus-loss dispatch failed");
    } else { fixture.button(true, 1); }
    // Suppress timer dispatch until the native event callback has returned so
    // its continued native attachment and canceled interaction can be inspected.
    const auto dispatch_deadline = Clock::now() + 2s;
    while (!hooked) {
        require(Clock::now() < dispatch_deadline, "host removal hook was not reached");
        host.pump(false);
        (void)::poll(nullptr, 0, 2);
    }
    host.hook = {};
    require(host.begins == 1 && host.ends == 1, "removal did not balance exactly one accepted gesture");
    require(host.performs == (trigger == Stage::perform || selector == "remove_end" ? 1U : 0U),
            "unexpected perform count after canceled interaction");
    require(host.order.front() == Stage::begin && host.order.back() == Stage::end,
            "gesture ordering was not begin through end");
    if (selector == "timer_refused") {
        require(fixture.parent_exists() && fixture.child_exists(), "refusal lost native attachment");
        host.reset_counts();
        fixture.button(true, 4);
        host.until([&] { return host.ends == 1; }, "refused removal disabled fresh native input");
        require(host.begins == 1 && host.performs == 1,
                "fresh gesture after refused removal was not balanced");
        // Later ordinary close must still remove the same attached child. A
        // premature SDK parent clear would strand it and fail this assertion.
        require(fixture.view->removed() == sdk::kResultOk, "ordinary retry after timer refusal failed");
        fixture.require_closed("retry after timer refusal stranded native child");
        fixture.finish();
        return;
    }
    if (selector != "parent_destroy_after_remove") {
        require(fixture.child_exists(), "teardown happened before the safe timer turn");
    }
    if (fixture.view && selector != "parent_destroy_after_remove") {
        // Pending removal permanently disables interaction for this attachment.
        const auto completed_performs = host.performs;
        fixture.button(true, 4); fixture.right_key(); host.pump(false);
        require(host.begins == 1 && host.ends == 1, "pending editor accepted a new gesture");
        require(host.performs == completed_performs, "pending editor performed an unbracketed edit");
    }
    host.until([&] { return !host.close_timer_registered(); }, "deferred close timer did not retire");
    require(host.close_ticks > 0, "no actual UI timer turn completed removal");
    fixture.require_closed("safe timer failed to destroy native child");
    require(host.begins == 1 && host.ends == 1, "deferred teardown repeated gesture callbacks");
    if (selector == "reopen") {
        fixture.attach(); host.reset_counts(); fixture.button(true, 4);
        host.until([&] { return host.ends == 1; }, "reopened editor did not accept native input");
        require(host.begins == 1 && host.performs == 1, "reopened gesture not balanced");
        require(fixture.view->removed() == sdk::kResultOk, "reopened ordinary removal failed");
        fixture.require_closed("reopened child survived ordinary removal");
    }
    fixture.finish();
}

void watchdog(int) {
    constexpr char message[] = "FAIL native photo lifecycle: 20-second watchdog expired\n";
    const auto ignored = ::write(STDERR_FILENO, message, sizeof(message) - 1);
    (void)ignored; ::_exit(1);
}
} // namespace

int main(int argc, char** argv) {
    if (std::signal(SIGALRM, watchdog) == SIG_ERR) { return 1; }
    ::alarm(20);
    try {
        require(argc == 3, "usage: vst3_photo_lifecycle_test BUNDLE SELECTOR");
        run(argv[1], argv[2]);
        ::alarm(0);
        std::cout << "PASS native photo lifecycle " << argv[2] << '\n';
        return 0;
    } catch (const std::exception& error) {
        ::alarm(0);
        std::cerr << "FAIL native photo lifecycle: " << error.what() << '\n';
        return 1;
    }
}
