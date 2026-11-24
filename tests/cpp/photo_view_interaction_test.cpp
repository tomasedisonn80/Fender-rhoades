// PhotoView is deliberately private to the production editor translation unit.
// Include that unit here so these tests execute its actual event handlers rather
// than a copied interaction model or a test-only replacement. Do not also compile
// photo_editor.cpp into this executable. Link the real controller, native-focus
// implementation and SDK/VSTGUI libraries normally.
#include "plugins/vst3/photo_editor.cpp"

#include "base/source/updatehandler.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "vstgui/lib/vstguiinit.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace photo_view_test {
namespace sdk = Steinberg;
namespace vst = Steinberg::Vst;
namespace gui = VSTGUI;
using Controller = rhodes::vst3::RhodesController;
using View = rhodes::vst3::PhotoView;

std::size_t checks = 0;

void check(bool condition, std::string_view message) {
    ++checks;
    if (!condition) { throw std::runtime_error(std::string{message}); }
}

bool close(double actual, double expected) {
    // Persistent controller values are binary32 widened to binary64. Use the
    // language's conversion as an independent oracle, not production quantize.
    const double canonical = static_cast<double>(static_cast<float>(expected));
    return std::isfinite(actual) && std::abs(actual - canonical) <= 1.0e-12;
}

// These host interfaces are fixture-owned, including every message. The fixture
// outlives its controller and view; SDK releases must not delete stack storage.
template <class Interface>
struct FixedObject : Interface {
    sdk::uint32 PLUGIN_API addRef() override { return 1; }
    sdk::uint32 PLUGIN_API release() override { return 1; }
    sdk::tresult PLUGIN_API queryInterface(const sdk::TUID interface_id, void** object) override {
        if (interface_id == nullptr || object == nullptr) { return sdk::kInvalidArgument; }
        *object = nullptr;
        if (sdk::FUnknownPrivate::iidEqual(interface_id, Interface::iid) ||
            sdk::FUnknownPrivate::iidEqual(interface_id, sdk::FUnknown::iid)) {
            *object = static_cast<Interface*>(this);
            return sdk::kResultOk;
        }
        return sdk::kNoInterface;
    }
};

void invoke_once(std::function<void()>& hook) {
    auto callback = std::exchange(hook, {});
    if (callback) { callback(); }
}

// Install the SDK's real dependency dispatcher. Parameter::changed() must reach
// this observer through the SDK setter, not through a fixture-owned fake setter.
struct ScopedUpdates {
    sdk::IUpdateHandler* previous = sdk::FObject::getUpdateHandler();
    sdk::UpdateHandler handler;
    ScopedUpdates() { sdk::FObject::setUpdateHandler(&handler); }
    ~ScopedUpdates() { sdk::FObject::setUpdateHandler(previous); }
};

struct ParameterChange {
    double value;
    std::uint64_t revision;
};

struct ParameterObserver final : FixedObject<sdk::IDependent> {
    Controller& controller;
    vst::Parameter* parameter;
    std::vector<ParameterChange> changes;
    std::function<void()> on_change;

    explicit ParameterObserver(Controller& value)
        : controller(value), parameter(controller.getParameterObject(0)) {
        check(parameter != nullptr, "observed Volume parameter is missing");
        parameter->addDependent(this);
    }
    ~ParameterObserver() { parameter->removeDependent(this); }

    void PLUGIN_API update(sdk::FUnknown*, sdk::int32 message) override {
        if (message != sdk::IDependent::kChanged) { return; }
        changes.push_back({controller.getParamNormalized(0), controller.parameterRevision(0)});
        invoke_once(on_change);
    }
    void expect(std::initializer_list<ParameterChange> expected) const {
        check(changes.size() == expected.size(), "unexpected SDK parameter notification count");
        std::size_t index = 0;
        for (const auto& wanted : expected) {
            const auto& actual = changes[index++];
            check(close(actual.value, wanted.value) && actual.revision == wanted.revision,
                  "SDK parameter notification value or mutation revision differs");
        }
    }
};

enum class EditKind { begin, perform, end };
struct Edit {
    EditKind kind;
    vst::ParamID id;
    double value = 0.0;
};

struct Handler final : FixedObject<vst::IComponentHandler> {
    std::vector<Edit> edits;
    std::array<int, 8> active{};
    bool malformed = false;
    bool accept_begin = true;
    bool accept_perform = true;
    std::function<void()> on_begin;
    std::function<void()> on_perform;
    std::function<void()> on_end;

    sdk::tresult PLUGIN_API beginEdit(vst::ParamID id) override {
        const bool accepted = accept_begin;
        edits.push_back({EditKind::begin, id});
        if (id >= active.size()) { malformed = true; }
        else if (accepted && ++active[id] != 1) { malformed = true; }
        invoke_once(on_begin);
        return accepted ? sdk::kResultOk : sdk::kResultFalse;
    }
    sdk::tresult PLUGIN_API performEdit(vst::ParamID id, vst::ParamValue value) override {
        const bool accepted = accept_perform;
        edits.push_back({EditKind::perform, id, value});
        if (id >= active.size() || active[id] != 1) { malformed = true; }
        invoke_once(on_perform);
        return accepted ? sdk::kResultOk : sdk::kResultFalse;
    }
    sdk::tresult PLUGIN_API endEdit(vst::ParamID id) override {
        edits.push_back({EditKind::end, id});
        if (id >= active.size() || active[id] != 1) { malformed = true; }
        else { --active[id]; }
        invoke_once(on_end);
        return sdk::kResultOk;
    }
    sdk::tresult PLUGIN_API restartComponent(sdk::int32) override { return sdk::kResultOk; }

    void expect(std::initializer_list<Edit> expected) const {
        check(!malformed, "host received an edit outside one accepted begin/end pair");
        check(edits.size() == expected.size(), "unexpected number of host edit callbacks");
        std::size_t index = 0;
        for (const auto& wanted : expected) {
            const auto& actual = edits[index++];
            check(actual.kind == wanted.kind && actual.id == wanted.id,
                  "host edit order or parameter differs from the literal trace");
            if (wanted.kind == EditKind::perform) {
                check(close(actual.value, wanted.value), "host received the wrong canonical value");
            }
        }
    }
    void balanced() const {
        check(!malformed, "invalid host edit protocol");
        for (const auto count : active) { check(count == 0, "accepted beginEdit remains open"); }
    }
    void clear() {
        balanced();
        edits.clear();
    }
};

struct Attributes final : FixedObject<vst::IAttributeList> {
    sdk::int64 key = -1;
    sdk::int64 down = -1;
    double velocity = -1.0;

    sdk::tresult PLUGIN_API setInt(AttrID id, sdk::int64 value) override {
        if (std::strcmp(id, "key") == 0) { key = value; return sdk::kResultOk; }
        if (std::strcmp(id, "down") == 0) { down = value; return sdk::kResultOk; }
        return sdk::kResultFalse;
    }
    sdk::tresult PLUGIN_API getInt(AttrID id, sdk::int64& value) override {
        if (std::strcmp(id, "key") == 0) { value = key; return sdk::kResultOk; }
        if (std::strcmp(id, "down") == 0) { value = down; return sdk::kResultOk; }
        return sdk::kResultFalse;
    }
    sdk::tresult PLUGIN_API setFloat(AttrID id, double value) override {
        if (std::strcmp(id, "velocity") != 0) { return sdk::kResultFalse; }
        velocity = value;
        return sdk::kResultOk;
    }
    sdk::tresult PLUGIN_API getFloat(AttrID id, double& value) override {
        if (std::strcmp(id, "velocity") != 0) { return sdk::kResultFalse; }
        value = velocity;
        return sdk::kResultOk;
    }
    sdk::tresult PLUGIN_API setString(AttrID, const vst::TChar*) override {
        return sdk::kNotImplemented;
    }
    sdk::tresult PLUGIN_API getString(AttrID, vst::TChar*, sdk::uint32) override {
        return sdk::kNotImplemented;
    }
    sdk::tresult PLUGIN_API setBinary(AttrID, const void*, sdk::uint32) override {
        return sdk::kNotImplemented;
    }
    sdk::tresult PLUGIN_API getBinary(AttrID, const void*&, sdk::uint32&) override {
        return sdk::kNotImplemented;
    }
};

struct Message final : FixedObject<vst::IMessage> {
    const char* id = nullptr;
    Attributes attributes;
    sdk::FIDString PLUGIN_API getMessageID() override { return id; }
    void PLUGIN_API setMessageID(sdk::FIDString value) override { id = value; }
    vst::IAttributeList* PLUGIN_API getAttributes() override { return &attributes; }
};

struct Application final : FixedObject<vst::IHostApplication> {
    std::array<Message, 64> messages;
    std::size_t allocated = 0;
    std::function<void()> on_allocate;

    sdk::tresult PLUGIN_API getName(vst::String128 name) override {
        name[0] = 'T';
        name[1] = 0;
        return sdk::kResultOk;
    }
    sdk::tresult PLUGIN_API createInstance(sdk::TUID cid, sdk::TUID interface_id,
                                          void** object) override {
        if (object == nullptr) { return sdk::kInvalidArgument; }
        *object = nullptr;
        if (!sdk::FUnknownPrivate::iidEqual(cid, vst::IMessage::iid) ||
            !sdk::FUnknownPrivate::iidEqual(interface_id, vst::IMessage::iid)) {
            return sdk::kNoInterface;
        }
        if (allocated == messages.size()) { return sdk::kResultFalse; }
        // Reserve before calling out: a nested release gets a different message
        // and cannot overwrite the still-in-flight note-on's attributes.
        auto* message = &messages[allocated++];
        invoke_once(on_allocate);
        *object = static_cast<vst::IMessage*>(message);
        return sdk::kResultOk;
    }
};

struct Note {
    bool release_all = false;
    sdk::int64 key = -1;
    sdk::int64 down = -1;
    double velocity = -1.0;
};

struct Receiver final : FixedObject<vst::IConnectionPoint> {
    std::vector<Note> notes;
    std::array<bool, 73> held{};
    bool malformed = false;
    std::function<void()> on_note;

    sdk::tresult PLUGIN_API connect(vst::IConnectionPoint*) override { return sdk::kResultOk; }
    sdk::tresult PLUGIN_API disconnect(vst::IConnectionPoint*) override { return sdk::kResultOk; }
    sdk::tresult PLUGIN_API notify(vst::IMessage* message) override {
        if (message == nullptr || message->getMessageID() == nullptr) {
            malformed = true;
            return sdk::kResultFalse;
        }
        Note note;
        if (std::strcmp(message->getMessageID(), "RhodesPhotoReleaseV1") == 0) {
            note.release_all = true;
            notes.push_back(note);
            held.fill(false);
            return sdk::kResultOk;
        }
        auto* attributes = message->getAttributes();
        if (std::strcmp(message->getMessageID(), "RhodesPhotoNoteV1") != 0 ||
            attributes == nullptr || attributes->getInt("key", note.key) != sdk::kResultOk ||
            attributes->getInt("down", note.down) != sdk::kResultOk ||
            attributes->getFloat("velocity", note.velocity) != sdk::kResultOk ||
            note.key < 0 || note.key >= 73 || (note.down != 0 && note.down != 1)) {
            malformed = true;
            return sdk::kResultFalse;
        }
        // The receiving side accepts this down before delivering focus loss.
        // Record first so any nested release has its real position on the wire.
        notes.push_back(note);
        held[static_cast<std::size_t>(note.key)] = note.down == 1;
        invoke_once(on_note);
        return sdk::kResultOk;
    }
    void note(std::size_t index, int key, bool down) const {
        check(!malformed && index < notes.size(), "missing or malformed audition message");
        const auto& actual = notes[index];
        check(!actual.release_all && actual.key == key && actual.down == (down ? 1 : 0) &&
              actual.velocity == (down ? 0.75 : 0.0), "audition wire sequence differs");
    }
    void quiet() const {
        check(!malformed, "malformed audition transport");
        for (const auto down : held) { check(!down, "accepted audition note remains held"); }
    }
};

// Fixed photograph locations are independent event inputs, not calls into the
// geometry helper under test. A source-sized view makes their units explicit.
const gui::CPoint volume{319.0, 458.0};
const gui::CPoint bass{422.5, 478.5};
const gui::CPoint key0{128.0, 600.0};
const gui::CPoint key1{158.0, 600.0};

bool down(View& view, gui::CPoint position, unsigned clicks = 1) {
    gui::MouseDownEvent event;
    event.mousePosition = position;
    event.buttonState.set(gui::MouseButton::Left);
    event.clickCount = static_cast<decltype(event.clickCount)>(clicks);
    view.onMouseDownEvent(event);
    return event.consumed;
}

bool move(View& view, gui::CPoint position, gui::Modifiers modifiers = {}) {
    gui::MouseMoveEvent event;
    event.mousePosition = position;
    event.buttonState.set(gui::MouseButton::Left);
    event.modifiers = modifiers;
    view.onMouseMoveEvent(event);
    return event.consumed;
}

void up(View& view, gui::CPoint position) {
    gui::MouseUpEvent event;
    event.mousePosition = position;
    view.onMouseUpEvent(event);
}

bool key(View& view, gui::VirtualKey code, gui::Modifiers modifiers = {}, bool repeat = false) {
    gui::KeyboardEvent event;
    event.type = gui::EventType::KeyDown;
    event.virt = code;
    event.modifiers = modifiers;
    event.isRepeat = repeat;
    view.onKeyboardEvent(event);
    return event.consumed;
}

bool wheel(View& view, double delta, gui::Modifiers modifiers = {}) {
    gui::MouseWheelEvent event;
    event.mousePosition = volume;
    event.deltaY = delta;
    event.modifiers = modifiers;
    view.onMouseWheelEvent(event);
    return event.consumed;
}

struct Fixture {
    Application application;
    Handler handler;
    Receiver receiver;
    Controller controller;
    gui::SharedPointer<View> view;

    Fixture() {
        check(controller.initialize(&application) == sdk::kResultOk, "initialize real controller");
        check(controller.setComponentHandler(&handler) == sdk::kResultOk, "install host handler");
        check(controller.connect(&receiver) == sdk::kResultOk, "connect audition receiver");
        view = make_view();
    }
    ~Fixture() {
        handler.on_begin = {};
        handler.on_perform = {};
        handler.on_end = {};
        receiver.on_note = {};
        application.on_allocate = {};
        view = nullptr;
        (void)controller.disconnect(&receiver);
        (void)controller.setComponentHandler(nullptr);
        (void)controller.terminate();
    }
    gui::SharedPointer<View> make_view() {
        // This fixture never attaches, draws, or opens a native CFrame. Its
        // independent SharedPointer keeps the view alive across every callback;
        // it deliberately does not claim whole-editor-close lifetime coverage.
        auto result = gui::owned(new View(controller, nullptr));
        result->setViewSize(gui::CRect{0.0, 0.0, 1536.0, 1152.0}, false);
        return result;
    }
    void set(vst::ParamID id, double value) {
        check(controller.setParamNormalized(id, value) == sdk::kResultOk, "seed controller value");
    }
    void value(vst::ParamID id, double expected) {
        check(close(controller.getParamNormalized(id), expected), "controller value differs");
    }
    void cancel() {
        // This is the exact call PhotoEditor::onFocus(false) forwards to its
        // PhotoView. It cancels logically without removing the dispatch target.
        check(view->cancel_interaction(), "focus-equivalent cancellation failed");
    }
    void expect_inert_move() {
        const auto edits = handler.edits.size();
        const auto notes = receiver.notes.size();
        check(!move(*view, {319.0, 422.0}), "cancelled interaction resumed a captured drag");
        check(handler.edits.size() == edits && receiver.notes.size() == notes,
              "cancelled interaction emitted another edit or note");
    }
};

void rapid_switches() {
    struct Switch { vst::ParamID id; gui::CPoint point; };
    for (const auto& control : std::array<Switch, 2>{{
             {1, {776.5, 932.0}}, {7, {560.5, 465.0}}}}) {
        for (const bool initially_on : {false, true}) {
            for (const unsigned presses : {2U, 3U}) {
                Fixture f;
                f.set(control.id, initially_on ? 1.0 : 0.0);
                check(f.view->parameter_at(control.point) == static_cast<int>(control.id),
                      "fixed switch location did not hit its intended control");
                const std::array<double, 3> expected = initially_on ?
                    std::array<double, 3>{0.0, 1.0, 0.0} : std::array<double, 3>{1.0, 0.0, 1.0};
                for (unsigned press = 0; press < presses; ++press) {
                    check(down(*f.view, control.point, press + 1), "switch press not consumed");
                    up(*f.view, control.point);
                    f.value(control.id, expected[press]);
                    f.handler.expect({{EditKind::begin, control.id},
                                      {EditKind::perform, control.id, expected[press]},
                                      {EditKind::end, control.id}});
                    f.handler.clear();
                }
            }
        }
    }
}

void continuous_reset() {
    struct Control { vst::ParamID id; gui::CPoint point; };
    const std::array<Control, 5> controls{{{0, volume}, {3, bass},
        {4, {421.5, 449.0}}, {5, {519.0, 458.0}}, {6, {603.0, 458.0}}}};
    for (const auto& control : controls) {
        Fixture f;
        f.set(control.id, 0.79);
        const auto* parameter = f.controller.getParameterObject(control.id);
        check(parameter != nullptr, "continuous control is not registered");
        const auto declared_default = parameter->getInfo().defaultNormalizedValue;
        check(down(*f.view, control.point), "first continuous press not consumed");
        up(*f.view, control.point);
        f.value(control.id, 0.79);
        f.handler.expect({{EditKind::begin, control.id}, {EditKind::end, control.id}});
        f.handler.clear();
        for (const unsigned clicks : {2U, 3U}) {
            f.set(control.id, 0.79);
            check(down(*f.view, control.point, clicks), "continuous reset not consumed");
            up(*f.view, control.point);
            f.value(control.id, declared_default);
            f.handler.expect({{EditKind::begin, control.id},
                              {EditKind::perform, control.id, declared_default},
                              {EditKind::end, control.id}});
            f.handler.clear();
        }
    }
}

void host_shortcuts() {
    Fixture f;
    f.set(0, 0.5);
    for (const auto modifier : {gui::ModifierKey::Control, gui::ModifierKey::Alt,
                               gui::ModifierKey::Super}) {
        for (const bool shift : {false, true}) {
            gui::Modifiers modifiers{modifier};
            if (shift) { modifiers.add(gui::ModifierKey::Shift); }
            for (const auto code : {gui::VirtualKey::Right, gui::VirtualKey::Home,
                    gui::VirtualKey::End, gui::VirtualKey::Tab, gui::VirtualKey::Escape,
                    gui::VirtualKey::Space, gui::VirtualKey::Return}) {
                check(!key(*f.view, code, modifiers), "host shortcut was consumed by the view");
                f.value(0, 0.5);
                f.handler.expect({});
            }
        }
    }
    // An ignored modified Tab must not have moved focus away from Volume.
    check(key(*f.view, gui::VirtualKey::Right), "unmodified arrow was not handled");
    f.value(0, 0.51);
    f.handler.expect({{EditKind::begin, 0}, {EditKind::perform, 0, 0.51}, {EditKind::end, 0}});
    f.handler.clear();
    check(down(*f.view, volume), "start shortcut-preservation drag");
    check(!key(*f.view, gui::VirtualKey::Escape, gui::Modifiers{gui::ModifierKey::Super}),
          "Super-Escape consumed an active drag");
    f.handler.expect({{EditKind::begin, 0}});
    up(*f.view, volume);
    f.handler.balanced();
}

void fine_steps() {
    Fixture f;
    const gui::Modifiers shift{gui::ModifierKey::Shift};
    f.set(0, 0.5);
    check(key(*f.view, gui::VirtualKey::Right), "ordinary arrow");
    f.value(0, 0.51);
    check(key(*f.view, gui::VirtualKey::Right, shift), "fine arrow");
    f.value(0, 0.511);
    check(key(*f.view, gui::VirtualKey::Left, shift), "reverse fine arrow");
    f.value(0, 0.51);
    f.handler.expect({{EditKind::begin, 0}, {EditKind::perform, 0, 0.51}, {EditKind::end, 0},
                      {EditKind::begin, 0}, {EditKind::perform, 0, 0.511}, {EditKind::end, 0},
                      {EditKind::begin, 0}, {EditKind::perform, 0, 0.51}, {EditKind::end, 0}});
    f.handler.clear();
    f.set(0, 0.5);
    check(wheel(*f.view, 1.0), "ordinary wheel");
    f.value(0, 0.52);
    f.set(0, 0.5);
    check(wheel(*f.view, 1.0, shift), "fine wheel");
    f.value(0, 0.502);
    f.set(0, 0.5);
    check(wheel(*f.view, -1.0, shift), "reverse fine wheel");
    f.value(0, 0.498);
    f.handler.expect({{EditKind::begin, 0}, {EditKind::perform, 0, 0.52}, {EditKind::end, 0},
                      {EditKind::begin, 0}, {EditKind::perform, 0, 0.502}, {EditKind::end, 0},
                      {EditKind::begin, 0}, {EditKind::perform, 0, 0.498}, {EditKind::end, 0}});
    f.handler.clear();
    f.set(0, 0.5);
    check(down(*f.view, volume), "start fine drag");
    check(move(*f.view, {319.0, 440.0}), "ordinary drag motion");
    f.value(0, 0.6);
    check(move(*f.view, {319.0, 422.0}, shift), "fine drag motion");
    f.value(0, 0.61);
    up(*f.view, {319.0, 422.0});
    f.handler.expect({{EditKind::begin, 0}, {EditKind::perform, 0, 0.6},
                      {EditKind::perform, 0, 0.61}, {EditKind::end, 0}});
    f.handler.balanced();
}

void rejected_edits() {
    Fixture f;
    f.set(0, 0.5);
    f.handler.accept_begin = false;
    check(wheel(*f.view, 1.0), "rejected wheel must still be consumed");
    f.value(0, 0.5);
    f.handler.expect({{EditKind::begin, 0}});
    f.handler.clear();
    check(down(*f.view, volume), "rejected drag press not consumed");
    f.expect_inert_move();
    up(*f.view, volume);
    f.handler.expect({{EditKind::begin, 0}});
    f.handler.clear();
    f.handler.accept_begin = true;
    f.handler.accept_perform = false;
    check(wheel(*f.view, 1.0), "rejected perform not consumed");
    f.value(0, 0.5);
    f.handler.expect({{EditKind::begin, 0}, {EditKind::perform, 0, 0.52}, {EditKind::end, 0}});
    f.handler.balanced();
}

void end_cancel_idempotent() {
    Fixture f;
    check(down(*f.view, volume), "start drag before end callback");
    f.handler.on_end = [&] { f.cancel(); };
    f.cancel();
    f.cancel();
    up(*f.view, volume);
    f.handler.expect({{EditKind::begin, 0}, {EditKind::end, 0}});
    f.handler.balanced();
    f.expect_inert_move();
}

void begin_cancel_drag() {
    Fixture f;
    f.set(0, 0.5);
    f.handler.on_begin = [&] { f.cancel(); };
    check(down(*f.view, volume), "cancelled drag press not consumed");
    f.handler.expect({{EditKind::begin, 0}, {EditKind::end, 0}});
    f.handler.balanced();
    f.value(0, 0.5);
    f.expect_inert_move();
    up(*f.view, volume);
    f.cancel();
    f.handler.expect({{EditKind::begin, 0}, {EditKind::end, 0}});
}

void begin_cancel_wheel() {
    Fixture f;
    f.set(0, 0.5);
    f.handler.on_begin = [&] { f.cancel(); };
    check(wheel(*f.view, 1.0), "cancelled wheel not consumed");
    f.value(0, 0.5);
    f.handler.expect({{EditKind::begin, 0}, {EditKind::end, 0}});
    f.handler.balanced();
    f.expect_inert_move();
}

void begin_cancel_switch() {
    Fixture f;
    f.set(7, 1.0);
    f.handler.on_begin = [&] { f.cancel(); };
    check(down(*f.view, {560.5, 465.0}, 2), "cancelled rapid switch press not consumed");
    f.value(7, 1.0);
    f.handler.expect({{EditKind::begin, 7}, {EditKind::end, 7}});
    f.handler.balanced();
}

void end_cancel_new_press() {
    Fixture f;
    check(down(*f.view, volume), "start old gesture");
    f.handler.on_end = [&] { f.cancel(); };
    check(down(*f.view, bass), "new press not consumed after reentrant cancellation");
    f.handler.expect({{EditKind::begin, 0}, {EditKind::end, 0}});
    f.handler.balanced();
    f.expect_inert_move();
}

void perform_cancel_drag() {
    Fixture f;
    f.set(0, 0.5);
    check(down(*f.view, volume), "start perform-cancel drag");
    f.handler.on_perform = [&] { f.cancel(); };
    check(move(*f.view, {319.0, 440.0}), "perform-cancel move not consumed");
    f.value(0, 0.6); // Cancellation retains the last accepted edit.
    f.handler.expect({{EditKind::begin, 0}, {EditKind::perform, 0, 0.6}, {EditKind::end, 0}});
    f.handler.balanced();
    f.expect_inert_move();
    up(*f.view, volume);
    f.cancel();
    f.handler.expect({{EditKind::begin, 0}, {EditKind::perform, 0, 0.6}, {EditKind::end, 0}});
}

void perform_cancel_host_update() {
    Fixture f;
    f.set(0, 0.5);
    f.handler.accept_perform = false;
    f.handler.on_perform = [&] {
        f.cancel();
        f.set(0, 0.75); // A newer host update must survive the rejected UI edit.
    };
    check(wheel(*f.view, 1.0), "perform-cancel wheel not consumed");
    f.value(0, 0.75);
    f.handler.expect({{EditKind::begin, 0}, {EditKind::perform, 0, 0.52}, {EditKind::end, 0}});
    f.handler.balanced();
    f.expect_inert_move();
}

void observer_cancel_rollback() {
    for (const bool drag : {false, true}) {
        ScopedUpdates updates;
        Fixture f;
        f.set(0, 0.5);
        check(f.controller.parameterRevision(0) == 1, "unexpected seeded Volume revision");
        ParameterObserver observer(f.controller);
        observer.on_change = [&] {
            // Cancellation occurs inside the provisional SDK setter, before
            // the host has received any perform notification.
            f.handler.expect({{EditKind::begin, 0}});
            f.cancel();
        };
        if (drag) {
            check(down(*f.view, volume), "start observer-cancelled drag");
            check(move(*f.view, {319.0, 440.0}), "observer-cancelled motion not consumed");
            observer.expect({{0.6, 2}, {0.5, 3}});
        } else {
            check(wheel(*f.view, 1.0), "observer-cancelled wheel not consumed");
            observer.expect({{0.52, 2}, {0.5, 3}});
        }
        f.value(0, 0.5);
        check(f.controller.parameterRevision(0) == 3, "provisional value was not rolled back once");
        f.handler.expect({{EditKind::begin, 0}, {EditKind::end, 0}});
        f.handler.balanced();
        f.expect_inert_move();
        up(*f.view, volume);
        f.cancel();
        f.handler.expect({{EditKind::begin, 0}, {EditKind::end, 0}});
    }
}

void observer_cancel_host_changed() {
    ScopedUpdates updates;
    Fixture f;
    f.set(0, 0.5);
    ParameterObserver observer(f.controller);
    observer.on_change = [&] {
        f.handler.expect({{EditKind::begin, 0}});
        f.cancel();
        f.set(0, 0.75);
    };
    check(wheel(*f.view, 1.0), "observer host-write wheel not consumed");
    observer.expect({{0.52, 2}, {0.75, 3}});
    f.value(0, 0.75);
    check(f.controller.parameterRevision(0) == 3, "newer changed host write was rolled back");
    f.handler.expect({{EditKind::begin, 0}, {EditKind::end, 0}});
    f.handler.balanced();
    f.expect_inert_move();
}

void observer_cancel_host_equal() {
    ScopedUpdates updates;
    Fixture f;
    f.set(0, 0.5);
    ParameterObserver observer(f.controller);
    observer.on_change = [&] {
        f.handler.expect({{EditKind::begin, 0}});
        // Echo the exact provisional canonical value. Its equality must not
        // erase the distinction between this host write and the earlier UI one.
        const double submitted = observer.changes.back().value;
        check(close(submitted, 0.52), "observer did not see the intended provisional value");
        f.cancel();
        f.set(0, submitted);
    };
    check(wheel(*f.view, 1.0), "observer equal-write wheel not consumed");
    observer.expect({{0.52, 2}}); // SDK equal writes advance the stamp without another notification.
    f.value(0, 0.52);
    check(f.controller.parameterRevision(0) == 3, "newer equal host write was lost or rolled back");
    f.handler.expect({{EditKind::begin, 0}, {EditKind::end, 0}});
    f.handler.balanced();
    f.expect_inert_move();
}

void perform_reject_host_equal() {
    for (const bool cancel : {false, true}) {
        Fixture f;
        f.set(0, 0.5);
        f.handler.accept_perform = false;
        f.handler.on_perform = [&] {
            f.handler.expect({{EditKind::begin, 0}, {EditKind::perform, 0, 0.52}});
            const double submitted = f.handler.edits.back().value;
            if (cancel) { f.cancel(); }
            f.set(0, submitted); // Deliberate exact host echo, not an expected-value oracle.
        };
        check(wheel(*f.view, 1.0), "equal-write rejected perform not consumed");
        f.value(0, 0.52);
        check(f.controller.parameterRevision(0) == 3, "rejected edit rolled back a newer equal write");
        f.handler.expect({{EditKind::begin, 0}, {EditKind::perform, 0, 0.52}, {EditKind::end, 0}});
        f.handler.balanced();
        f.expect_inert_move();
    }
}

void perform_reject_other_parameter() {
    for (const bool cancel : {false, true}) {
        Fixture f;
        f.set(0, 0.5);
        f.set(3, 0.25);
        f.handler.accept_perform = false;
        f.handler.on_perform = [&] {
            if (cancel) { f.cancel(); }
            f.set(3, 0.75);
        };
        check(wheel(*f.view, 1.0), "other-parameter rejected perform not consumed");
        f.value(0, 0.5);
        f.value(3, 0.75);
        check(f.controller.parameterRevision(0) == 3 && f.controller.parameterRevision(3) == 2,
              "another parameter's write suppressed or altered the Volume rollback");
        f.handler.expect({{EditKind::begin, 0}, {EditKind::perform, 0, 0.52}, {EditKind::end, 0}});
        f.handler.balanced();
        f.expect_inert_move();
    }
}

void keyboard_cancel_edit() {
    for (const auto code : {gui::VirtualKey::Right, gui::VirtualKey::Home, gui::VirtualKey::End}) {
        Fixture f;
        f.set(0, 0.5);
        check(down(*f.view, volume), "start gesture before keyboard edit");
        bool cancelled = false;
        f.handler.on_end = [&] { cancelled = true; f.cancel(); };
        check(key(*f.view, code), "cancelled keyboard edit not consumed");
        check(cancelled, "keyboard edit did not exercise the old gesture's end callback");
        f.value(0, 0.5);
        check(f.controller.parameterRevision(0) == 1, "cancelled keyboard event wrote a parameter");
        f.handler.expect({{EditKind::begin, 0}, {EditKind::end, 0}});
        f.handler.balanced();
        f.expect_inert_move();
        up(*f.view, volume);
        f.cancel();
        f.handler.expect({{EditKind::begin, 0}, {EditKind::end, 0}});
    }
}

void keyboard_cancel_tab() {
    for (const bool shift : {false, true}) {
        Fixture f;
        f.set(0, 0.5);
        f.set(4, 0.25);
        f.set(1, 0.0);
        check(down(*f.view, volume), "focus Volume before cancelled Tab");
        bool cancelled = false;
        f.handler.on_end = [&] { cancelled = true; f.cancel(); };
        const gui::Modifiers modifiers = shift ? gui::Modifiers{gui::ModifierKey::Shift} :
                                                gui::Modifiers{};
        check(key(*f.view, gui::VirtualKey::Tab, modifiers), "cancelled Tab not consumed");
        check(cancelled, "Tab did not exercise the old gesture's end callback");
        f.handler.expect({{EditKind::begin, 0}, {EditKind::end, 0}});
        f.handler.balanced();
        f.expect_inert_move();
        f.handler.clear();
        // A separate fresh event is allowed. Its literal Volume trace proves
        // neither forward nor reverse Tab resumed and changed focus after loss.
        check(key(*f.view, gui::VirtualKey::Right), "fresh arrow after cancelled Tab not consumed");
        f.value(0, 0.51);
        f.value(4, 0.25);
        f.value(1, 0.0);
        f.handler.expect({{EditKind::begin, 0}, {EditKind::perform, 0, 0.51}, {EditKind::end, 0}});
        f.handler.balanced();
    }
}

void audition_drag() {
    Fixture f;
    check(down(*f.view, key0), "audition key-down not consumed");
    check(f.receiver.held[0], "first photographed key did not sound");
    check(move(*f.view, key1), "audition glissando not consumed");
    check(!f.receiver.held[0] && f.receiver.held[1], "glissando did not transfer its held note");
    up(*f.view, key1);
    f.cancel();
    check(f.receiver.notes.size() == 4, "ordinary audition emitted duplicate cleanup");
    f.receiver.note(0, 0, true);
    f.receiver.note(1, 0, false);
    f.receiver.note(2, 1, true);
    f.receiver.note(3, 1, false);
    f.receiver.quiet();
    f.handler.expect({});
}

void glissando_cancel_old_up(bool allocation) {
    Fixture f;
    check(down(*f.view, key0), "start glissando cancellation on first key");
    check(f.receiver.notes.size() == 1, "initial glissando wire count");
    f.receiver.note(0, 0, true);
    bool cancelled = false;
    const auto cancel = [&] {
        check(f.receiver.notes.size() == (allocation ? 1U : 2U),
              "glissando cancellation missed the old note-up boundary");
        if (!allocation) { f.receiver.note(1, 0, false); }
        cancelled = true;
        f.cancel();
    };
    if (allocation) { f.application.on_allocate = cancel; }
    else { f.receiver.on_note = cancel; }
    check(move(*f.view, key1), "glissando cancellation motion not consumed");
    check(cancelled, "old note-up cancellation callback was not exercised");
    check(f.receiver.notes.size() == 2, "cancelled old note-up started the next key");
    f.receiver.note(0, 0, true);
    f.receiver.note(1, 0, false);
    f.receiver.quiet();
    check(!move(*f.view, key1), "old note-up cancellation resumed keyboard capture");
    f.expect_inert_move();
    up(*f.view, key1);
    f.cancel();
    check(f.receiver.notes.size() == 2, "old note-up cancellation left stale audition ownership");
    f.handler.expect({});
}

void audition_glissando_cancel_dispatch() { glissando_cancel_old_up(false); }
void audition_glissando_cancel_allocation() { glissando_cancel_old_up(true); }

void keyboard_audition_cancel_edit() {
    for (const bool allocation : {false, true}) {
        Fixture f;
        f.set(0, 0.5);
        check(down(*f.view, key0), "hold audition key before keyboard edit");
        bool cancelled = false;
        const auto cancel = [&] {
            check(f.receiver.notes.size() == (allocation ? 1U : 2U),
                  "keyboard cancellation missed the audition note-up boundary");
            if (!allocation) { f.receiver.note(1, 0, false); }
            cancelled = true;
            f.cancel();
        };
        if (allocation) { f.application.on_allocate = cancel; }
        else { f.receiver.on_note = cancel; }
        check(key(*f.view, gui::VirtualKey::Right), "audition-cancelled keyboard edit not consumed");
        check(cancelled, "keyboard edit did not exercise the held note's release callback");
        f.value(0, 0.5);
        check(f.controller.parameterRevision(0) == 1, "cancelled note release restarted parameter editing");
        f.handler.expect({});
        check(f.receiver.notes.size() == 2, "keyboard cancellation emitted an extra audition note");
        f.receiver.note(0, 0, true);
        f.receiver.note(1, 0, false);
        f.receiver.quiet();
        check(!move(*f.view, key1), "keyboard cancellation resumed audition capture");
        f.expect_inert_move();
        up(*f.view, key0);
        f.cancel();
        check(f.receiver.notes.size() == 2, "keyboard cancellation retained audition ownership");
    }
}

void audition_cancel_dispatch() {
    Fixture f;
    f.receiver.on_note = [&] { f.cancel(); };
    check(down(*f.view, key0), "cancelled audition press not consumed");
    f.receiver.quiet();
    check(f.receiver.notes.size() == 2, "cancelled accepted down requires one ordered note-up");
    f.receiver.note(0, 0, true);
    f.receiver.note(1, 0, false);
    check(!move(*f.view, key1), "note callback cancellation resumed a keyboard drag");
    up(*f.view, key0);
    f.cancel();
    check(f.receiver.notes.size() == 2, "cancelled note was released twice");
}

void audition_cancel_allocation() {
    Fixture f;
    bool cancelled = false;
    f.application.on_allocate = [&] { cancelled = true; f.cancel(); };
    check(down(*f.view, key0), "allocation-cancelled audition press not consumed");
    check(cancelled, "message allocation callback was not exercised");
    f.receiver.quiet();
    // A release sent before an in-flight down reaches the receiver cannot
    // satisfy this check. The observable final wire state must also be quiet.
    bool pending_down = false;
    for (const auto& note : f.receiver.notes) {
        check(!note.release_all, "local focus cancellation globally released sibling owners");
        check(note.key == 0, "allocation cancellation addressed another key");
        pending_down = note.down == 1;
    }
    check(!pending_down, "note-on followed its cancellation release on the wire");
    const auto count = f.receiver.notes.size();
    check(!move(*f.view, key1), "allocation cancellation resumed a keyboard drag");
    up(*f.view, key0);
    f.cancel();
    check(f.receiver.notes.size() == count, "allocation cancellation left controller ownership");
}

void audition_cancel_sibling() {
    Fixture f;
    auto sibling = f.make_view();
    check(down(*sibling, key1), "sibling key-down");
    f.receiver.on_note = [&] { f.cancel(); };
    check(down(*f.view, key0), "cancelled primary key-down");
    check(!f.receiver.held[0] && f.receiver.held[1],
          "focus cancellation retained its note or released a sibling's note");
    check(f.receiver.notes.size() == 3, "sibling cancellation wire count");
    f.receiver.note(0, 1, true);
    f.receiver.note(1, 0, true);
    f.receiver.note(2, 0, false);
    check(sibling->cancel_interaction(), "sibling cleanup");
    f.receiver.quiet();
    check(f.receiver.notes.size() == 4, "sibling cleanup duplicated a release");
    f.receiver.note(3, 1, false);
}

struct Case { std::string_view name; void (*run)(); };
const std::array<Case, 26> cases{{
    {"rapid_switches", rapid_switches},
    {"continuous_reset", continuous_reset},
    {"host_shortcuts", host_shortcuts},
    {"fine_steps", fine_steps},
    {"rejected_edits", rejected_edits},
    {"end_cancel_idempotent", end_cancel_idempotent},
    {"begin_cancel_drag", begin_cancel_drag},
    {"begin_cancel_wheel", begin_cancel_wheel},
    {"begin_cancel_switch", begin_cancel_switch},
    {"end_cancel_new_press", end_cancel_new_press},
    {"perform_cancel_drag", perform_cancel_drag},
    {"perform_cancel_host_update", perform_cancel_host_update},
    {"observer_cancel_rollback", observer_cancel_rollback},
    {"observer_cancel_host_changed", observer_cancel_host_changed},
    {"observer_cancel_host_equal", observer_cancel_host_equal},
    {"perform_reject_host_equal", perform_reject_host_equal},
    {"perform_reject_other_parameter", perform_reject_other_parameter},
    {"keyboard_cancel_edit", keyboard_cancel_edit},
    {"keyboard_cancel_tab", keyboard_cancel_tab},
    {"keyboard_audition_cancel_edit", keyboard_audition_cancel_edit},
    {"audition_drag", audition_drag},
    {"audition_glissando_cancel_dispatch", audition_glissando_cancel_dispatch},
    {"audition_glissando_cancel_allocation", audition_glissando_cancel_allocation},
    {"audition_cancel_dispatch", audition_cancel_dispatch},
    {"audition_cancel_allocation", audition_cancel_allocation},
    {"audition_cancel_sibling", audition_cancel_sibling},
}};
} // namespace photo_view_test

int main(int argc, char** argv) {
    using namespace photo_view_test;
    if (argc > 2) {
        std::cerr << "usage: photo_view_interaction_tests [case|all|--list]\n";
        return 2;
    }
    const std::string_view selected = argc == 2 ? argv[1] : "all";
    if (selected == "--list") {
        for (const auto& test : cases) { std::cout << test.name << '\n'; }
        return 0;
    }
    // Events obtain their timestamps from the real platform factory. No native
    // frame, bitmap decoding, display connection or OS event loop is requested.
    gui::init(nullptr);
    std::size_t executed = 0;
    std::size_t failures = 0;
    for (const auto& test : cases) {
        if (selected != "all" && selected != test.name) { continue; }
        ++executed;
        try {
            test.run();
            std::cout << "PASS " << test.name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "FAIL " << test.name << ": " << error.what() << '\n';
        }
    }
    gui::exit();
    if (executed == 0) {
        std::cerr << "unknown case: " << selected << '\n';
        return 2;
    }
    std::cout << "PhotoView interaction: " << executed << " cases, " << checks
              << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
