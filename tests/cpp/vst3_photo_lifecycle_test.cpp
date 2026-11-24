// Linux native lifecycle regression. Only SDK hosting/public interfaces are
// linked; the editor and VSTGUI execute exclusively in the loaded real bundle.
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"

#include <X11/keysym.h>
#include <xcb/xcb.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
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
    unsigned refused = 0, close_ticks = 0, redraw_ticks = 0;
    unsigned begins = 0, performs = 0, ends = 0;
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
            if (t.interval == 16ms) { ++redraw_ticks; }
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
        try {
            connection_ = xcb_connect(nullptr, nullptr);
            require(connection_ && !xcb_connection_has_error(connection_),
                    "cannot open DISPLAY; use tools/run_xvfb_test.py");
            const auto screens = xcb_setup_roots_iterator(xcb_get_setup(connection_));
            require(screens.rem > 0, "X11 screen unavailable");
            root_ = screens.data->root;
            parent_ = create_parent();
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
        } catch (...) {
            cleanup();
            throw;
        }
    }
    ~Fixture() { cleanup(); }
    void finish() {
        cleanup();
        require(host.events.empty() && host.timers.empty(), "native registrations survived unload");
        require(host.reference_count() == 1, "plug-in retained host ownership after unload");
    }
    void cleanup() {
        host.hook = {};
        if (sibling_) {
            if (sibling_->view) {
                (void)sibling_->view->removed();
                (void)sibling_->view->setFrame(nullptr);
                sibling_->view = nullptr;
            }
            if (sibling_->controller) {
                (void)sibling_->controller->setComponentHandler(nullptr);
                if (sibling_->initialized) { (void)sibling_->controller->terminate(); }
                sibling_->controller = nullptr;
            }
            if (connection_ && sibling_->parent) {
                xcb_destroy_window(connection_, sibling_->parent);
            }
            sibling_.reset();
        }
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
        attach_editor(view.get(), parent_, child_);
    }
    void open_sibling() {
        require(!sibling_, "sibling editor already exists");
        sibling_ = std::make_unique<Sibling>();
        sibling_->parent = create_parent();
        // The module's native run loop is a singleton. Both independent
        // controllers must share this exact module and host, not two Fixtures.
        for (const auto& info : module_->getFactory().classInfos()) {
            if (info.category() == kVstComponentControllerClass) {
                require(!sibling_->controller, "multiple sibling controller classes");
                sibling_->controller = module_->getFactory().createInstance<vst::IEditController>(info.ID());
            }
        }
        require(sibling_->controller != nullptr, "sibling controller factory instance");
        require(sibling_->controller->initialize(static_cast<vst::IHostApplication*>(&host))
                == sdk::kResultOk, "initialize sibling controller");
        sibling_->initialized = true;
        require(sibling_->controller->setComponentHandler(&host) == sdk::kResultOk,
                "set sibling component handler");
        sibling_->view = sdk::owned(sibling_->controller->createView(vst::ViewType::kEditor));
        require(sibling_->view != nullptr, "create sibling editor");
        require(sibling_->view->setFrame(&host) == sdk::kResultOk, "set sibling frame/runloop");
        attach_editor(sibling_->view.get(), sibling_->parent, sibling_->child);
    }
    void repaint_sibling() {
        require(sibling_ && exists(sibling_->child), "sibling child no longer exists");
        await_photo(sibling_->child);
    }
    void require_primary_closed_with_sibling() {
        require(sibling_ && exists(sibling_->child), "primary removal destroyed sibling child");
        require(!host.events.empty() && !host.timers.empty(),
                "primary removal retired sibling registrations");
        require(!host.close_timer_registered(), "primary removal retained a close timer");
        require_child_destroyed(child_, "primary child survived removal with sibling open");
    }
    void close_sibling() {
        require(sibling_ && sibling_->view->removed() == sdk::kResultOk,
                "sibling ordinary removal failed");
        require(host.events.empty() && host.timers.empty(),
                "last sibling removal retained native registrations");
        require_child_destroyed(sibling_->child, "sibling native child survived removal");
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
        require_child_destroyed(child_, what);
    }
    void destroy_parent() {
        check(xcb_destroy_window_checked(connection_, parent_), "destroy parent after removal");
        parent_ = XCB_WINDOW_NONE;
    }
    void button(bool down, std::uint8_t detail, bool sibling = false) {
        require(!sibling || sibling_, "sibling input without an editor");
        const auto window = sibling ? sibling_->child : child_;
        xcb_button_press_event_t event{};
        event.response_type = down ? XCB_BUTTON_PRESS : XCB_BUTTON_RELEASE;
        // Separate independent button presses beyond X11Frame's double-click
        // interval, without making the test wait in wall-clock time.
        timestamp_ += down ? 300 : 1;
        event.detail = detail; event.time = timestamp_;
        event.root = root_; event.event = window; event.same_screen = 1;
        event.event_x = 213; event.event_y = 305;
        event.root_x = event.event_x; event.root_y = event.event_y;
        event.state = down ? 0 : XCB_BUTTON_MASK_1;
        send(event, down ? XCB_EVENT_MASK_BUTTON_PRESS : XCB_EVENT_MASK_BUTTON_RELEASE, window);
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
    struct Sibling {
        sdk::IPtr<vst::IEditController> controller;
        sdk::IPtr<sdk::IPlugView> view;
        xcb_window_t parent = XCB_WINDOW_NONE, child = XCB_WINDOW_NONE;
        bool initialized = false;
    };
    struct PhotoPatch {
        std::int16_t x, y;
        std::array<unsigned, 3> rgb;
    };
    // Independent 8x8 native patches in the supplied 1536x1152 photograph,
    // displayed at 1024x768: wall, dark lid, right wall, and floor. Rounded
    // RGB means were sampled from assets/reference/rhodes-suitcase-reference.webp
    // over the corresponding 12x12 source rectangles, not from plug-in geometry
    // or a captured editor. Tolerance permits native downsampling, while these
    // distinct gray levels reject an unpainted or uniformly colored drawable.
    static constexpr std::array<PhotoPatch, 4> photo_patches{{
        {80, 80, {167, 163, 159}}, {350, 250, {16, 18, 22}},
        {960, 180, {182, 180, 176}}, {140, 670, {190, 188, 186}}
    }};
    using PatchSums = std::array<std::array<unsigned, 3>, photo_patches.size()>;
    xcb_window_t create_parent() {
        const auto parent = xcb_generate_id(connection_);
        check(xcb_create_window_checked(connection_, XCB_COPY_FROM_PARENT, parent, root_,
            0, 0, 1024, 768, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, XCB_COPY_FROM_PARENT,
            0, nullptr), "create native parent");
        check(xcb_map_window_checked(connection_, parent), "map native parent");
        return parent;
    }
    void attach_editor(sdk::IPlugView* editor, xcb_window_t parent, xcb_window_t& child) {
        const auto handle = reinterpret_cast<void*>(static_cast<std::uintptr_t>(parent));
        require(editor->attached(handle, sdk::kPlatformTypeX11EmbedWindowID) == sdk::kResultOk,
                "attach editor to real X11 parent");
        host.until([&] {
            const auto tree = xowned(xcb_query_tree_reply(connection_,
                xcb_query_tree(connection_, parent), nullptr));
            if (!tree || xcb_query_tree_children_length(tree.get()) != 1) { return false; }
            child = xcb_query_tree_children(tree.get())[0]; return true;
        }, "native child not created");
        require(exists(child), "attached native child is not usable");
        await_photo(child);
    }
    void require_child_destroyed(xcb_window_t child, std::string_view what) {
        // Observe only server geometry here: pumping retired work must not be
        // necessary to finish structural teardown, including with a live sibling.
        const auto deadline = Clock::now() + 2s;
        while (exists(child)) {
            require(Clock::now() < deadline, what);
            const auto result = ::poll(nullptr, 0, 2);
            require(result >= 0 || errno == EINTR, "close observation poll failed");
        }
    }
    PatchSums read_patches(xcb_window_t window) {
        std::array<xcb_get_image_cookie_t, photo_patches.size()> cookies{};
        for (std::size_t i = 0; i < photo_patches.size(); ++i) {
            cookies[i] = xcb_get_image(connection_, XCB_IMAGE_FORMAT_Z_PIXMAP,
                window, photo_patches[i].x, photo_patches[i].y, 8, 8, 0xffffffffU);
        }
        PatchSums sums{};
        const bool lsb = xcb_get_setup(connection_)->image_byte_order == XCB_IMAGE_ORDER_LSB_FIRST;
        for (std::size_t i = 0; i < cookies.size(); ++i) {
            xcb_generic_error_t* error = nullptr;
            const auto pixels = xowned(xcb_get_image_reply(connection_, cookies[i], &error));
            const auto owned_error = xowned(error);
            require(pixels && !owned_error && pixels->depth == 24 &&
                    xcb_get_image_data_length(pixels.get()) == 8 * 8 * 4,
                    "read 24-bit native photograph patch");
            const auto* bytes = xcb_get_image_data(pixels.get());
            for (unsigned pixel = 0; pixel < 8 * 8; ++pixel) {
                const auto* rgb = bytes + pixel * 4;
                sums[i][0] += rgb[lsb ? 2 : 1];
                sums[i][1] += rgb[lsb ? 1 : 2];
                sums[i][2] += rgb[lsb ? 0 : 3];
            }
        }
        return sums;
    }
    static bool matches_photo(const PatchSums& sums) {
        for (std::size_t i = 0; i < photo_patches.size(); ++i) {
            for (std::size_t channel = 0; channel < 3; ++channel) {
                const int delta = static_cast<int>(sums[i][channel]) -
                    static_cast<int>(photo_patches[i].rgb[channel] * 64);
                if (std::abs(delta) > 8 * 64) { return false; }
            }
        }
        return true;
    }
    void await_photo(xcb_window_t window) {
        // XEmbed maps the child in response to its property event. Geometry
        // alone does not establish that XGetImage is legal yet.
        host.until([&] {
            const auto attributes = xowned(xcb_get_window_attributes_reply(connection_,
                xcb_get_window_attributes(connection_, window), nullptr));
            return attributes && attributes->map_state == XCB_MAP_STATE_VIEWABLE;
        }, "native photograph child did not become viewable");
        const auto geometry = xowned(xcb_get_geometry_reply(connection_,
            xcb_get_geometry(connection_, window), nullptr));
        require(geometry && geometry->width == 1024 && geometry->height == 768 &&
                geometry->depth == 24, "native photo child must be 1024x768 at 24-bit depth");
        const auto attributes = xowned(xcb_get_window_attributes_reply(connection_,
            xcb_get_window_attributes(connection_, window), nullptr));
        require(attributes != nullptr, "read native photo visual");
        bool rgb_visual = false;
        const auto screens = xcb_setup_roots_iterator(xcb_get_setup(connection_));
        for (auto depths = xcb_screen_allowed_depths_iterator(screens.data); depths.rem;
             xcb_depth_next(&depths)) {
            for (auto visuals = xcb_depth_visuals_iterator(depths.data); visuals.rem;
                 xcb_visualtype_next(&visuals)) {
                const auto& v = *visuals.data;
                if (v.visual_id == attributes->visual) {
                    rgb_visual = v._class == XCB_VISUAL_CLASS_TRUE_COLOR &&
                        v.red_mask == 0xff0000 && v.green_mask == 0x00ff00 && v.blue_mask == 0x0000ff;
                }
            }
        }
        require(rgb_visual, "native photo drawable must use a true-color RGB visual");
        // Establish a known unpainted state, even if the X server recycled old
        // drawable pixels. An Expose must then produce the actual photo through
        // the plug-in's native redraw timer before removal can be armed.
        const auto gc = xcb_generate_id(connection_);
        const std::uint32_t black = 0;
        check(xcb_create_gc_checked(connection_, gc, window, XCB_GC_FOREGROUND, &black),
              "create native paint-observation GC");
        const xcb_rectangle_t rectangle{0, 0, 1024, 768};
        check(xcb_poly_fill_rectangle_checked(connection_, window, gc, 1, &rectangle),
              "clear native paint-observation surface");
        check(xcb_free_gc_checked(connection_, gc), "free native paint-observation GC");
        const auto blank = read_patches(window);
        require(std::all_of(blank.begin(), blank.end(), [](const auto& channels) {
            return channels == std::array<unsigned, 3>{0, 0, 0};
        }), "native paint-observation surface was not unpainted");
        xcb_expose_event_t expose{};
        expose.response_type = XCB_EXPOSE; expose.window = window;
        expose.width = 1024; expose.height = 768;
        const auto before = host.redraw_ticks;
        send(expose, XCB_EVENT_MASK_EXPOSURE, window);
        host.until([&] {
            return host.redraw_ticks > before && matches_photo(read_patches(window));
        }, "native redraw did not paint the known photograph patches");
    }
    void check(xcb_void_cookie_t cookie, std::string_view what) {
        const auto error = xowned(xcb_request_check(connection_, cookie));
        require(!error && !xcb_connection_has_error(connection_), what);
    }
    template<class Packet> void send(const Packet& event, std::uint32_t mask,
                                    xcb_window_t window = XCB_WINDOW_NONE) {
        if (window == XCB_WINDOW_NONE) { window = child_; }
        // XSendEvent always reads 32 wire bytes; Expose's C struct is shorter.
        std::array<char, 32> packet{};
        static_assert(sizeof(Packet) <= 32);
        std::memcpy(packet.data(), &event, sizeof(Packet));
        check(xcb_send_event_checked(connection_, false, window, mask,
            packet.data()), "send native event");
        require(xcb_flush(connection_) > 0, "flush native input");
    }
    VST3::Hosting::Module::Ptr module_;
    sdk::IPtr<vst::IEditController> controller_;
    std::unique_ptr<Sibling> sibling_;
    xcb_connection_t* connection_ = nullptr;
    xcb_window_t root_ = XCB_WINDOW_NONE, parent_ = XCB_WINDOW_NONE, child_ = XCB_WINDOW_NONE;
    xcb_timestamp_t timestamp_ = 1000;
    bool initialized_ = false;
};

void run(const char* bundle, std::string_view selector) {
    const std::vector<std::string_view> supported{"normal_close", "remove_begin", "remove_perform",
        "remove_end", "nested_pump", "timer_refused", "repeated_remove", "reopen",
        "parent_destroy_after_remove", "release_host_reference_after_remove", "focus_remove",
        "reopen_cycles", "sibling_survives"};
    require(std::find(supported.begin(), supported.end(), selector) != supported.end(),
            "unknown lifecycle selector");
    Fixture fixture{bundle};
    auto& host = fixture.host;
    if (selector == "reopen_cycles") {
        const auto* public_view = fixture.view.get();
        for (unsigned cycle = 0; cycle < 10; ++cycle) {
            require(fixture.view->removed() == sdk::kResultOk, "cycle ordinary removal failed");
            fixture.require_closed("cycle removal left a native child");
            fixture.attach(); // Each reuse must finish an observed native photo paint.
            require(fixture.view.get() == public_view, "cycle replaced the public view");
            host.reset_counts();
            fixture.button(true, 4);
            host.until([&] { return host.ends == 1; }, "cycle reopened input not dispatched");
            require(host.begins == 1 && host.performs == 1 && host.ends == 1,
                    "cycle reopened gesture not balanced");
            require(host.order == std::vector<Stage>{Stage::begin, Stage::perform, Stage::end},
                    "cycle reopened gesture order");
        }
        require(fixture.view->removed() == sdk::kResultOk, "final cycle removal failed");
        fixture.require_closed("final cycle left a native child");
        require(host.begins == 1 && host.performs == 1 && host.ends == 1,
                "final cycle removal repeated gesture callbacks");
        fixture.finish();
        return;
    }
    if (selector == "sibling_survives") {
        require(host.events.size() == 1, "primary must register one shared native connection");
        const auto connection_generation = host.events.front().generation;
        fixture.open_sibling();
        require(host.events.size() == 1 && host.events.front().generation == connection_generation,
                "sibling did not reuse the same registered native connection");
        const auto redraw_count = [&] {
            return std::count_if(host.timers.begin(), host.timers.end(), [](const auto& timer) {
                return timer.interval == 16ms;
            });
        };
        require(redraw_count() == 2, "both native editors must register redraw timers");
        require(fixture.view->removed() == sdk::kResultOk, "primary removal with sibling failed");
        fixture.require_primary_closed_with_sibling();
        require(host.events.size() == 1 && host.events.front().generation == connection_generation,
                "primary removal replaced or retired the sibling connection");
        require(redraw_count() == 1, "primary removal did not retire exactly its redraw timer");
        fixture.repaint_sibling();
        host.reset_counts();
        fixture.button(true, 4, true);
        host.until([&] { return host.ends == 1; }, "surviving sibling input not dispatched");
        require(host.begins == 1 && host.performs == 1 && host.ends == 1,
                "surviving sibling gesture not balanced");
        require(host.order == std::vector<Stage>{Stage::begin, Stage::perform, Stage::end},
                "surviving sibling gesture order");
        fixture.close_sibling();
        require(host.begins == 1 && host.performs == 1 && host.ends == 1,
                "sibling removal repeated gesture callbacks");
        fixture.finish();
        return;
    }
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
