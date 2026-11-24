#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

// SDK-only host test: production code is reached exclusively through loaded
// module factories. The public editor contract below is independent of the
// production geometry, editor classes and VSTGUI headers. No view is attached:
// passing establishes neither native-window behavior nor rendered appearance.
namespace {
namespace sdk = Steinberg;
namespace vst = Steinberg::Vst;
using Module = VST3::Hosting::Module;
using Scale = sdk::IPlugViewContentScaleSupport::ScaleFactor;

struct Identity {
    const char* name;
    const char* controller;
};

constexpr std::array<Identity, 3> identities{{
    {"direct", "EA801E8B97864ED9B3A0E6572D81CE61"},
    {"filtered", "0569EDDBD21D4568A9ED52C3D8545B6D"},
    {"quadrature", "A45F23E1B68A4D79AF31472C8E909D52"},
}};

constexpr std::size_t controllers_per_module = 2;
constexpr std::size_t controller_count = identities.size() * controllers_per_module;
const sdk::ViewRect default_size{0, 0, 1024, 768};
std::string context;
std::string current_case;
std::size_t checks = 0;
std::size_t controllers_initialized = 0;
std::size_t views_created = 0;
std::size_t rejected_scales = 0;
std::size_t host_resize_requests = 0;

void require(bool condition, std::string_view message) {
    ++checks;
    if (!condition) {
        throw std::runtime_error(context + "/" + current_case + ": " + std::string{message});
    }
}

bool same_rect(const sdk::ViewRect& a, const sdk::ViewRect& b) {
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

std::int64_t width(const sdk::ViewRect& rect) {
    return static_cast<std::int64_t>(rect.right) - rect.left;
}

std::int64_t height(const sdk::ViewRect& rect) {
    return static_cast<std::int64_t>(rect.bottom) - rect.top;
}

sdk::ViewRect size_of(sdk::IPlugView& view) {
    sdk::ViewRect result{-1, -2, -3, -4};
    require(view.getSize(&result) == sdk::kResultOk, "read unattached view size");
    return result;
}

sdk::IPtr<sdk::IPlugViewContentScaleSupport> scaling_of(sdk::IPlugView& view) {
    sdk::IPlugViewContentScaleSupport* queried = nullptr;
    const auto result = view.queryInterface(sdk::IPlugViewContentScaleSupport::iid,
                                            reinterpret_cast<void**>(&queried));
    auto scaling = sdk::owned(queried);
    require(result == sdk::kResultOk && scaling != nullptr, "public content-scaling interface");
    return scaling;
}

class RecordingFrame final : public sdk::IPlugFrame {
public:
    enum class Response { accept, refuse, omit_on_size, invalid_on_size, refuse_after_on_size };

    explicit RecordingFrame(sdk::IPlugView& view) : view_(view) {
        require(view_.setFrame(this) == sdk::kResultOk, "provide host frame before native attachment");
    }

    ~RecordingFrame() { (void)view_.setFrame(nullptr); }

    sdk::tresult PLUGIN_API queryInterface(const sdk::TUID interface_id, void** object) override {
        if (object == nullptr) { return sdk::kInvalidArgument; }
        *object = nullptr;
        if (sdk::FUnknownPrivate::iidEqual(interface_id, sdk::IPlugFrame::iid) ||
            sdk::FUnknownPrivate::iidEqual(interface_id, sdk::FUnknown::iid)) {
            *object = static_cast<sdk::IPlugFrame*>(this);
            addRef();
            return sdk::kResultOk;
        }
        return sdk::kNoInterface;
    }

    sdk::uint32 PLUGIN_API addRef() override { return ++references_; }
    sdk::uint32 PLUGIN_API release() override { return --references_; }

    sdk::tresult PLUGIN_API resizeView(sdk::IPlugView* view, sdk::ViewRect* size) override {
        ++calls;
        ++host_resize_requests;
        if (inside_resize_) {
            recursive_request = true;
            return sdk::kResultFalse;
        }
        inside_resize_ = true;
        correct_view = view == &view_;
        requested = *size;
        before_result = view->getSize(&before);
        on_size_result = sdk::kNotImplemented;
        sdk::tresult result = sdk::kResultFalse;
        if (response == Response::accept || response == Response::invalid_on_size ||
            response == Response::refuse_after_on_size) {
            auto notified_size = *size;
            if (response == Response::invalid_on_size) { ++notified_size.bottom; }
            ++on_size_calls;
            on_size_result = view->onSize(&notified_size);
            result = response == Response::refuse_after_on_size ? sdk::kResultFalse : on_size_result;
        } else if (response == Response::omit_on_size) {
            result = sdk::kResultTrue;
        }
        after_result = view->getSize(&after);
        inside_resize_ = false;
        return result;
    }

    Response response = Response::accept;
    std::size_t calls = 0;
    std::size_t on_size_calls = 0;
    sdk::ViewRect requested{};
    sdk::ViewRect before{};
    sdk::ViewRect after{};
    sdk::tresult before_result = sdk::kNotImplemented;
    sdk::tresult after_result = sdk::kNotImplemented;
    sdk::tresult on_size_result = sdk::kNotImplemented;
    bool correct_view = false;
    bool recursive_request = false;

private:
    sdk::IPlugView& view_;
    // Stack lifetime is bound to view_ through setFrame; detach before destruction.
    sdk::uint32 references_ = 1;
    bool inside_resize_ = false;
};

void expect_no_view(vst::IEditController& controller, const char* name) {
    // Own even an unexpected result so a failed assertion does not leak a view.
    const auto unexpected = sdk::owned(controller.createView(name));
    require(unexpected == nullptr, "reject unavailable or unknown view name");
}

class ControllerInstance {
    // The module outlives every interface, including on assertion failures.
    Module::Ptr module_;
    bool initialized_ = false;
    sdk::IPtr<vst::IEditController> controller_;
    std::array<sdk::IPtr<sdk::IPlugView>, 2> views_;

    void release_views() {
        for (auto& view : views_) { view = nullptr; }
    }

public:
    ControllerInstance() = default;
    ControllerInstance(const ControllerInstance&) = delete;
    ControllerInstance& operator=(const ControllerInstance&) = delete;

    ~ControllerInstance() {
        release_views();
        if (initialized_) { (void)controller_->terminate(); }
        controller_ = nullptr;
    }

    void open(const Module::Ptr& module, const Identity& identity, vst::HostApplication& host) {
        module_ = module;
        require(module_->getFactory().classCount() == 2, "two exported factory classes");
        for (const auto& info : module_->getFactory().classInfos()) {
            if (info.category() != kVstComponentControllerClass) { continue; }
            require(info.ID().toString() == identity.controller, "frozen controller class identity");
            require(controller_ == nullptr, "one controller factory class");
            controller_ = module_->getFactory().createInstance<vst::IEditController>(info.ID());
        }
        require(controller_ != nullptr, "instantiate controller through real module factory");
        expect_no_view(*controller_, vst::ViewType::kEditor);
        require(controller_->initialize(&host) == sdk::kResultOk, "initialize with SDK HostApplication");
        initialized_ = true;
        ++controllers_initialized;
        for (const auto* name : std::array<const char*, 4>{{nullptr, "", "Editor", "Rhodes.UnknownView"}}) {
            expect_no_view(*controller_, name);
        }
        replace_view(0);
        replace_view(1);
        require(views_[0].get() != views_[1].get(), "distinct simultaneous views on one controller");
    }

    void replace_view(std::size_t index) {
        views_[index] = nullptr;
        views_[index] = sdk::owned(controller_->createView(vst::ViewType::kEditor));
        require(views_[index] != nullptr, "GUI bundle creates kEditor before attachment");
        ++views_created;
        require(same_rect(size_of(*views_[index]), default_size), "new view defaults to 1024 by 768");
    }

    void close() {
        // Host order matters: release views and their queried interfaces before
        // controller termination, then release the controller before the module.
        release_views();
        require(controller_->terminate() == sdk::kResultOk, "terminate after all views are released");
        initialized_ = false;
        expect_no_view(*controller_, vst::ViewType::kEditor);
        controller_ = nullptr;
        module_.reset();
    }

    vst::IEditController& controller() { return *controller_; }
    sdk::IPlugView& view(std::size_t index) { return *views_[index]; }
};

const char* native_platform() {
#if defined(_WIN32)
    return sdk::kPlatformTypeHWND;
#elif defined(__APPLE__)
    return sdk::kPlatformTypeNSView;
#elif defined(__linux__)
    return sdk::kPlatformTypeX11EmbedWindowID;
#else
#error The loaded photo-editor contract requires a supported native module platform.
#endif
}

void basic_view_contract(sdk::IPlugView& view) {
    current_case = "unattached interface";
    require(same_rect(size_of(view), default_size), "default full-composition aspect and size");
    require(view.canResize() == sdk::kResultTrue, "host resizing is supported");
    require(view.isPlatformTypeSupported(native_platform()) == sdk::kResultTrue,
            "advertise this platform's native parent type");
    require(view.isPlatformTypeSupported("Rhodes.UnknownPlatform") != sdk::kResultOk,
            "reject an unknown native parent type");
    require(view.isPlatformTypeSupported("") != sdk::kResultOk, "reject empty native parent type");
    require(view.getSize(nullptr) == sdk::kInvalidArgument, "reject null size output");
    require(view.checkSizeConstraint(nullptr) == sdk::kInvalidArgument, "reject null constraint rect");
    require(view.onSize(nullptr) == sdk::kInvalidArgument, "reject null resize rect");
    require(view.onFocus(true) == sdk::kResultOk && view.onFocus(false) == sdk::kResultOk,
            "unattached focus transitions are safe");
    require(same_rect(size_of(view), default_size), "rejections and focus preserve current geometry");
}

sdk::ViewRect constrain(sdk::IPlugView& view, sdk::ViewRect requested,
                        std::int64_t minimum_width, std::int64_t maximum_width) {
    const auto old_size = size_of(view);
    const auto origin = requested;
    require(view.checkSizeConstraint(&requested) == sdk::kResultOk, "constrain proposed rectangle");
    require(requested.left == origin.left && requested.top == origin.top,
            "constraints preserve the requested origin");
    const auto w = width(requested);
    const auto h = height(requested);
    require(w >= minimum_width && w <= maximum_width, "width remains within declared limits");
    require(h > 0 && 3 * w == 4 * h, "exact integer 4:3 composition at every accepted size");
    auto repeated = requested;
    require(view.checkSizeConstraint(&repeated) == sdk::kResultOk && same_rect(repeated, requested),
            "a constrained rectangle is stable on repeated host queries");
    require(same_rect(size_of(view), old_size), "constraint query does not resize the view");
    return requested;
}

void resize_to(sdk::IPlugView& view, sdk::ViewRect requested) {
    const auto input = requested;
    require(view.onSize(&requested) == sdk::kResultOk, "accept host's constrained resize");
    require(same_rect(requested, input), "resize does not rewrite caller input");
    require(same_rect(size_of(view), input), "getSize reports accepted host resize before attachment");
}

void resize_contract(sdk::IPlugView& view) {
    current_case = "resize constraints";
    struct Fixture {
        sdk::ViewRect request;
        std::int64_t expected_width;
        std::int64_t expected_height;
    };
    // Fixed boundary oracles, not a reimplementation of the editor's rounding.
    const std::array<Fixture, 8> fixtures{{
        {{0, 0, 1, 1}, 768, 576},
        {{0, 0, 768, 576}, 768, 576},
        {{0, 0, 1024, 768}, 1024, 768},
        {{0, 0, 1536, 1152}, 1536, 1152},
        {{0, 0, 8192, 6144}, 1536, 1152},
        {{17, -23, 1041, 1200}, 1024, 768},
        {{0, 0, -1, -1}, 768, 576},
        {{0, 0, 1024, 0}, 1024, 768},
    }};
    for (const auto& fixture : fixtures) {
        auto actual = constrain(view, fixture.request, 768, 1536);
        require(width(actual) == fixture.expected_width && height(actual) == fixture.expected_height,
                "independent boundary/offset rectangle oracle");
        resize_to(view, actual);
    }
    for (const sdk::int32 requested_width : {769, 803, 1001, 1217, 1535}) {
        const sdk::ViewRect request{0, 0, requested_width, 991};
        const auto actual = constrain(view, request, 768, 1536);
        require(width(actual) >= requested_width - 2 && width(actual) <= requested_width + 2,
                "integer aspect correction stays within two pixels of in-range requested width");
    }
    const auto int_min = std::numeric_limits<sdk::int32>::min();
    const auto int_max = std::numeric_limits<sdk::int32>::max();
    const auto wide = constrain(view, {int_min, int_min, int_max, int_max}, 768, 1536);
    require(width(wide) == 1536 && height(wide) == 1152,
            "large coordinate difference clamps without signed overflow");

    resize_to(view, default_size);
    for (auto invalid : std::array<sdk::ViewRect, 4>{{
            {0, 0, 767, 576}, {0, 0, 1537, 1152},
            {0, 0, 1024, 769}, {0, 0, 1025, 768}}}) {
        const auto before = invalid;
        require(view.onSize(&invalid) != sdk::kResultOk, "reject unconstrained host resize");
        require(same_rect(invalid, before), "rejected resize preserves caller rectangle");
        require(same_rect(size_of(view), default_size), "rejected resize preserves view geometry");
    }
    for (auto invalid : std::array<sdk::ViewRect, 2>{{
            {int_max - 100, 0, int_max, 768}, {0, int_max - 100, 1024, int_max}}}) {
        const auto before = invalid;
        require(view.checkSizeConstraint(&invalid) == sdk::kInvalidArgument,
                "reject rectangle whose constrained endpoint cannot be represented");
        require(same_rect(invalid, before), "overflow rejection preserves caller rectangle");
        require(same_rect(size_of(view), default_size), "overflow rejection preserves view geometry");
    }
}

void scale_contract(sdk::IPlugView& view) {
    current_case = "content scaling before attachment";
    auto scaling = scaling_of(view);
    struct Fixture {
        Scale factor;
        sdk::int32 default_width;
        sdk::int32 default_height;
        sdk::int32 minimum_width;
        sdk::int32 maximum_width;
    };
    constexpr std::array<Fixture, 6> fixtures{{
        {0.75F, 768, 576, 576, 1152},
        {1.25F, 1280, 960, 960, 1920},
        {1.5F, 1536, 1152, 1152, 2304},
        {2.0F, 2048, 1536, 1536, 3072},
        {3.0F, 3072, 2304, 2304, 4608},
        {1.0F, 1024, 768, 768, 1536},
    }};
    for (const auto& fixture : fixtures) {
        require(scaling->setContentScaleFactor(fixture.factor) == sdk::kResultTrue,
                "accept representable positive content scale before a host frame exists");
        const sdk::ViewRect expected{0, 0, fixture.default_width, fixture.default_height};
        require(same_rect(size_of(view), expected), "getSize immediately reflects content scaling");
        const auto minimum = constrain(view, {0, 0, 1, 1},
                                       fixture.minimum_width, fixture.maximum_width);
        const auto maximum = constrain(view, {0, 0, 100000, 100000},
                                       fixture.minimum_width, fixture.maximum_width);
        require(width(minimum) == fixture.minimum_width && width(maximum) == fixture.maximum_width,
                "DPI scales both logical resize limits");
    }

    // Test malformed values from a nondefault scale so an erroneous reset to
    // unity cannot masquerade as successful rollback.
    require(scaling->setContentScaleFactor(2.0F) == sdk::kResultTrue, "seed nondefault DPI");
    const auto before = size_of(view);
    const std::array<Scale, 9> invalid{{
        0.0F, -0.0F, -1.0F,
        std::numeric_limits<Scale>::quiet_NaN(),
        std::numeric_limits<Scale>::infinity(),
        -std::numeric_limits<Scale>::infinity(),
        std::numeric_limits<Scale>::max(),
        std::numeric_limits<Scale>::min(),
        std::numeric_limits<Scale>::denorm_min(),
    }};
    for (const auto factor : invalid) {
        ++rejected_scales;
        require(scaling->setContentScaleFactor(factor) == sdk::kInvalidArgument,
                "reject nonfinite, nonpositive or unrepresentable content scale");
        require(same_rect(size_of(view), before), "invalid scale preserves current view size");
        const auto minimum = constrain(view, {0, 0, 1, 1}, 1536, 3072);
        const auto maximum = constrain(view, {0, 0, 100000, 100000}, 1536, 3072);
        require(width(minimum) == 1536 && width(maximum) == 3072,
                "invalid scale preserves the prior scale-dependent constraints");
    }

    require(scaling->setContentScaleFactor(1.0F) == sdk::kResultTrue, "restore unit content scale");
    resize_to(view, {0, 0, 1280, 960});
    require(scaling->setContentScaleFactor(2.0F) == sdk::kResultTrue, "apply DPI after user resize");
    require(same_rect(size_of(view), {0, 0, 2560, 1920}), "DPI preserves user-selected logical zoom");
    resize_to(view, {0, 0, 3072, 2304});
    require(scaling->setContentScaleFactor(1.0F) == sdk::kResultTrue, "remove DPI at maximum user zoom");
    require(same_rect(size_of(view), {0, 0, 1536, 1152}), "maximum user zoom survives DPI round trip");
    resize_to(view, default_size);
}

void host_frame_scale_contract(sdk::IPlugView& view) {
    current_case = "content scaling with host frame before attachment";
    auto scaling = scaling_of(view);
    RecordingFrame frame{view};
    using Response = RecordingFrame::Response;
    struct Fixture {
        Response response;
        Scale factor;
        sdk::int32 requested_width;
        sdk::int32 retained_width;
        sdk::int32 minimum_width;
        sdk::int32 maximum_width;
        bool accepted;
    };
    constexpr std::array<Fixture, 8> fixtures{{
        {Response::accept, 2.0F, 2048, 2048, 1536, 3072, true},
        {Response::refuse, 1.5F, 1536, 2048, 1536, 3072, false},
        {Response::accept, 1.5F, 1536, 1536, 1152, 2304, true},
        {Response::invalid_on_size, 1.0F, 1024, 1536, 1152, 2304, false},
        {Response::omit_on_size, 2.0F, 2048, 1536, 1152, 2304, false},
        {Response::refuse_after_on_size, 2.0F, 2048, 1536, 1152, 2304, false},
        {Response::accept, 1.0F, 1024, 1024, 768, 1536, true},
        {Response::omit_on_size, 1.0F, 1024, 1024, 768, 1536, true},
    }};
    for (const auto& fixture : fixtures) {
        frame.response = fixture.response;
        const auto before = size_of(view);
        const auto calls_before = frame.calls;
        const auto notifications_before = frame.on_size_calls;
        const sdk::ViewRect wanted{0, 0, fixture.requested_width, fixture.requested_width * 3 / 4};
        const sdk::ViewRect retained{0, 0, fixture.retained_width, fixture.retained_width * 3 / 4};
        const auto result = scaling->setContentScaleFactor(fixture.factor);
        require(result == (fixture.accepted ? sdk::kResultTrue : sdk::kResultFalse),
                "DPI result agrees with host resize and synchronous notification");
        require(frame.calls == calls_before + 1 && frame.correct_view && !frame.recursive_request,
                "DPI sends exactly one resize request to the provided host frame");
        require(same_rect(frame.requested, wanted), "host receives the requested scaled rectangle");
        require(frame.before_result == sdk::kResultOk && same_rect(frame.before, before),
                "getSize inside resizeView remains old until host onSize");
        const bool notified = fixture.response == Response::accept ||
                              fixture.response == Response::invalid_on_size ||
                              fixture.response == Response::refuse_after_on_size;
        require(frame.on_size_calls == notifications_before + (notified ? 1U : 0U),
                "host fixture records the expected onSize notification count");
        const bool valid_notification = notified && fixture.response != Response::invalid_on_size;
        const auto expected_notification = valid_notification ? sdk::kResultTrue :
            (notified ? sdk::kResultFalse : sdk::kNotImplemented);
        require(frame.on_size_result == expected_notification,
                "view accepts valid onSize and rejects a malformed host notification");
        require(frame.after_result == sdk::kResultOk &&
                    same_rect(frame.after, valid_notification ? wanted : before),
                "getSize after host onSize reflects only a valid committed notification");
        require(same_rect(size_of(view), retained), "failed host negotiation restores prior geometry");
        const auto minimum = constrain(view, {0, 0, 1, 1},
                                       fixture.minimum_width, fixture.maximum_width);
        const auto maximum = constrain(view, {0, 0, 100000, 100000},
                                       fixture.minimum_width, fixture.maximum_width);
        require(width(minimum) == fixture.minimum_width && width(maximum) == fixture.maximum_width,
                "failed host negotiation restores prior scale-dependent limits");
    }

    const auto before_invalid = frame.calls;
    for (const Scale invalid : {0.0F, std::numeric_limits<Scale>::quiet_NaN()}) {
        ++rejected_scales;
        require(scaling->setContentScaleFactor(invalid) == sdk::kInvalidArgument,
                "invalid DPI is rejected before asking the host to resize");
    }
    require(frame.calls == before_invalid && same_rect(size_of(view), default_size),
            "invalid DPI leaves host request count and geometry unchanged");

    resize_to(view, {0, 0, 1280, 960});
    require(frame.calls == before_invalid, "host-driven onSize does not request another host resize");
    frame.response = Response::accept;
    require(scaling->setContentScaleFactor(2.0F) == sdk::kResultTrue &&
                same_rect(size_of(view), {0, 0, 2560, 1920}),
            "host-negotiated DPI preserves user-selected logical zoom");
    frame.response = Response::refuse;
    require(scaling->setContentScaleFactor(1.0F) == sdk::kResultFalse &&
                same_rect(size_of(view), {0, 0, 2560, 1920}),
            "host refusal preserves nondefault user zoom and DPI");
    frame.response = Response::accept;
    require(scaling->setContentScaleFactor(1.0F) == sdk::kResultTrue &&
                same_rect(size_of(view), {0, 0, 1280, 960}),
            "retry after host refusal retains the original user zoom");
    const auto requests_before_host_resize = frame.calls;
    resize_to(view, default_size);
    require(frame.calls == requests_before_host_resize,
            "host resize to default size does not echo a resize request");
}

using Instances = std::array<ControllerInstance, controller_count>;

void independence_contract(Instances& instances) {
    current_case = "independent live controllers and views";
    std::array<sdk::ViewRect, controller_count> expected_sizes;
    expected_sizes.fill(default_size);
    std::array<double, controller_count> gains{};
    for (std::size_t index = 0; index < instances.size(); ++index) {
        gains[index] = 0.125 + static_cast<double>(index) / 16.0;
        require(instances[index].controller().setParamNormalized(0, gains[index]) == sdk::kResultOk,
                "set distinct public gain on each controller");
        for (std::size_t prior = 0; prior < index; ++prior) {
            require(&instances[index].controller() != &instances[prior].controller(),
                    "separate factory calls create independent controllers");
            for (std::size_t a = 0; a < 2; ++a) {
                for (std::size_t b = 0; b < 2; ++b) {
                    require(&instances[index].view(a) != &instances[prior].view(b),
                            "no view object is shared across controllers or modules");
                }
            }
        }
    }
    for (std::size_t target = 0; target < instances.size(); ++target) {
        context = identities[target / controllers_per_module].name;
        const auto w = static_cast<sdk::int32>(800 + target * 80);
        resize_to(instances[target].view(0), {0, 0, w, w * 3 / 4});
        {
            auto scaling = scaling_of(instances[target].view(0));
            require(scaling->setContentScaleFactor(2.0F) == sdk::kResultTrue, "scale only target view");
        }
        expected_sizes[target] = {0, 0, w * 2, w * 3 / 2};
        for (std::size_t peer = 0; peer < instances.size(); ++peer) {
            require(same_rect(size_of(instances[peer].view(0)), expected_sizes[peer]),
                    "target resize and DPI preserve every other primary view");
            require(same_rect(size_of(instances[peer].view(1)), default_size),
                    "target resize and DPI preserve sibling views on the same controller");
            require(instances[peer].controller().getParamNormalized(0) == gains[peer],
                    "controller values remain independent while views resize");
        }
    }
    for (std::size_t target = 0; target < instances.size(); ++target) {
        context = identities[target / controllers_per_module].name;
        resize_to(instances[target].view(1), {0, 0, 768, 576});
        require(same_rect(size_of(instances[target].view(0)), expected_sizes[target]),
                "sibling resize leaves primary view geometry intact");
        instances[target].replace_view(0);
        require(same_rect(size_of(instances[target].view(1)), {0, 0, 768, 576}),
                "release and recreation leave the surviving sibling view intact");
        require(instances[target].controller().getParamNormalized(0) == gains[target],
                "recreating a view preserves its controller's parameter state");
        require(instances[target].view(0).onFocus(false) == sdk::kResultOk,
                "recreated unattached view accepts focus loss");
    }
}

void run(const std::array<const char*, 3>& paths) {
    vst::HostApplication host;
    std::array<Module::Ptr, 3> modules;
    for (std::size_t index = 0; index < modules.size(); ++index) {
        context = identities[index].name;
        current_case = "module loading";
        std::string error;
        modules[index] = Module::create(paths[index], error);
        require(modules[index] != nullptr, "load actual native GUI bundle: " + error);
    }
    {
        Instances instances;
        for (std::size_t index = 0; index < instances.size(); ++index) {
            const auto module_index = index / controllers_per_module;
            context = identities[module_index].name;
            current_case = "controller and view creation";
            instances[index].open(modules[module_index], identities[module_index], host);
        }
        for (std::size_t index = 0; index < instances.size(); ++index) {
            context = identities[index / controllers_per_module].name;
            basic_view_contract(instances[index].view(0));
            basic_view_contract(instances[index].view(1));
            resize_contract(instances[index].view(0));
            scale_contract(instances[index].view(0));
            host_frame_scale_contract(instances[index].view(0));
            require(same_rect(size_of(instances[index].view(1)), default_size),
                    "contract exercise leaves simultaneous sibling view unchanged");
        }
        independence_contract(instances);
        current_case = "release views before controller termination";
        for (std::size_t index = instances.size(); index-- > 0;) {
            context = identities[index / controllers_per_module].name;
            instances[index].close();
            for (std::size_t live = 0; live < index; ++live) {
                require(same_rect(size_of(instances[live].view(0)), default_size),
                        "terminating another controller preserves a live primary view");
                require(same_rect(size_of(instances[live].view(1)), {0, 0, 768, 576}),
                        "terminating another controller preserves a live sibling view");
            }
        }
    }
    for (std::size_t index = 0; index < modules.size(); ++index) {
        context = identities[index].name;
        current_case = "fresh controller after prior instances terminate";
        ControllerInstance fresh;
        fresh.open(modules[index], identities[index], host);
        require(fresh.controller().getParamNormalized(0) == 0.5,
                "new controller does not inherit terminated instance parameter state");
        basic_view_contract(fresh.view(0));
        fresh.close();
        require(modules[index].use_count() == 1, "all test interfaces released before module unload");
        modules[index].reset();
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 4) {
            throw std::runtime_error(
                "usage: vst3_photo_editor_test DIRECT_BUNDLE FILTERED_BUNDLE QUADRATURE_BUNDLE");
        }
        run({argv[1], argv[2], argv[3]});
        std::cout << "PASS photo editor unattached contract: " << checks << " checks, "
                  << controllers_initialized << " real controllers, " << views_created
                  << " views created, " << rejected_scales << " malformed scales rejected; "
                  << host_resize_requests << " host resize requests; "
                  << "no native window attached or rendering assessed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL photo editor unattached contract: " << error.what() << '\n';
        return 1;
    }
}
