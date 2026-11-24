#include "photo_editor.hpp"

#include "controller.hpp"
#include "photo_geometry.hpp"
#include "photo_key_highlight.hpp"
#include "photo_native_focus.hpp"

#include "vstgui/plugin-bindings/vst3editor.h"
#include "vstgui/lib/cbitmap.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/events.h"
#include "vstgui/lib/platform/iplatformframe.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

namespace rhodes::vst3 {
namespace {

using Clock = std::chrono::steady_clock;
using ParamID = Steinberg::Vst::ParamID;
using namespace VSTGUI;

constexpr double kDefaultWidth = 1024.0;
constexpr double kDefaultHeight = 768.0;
constexpr double kMinimumWidth = 768.0;
constexpr double kMaximumWidth = 1536.0;
constexpr std::array<int, 7> kTabOrder{{0, 4, 3, 5, 7, 6, 1}};
constexpr auto kFeedbackDuration = std::chrono::milliseconds{1700};

[[nodiscard]] bool succeeded(Steinberg::tresult result) noexcept {
    return result == Steinberg::kResultOk;
}

[[nodiscard]] bool is_switch(int id) noexcept { return id == 1 || id == 7; }
[[nodiscard]] bool is_slider(int id) noexcept { return id == 3 || id == 4; }

[[nodiscard]] const char* control_name(int id) noexcept {
    switch (id) {
    case 0: return "Volume";
    case 1: return "Pedal / Sustain";
    case 3: return "Bass";
    case 4: return "Treble";
    case 5: return "Intensity";
    case 6: return "Speed";
    case 7: return "Vibrato";
    default: return "Control";
    }
}

[[nodiscard]] CRect as_rect(const photo::Rect& r) noexcept {
    return {r.left, r.top, r.right, r.bottom};
}

// Native event positions have already had the frame's zoom and device scaling
// removed by VSTGUI. This final source-image transform is shared by painting,
// pointer hit testing, and the host's parameter finder.
class PhotoView final : public CView {
public:
    PhotoView(RhodesController& controller, CBitmap* photograph)
        : CView(CRect{0.0, 0.0, kDefaultWidth, kDefaultHeight}),
          controller_(controller), audition_owner_(controller.registerAuditionOwner()),
          photograph_(photograph) {
        setWantsFocus(true);
        setWantsIdle(true);
        setTransparency(false);
        setMouseableArea(getViewSize());
    }

    ~PhotoView() override {
        cancel_interaction();
        (void)controller_.unregisterAuditionOwner(audition_owner_);
    }

    void draw(CDrawContext* context) override {
        context->saveGlobalState();
        const auto fit = image_fit();
        if (fit.valid()) {
            const CGraphicsTransform transform{
                fit.scale, 0.0, 0.0, fit.scale,
                getViewSize().left + fit.offset_x, getViewSize().top + fit.offset_y};
            const CDrawContext::Transform scope{*context, transform};
            // No filters, crops, reconstructed controls, or persistent overlays.
            photograph_->draw(context, CRect{0.0, 0.0, photo::kSourceWidth,
                                             photo::kSourceHeight});
            context->setDrawMode(kAntiAliasing);
            if (held_key_ >= 0) { draw_key(*context, held_key_, true); }
            if (feedback_visible()) {
                if (feedback_parameter_ >= 0) {
                    draw_control(*context, feedback_parameter_);
                } else if (feedback_key_ >= 0 && held_key_ < 0 && status_ == nullptr) {
                    draw_key(*context, feedback_key_, false);
                }
                draw_feedback(*context);
            }
        }
        context->restoreGlobalState();
        setDirty(false);
    }

    void setViewSize(const CRect& size, bool invalidate = true) override {
        CView::setViewSize(size, invalidate);
        setMouseableArea(size);
    }

    void set_logical_zoom(double zoom) {
        if (!std::isfinite(zoom) || zoom <= 0.0 || zoom == logical_zoom_) { return; }
        logical_zoom_ = zoom;
        invalid();
    }

    [[nodiscard]] int parameter_at(CPoint position, bool from_frame = false) const {
        if (from_frame) { translateToLocal(position); }
        const auto point = source_position(position);
        return photo::hit_control(point.x, point.y);
    }

    void onMouseDownEvent(MouseDownEvent& event) override {
        if (!event.buttonState.isLeft()) { return; }
        // Discard any loss from the previous interaction before starting this
        // click. The platform assigns native focus after this callback returns.
        (void)native_focus_lost(true);
        const auto cancellation = cancellation_generation_;
        if (!cancel_interaction() || cancellation_generation_ != cancellation + 1) {
            event.consumed = true;
            event.ignoreFollowUpMoveAndUpEvents(true);
            return;
        }
        const auto position = source_position(event.mousePosition);
        const int id = photo::hit_control(position.x, position.y);
        if (id >= 0) {
            focus_parameter(id);
            event.consumed = true;
            // A switch toggles on every press, including rapid click sequences.
            // Double-click reset belongs only to continuous controls.
            if (is_switch(id)) {
                edit_once(id, value(id) >= 0.5 ? 0.0 : 1.0);
                event.ignoreFollowUpMoveAndUpEvents(true);
            } else if (event.clickCount >= 2) {
                edit_once(id, default_value(id));
                event.ignoreFollowUpMoveAndUpEvents(true);
            } else if (begin_gesture(id)) {
                dragging_control_ = true;
                previous_position_ = position;
            } else {
                event.ignoreFollowUpMoveAndUpEvents(true);
            }
            return;
        }
        const int key = photo::hit_key(position.x, position.y);
        if (key >= 0) {
            if (auto* frame = getFrame()) { frame->setFocusView(this); }
            dragging_keyboard_ = true;
            move_audition(key);
            event.consumed = true;
        }
    }

    void onMouseMoveEvent(MouseMoveEvent& event) override {
        if ((dragging_control_ || dragging_keyboard_) && native_focus_lost(true)) {
            cancel_interaction();
            event.consumed = true;
            return;
        }
        const auto position = source_position(event.mousePosition);
        if (dragging_control_ || dragging_keyboard_) {
            event.consumed = true;
            if (!event.buttonState.has(MouseButton::Left)) {
                cancel_interaction();
                return;
            }
            if (dragging_keyboard_) {
                move_audition(photo::hit_key(position.x, position.y));
            } else if (position.finite() && previous_position_.finite()) {
                const int id = gesture_parameter_;
                const auto* control = photo::control_for_parameter(static_cast<ParamID>(id));
                if (control != nullptr) {
                    const double travel = is_slider(id) ?
                        control->bounds.right - control->bounds.left : 180.0;
                    const double motion = is_slider(id) ?
                        position.x - previous_position_.x : previous_position_.y - position.y;
                    const double precision = event.modifiers.has(ModifierKey::Shift) ? 0.1 : 1.0;
                    if (motion != 0.0) {
                        change_value(value(id) + precision * motion / travel);
                    }
                }
                previous_position_ = position;
            }
            return;
        }
        update_hover(position);
    }

    void onMouseUpEvent(MouseUpEvent& event) override {
        if (!dragging_control_ && !dragging_keyboard_) { return; }
        dragging_control_ = false;
        dragging_keyboard_ = false;
        finish_gesture();
        release_key();
        event.consumed = true;
    }

    void onMouseCancelEvent(MouseCancelEvent& event) override {
        cancel_interaction();
        event.consumed = true;
    }

    void onMouseEnterEvent(MouseEnterEvent& event) override {
        if (!dragging_control_ && !dragging_keyboard_) {
            update_hover(source_position(event.mousePosition));
        }
    }

    void onMouseExitEvent(MouseExitEvent&) override {
        hover_parameter_ = -1;
        hover_key_ = -1;
        if (dragging_keyboard_) { release_key(); }
        if (!dragging_control_) { clear_feedback(); }
    }

    void onMouseWheelEvent(MouseWheelEvent& event) override {
        const int id = parameter_at(event.mousePosition);
        if (id < 0) { return; }
        event.consumed = true;
        if (dragging_control_ || dragging_keyboard_) { return; }
        const double delta = event.deltaY != 0.0 ? event.deltaY : event.deltaX;
        if (!std::isfinite(delta) || delta == 0.0) { return; }
        focus_parameter(id);
        const double increment = event.modifiers.has(ModifierKey::Shift) ? 0.002 : 0.02;
        edit_once(id, is_switch(id) ? (delta > 0.0 ? 1.0 : 0.0) : value(id) + delta * increment);
    }

    void onKeyboardEvent(KeyboardEvent& event) override {
        if (event.type != EventType::KeyDown || event.modifiers.has(ModifierKey::Control) ||
            event.modifiers.has(ModifierKey::Alt) || event.modifiers.has(ModifierKey::Super)) {
            return;
        }
        if (event.virt == VirtualKey::Escape) {
            cancel_interaction();
            event.consumed = true;
            return;
        }
        if (event.virt == VirtualKey::Tab) {
            const auto cancellation = cancellation_generation_;
            const bool released = cancel_interaction();
            event.consumed = true;
            if (!released || cancellation_generation_ != cancellation + 1) { return; }
            const auto found = std::find(kTabOrder.begin(), kTabOrder.end(), focused_parameter_);
            int index = found == kTabOrder.end() ? -1 :
                static_cast<int>(std::distance(kTabOrder.begin(), found));
            const int count = static_cast<int>(kTabOrder.size());
            index = event.modifiers.has(ModifierKey::Shift) ?
                (index < 0 ? count - 1 : (index + count - 1) % count) : (index + 1) % count;
            focus_parameter(kTabOrder[static_cast<std::size_t>(index)]);
            event.consumed = true;
            return;
        }
        const int id = focused_parameter_ < 0 ? kTabOrder.front() : focused_parameter_;
        const double step = is_switch(id) ? 1.0 :
            (event.modifiers.has(ModifierKey::Shift) ? 0.001 : 0.01);
        double next = value(id);
        switch (event.virt) {
        case VirtualKey::Left:
        case VirtualKey::Down: next -= step; break;
        case VirtualKey::Right:
        case VirtualKey::Up: next += step; break;
        case VirtualKey::Home: next = 0.0; break;
        case VirtualKey::End: next = 1.0; break;
        case VirtualKey::Space:
        case VirtualKey::Return:
        case VirtualKey::Enter:
            if (!is_switch(id)) { return; }
            if (event.isRepeat) { event.consumed = true; return; }
            next = next >= 0.5 ? 0.0 : 1.0;
            break;
        default: return;
        }
        const auto cancellation = cancellation_generation_;
        const bool released = cancel_interaction();
        event.consumed = true;
        if (!released || cancellation_generation_ != cancellation + 1) { return; }
        focus_parameter(id);
        edit_once(id, next);
        event.consumed = true;
    }

    void looseFocus() override {
        cancel_interaction();
        CView::looseFocus();
    }

    bool wantsWindowActiveStateChangeNotification() const override { return true; }
    void onWindowActivate(bool active) override {
        if (!active) { cancel_interaction(); }
    }

    bool removed(CView* parent) override {
        cancel_interaction();
        native_focus_.reset();
        return CView::removed(parent);
    }

    void onIdle() override {
        if (native_focus_lost()) { cancel_interaction(); }
        (void)controller_.retryAuditionRelease();
        if (held_key_ >= 0 && controller_.heldAuditionKey(audition_owner_) != held_key_) {
            // Another view's failed transport cancels all V1 UI ownership.
            // Its emergency must not leave this view showing a held key.
            held_key_ = -1;
            dragging_keyboard_ = false;
            clear_feedback();
            invalid();
        }
        if (feedback_parameter_ < 0 && feedback_key_ < 0) { return; }
        if (!feedback_visible()) {
            clear_feedback();
        } else if (feedback_parameter_ >= 0) {
            // Read the same controller parameter used by automation and recall.
            // There is no parallel GUI value or GUI-to-audio pointer.
            const double current = value(feedback_parameter_);
            if (current != last_displayed_value_) {
                last_displayed_value_ = current;
                invalid();
            }
        }
    }

    bool cancel_interaction() {
        ++cancellation_generation_;
        dragging_control_ = false;
        dragging_keyboard_ = false;
        finish_gesture();
        const bool released = release_key();
        hover_parameter_ = -1;
        hover_key_ = -1;
        clear_feedback();
        return released;
    }

private:
    [[nodiscard]] bool native_focus_lost(bool synchronize = false) {
#if defined(__linux__)
        auto* frame = getFrame();
        auto* platform = frame == nullptr ? nullptr : frame->getPlatformFrame();
        const auto window = platform != nullptr &&
                platform->getPlatformType() == PlatformType::kX11EmbedWindowID ?
            reinterpret_cast<std::uintptr_t>(platform->getPlatformRepresentation()) : 0;
        native_focus_.attach(window);
#endif
        return native_focus_.poll(synchronize);
    }

    [[nodiscard]] photo::AspectFit image_fit() const noexcept {
        return photo::AspectFit::fit(getWidth(), getHeight());
    }

    [[nodiscard]] photo::Point source_position(const CPoint& position) const noexcept {
        return image_fit().view_to_source(
            {position.x - getViewSize().left, position.y - getViewSize().top});
    }

    [[nodiscard]] double value(int id) const {
        return controller_.getParamNormalized(static_cast<ParamID>(id));
    }

    [[nodiscard]] double default_value(int id) const {
        auto* parameter = controller_.getParameterObject(static_cast<ParamID>(id));
        return parameter == nullptr ? 0.0 : parameter->getInfo().defaultNormalizedValue;
    }

    bool begin_gesture(int id) {
        const auto cancellation = cancellation_generation_;
        finish_gesture();
        if (cancellation_generation_ != cancellation) { return false; }
        const auto tag = static_cast<ParamID>(id);
        const bool accepted = controller_.getParameterObject(tag) != nullptr &&
            succeeded(controller_.beginEdit(tag));
        if (cancellation_generation_ != cancellation) {
            // A synchronous callback can cancel before beginEdit returns. The
            // accepted begin still needs exactly one matching end, but must
            // never publish a gesture or restart the canceled drag.
            if (accepted) { (void)controller_.endEdit(tag); }
            return false;
        }
        if (!accepted) {
            show_parameter(id, "Host unavailable for editing");
            return false;
        }
        gesture_parameter_ = id;
        show_parameter(id);
        return true;
    }

    void change_value(double next) {
        const int id = gesture_parameter_;
        if (id < 0 || !std::isfinite(next)) { return; }
        const auto tag = static_cast<ParamID>(id);
        next = std::clamp(next, 0.0, 1.0);
        const double previous = value(id);
        const auto cancellation = cancellation_generation_;
        const auto submitted_revision = controller_.parameterRevision(tag) + 1;
        if (!succeeded(controller_.setParamNormalized(tag, next))) {
            if (cancellation_generation_ == cancellation) {
                show_parameter(id, "Parameter edit rejected");
            }
            return;
        }
        // SDK parameter observers run synchronously inside the setter. A
        // cancellation may already have ended this gesture, or a newer host
        // write may have replaced even an equal-valued provisional edit.
        if (cancellation_generation_ != cancellation) {
            if (controller_.parameterRevision(tag) == submitted_revision) {
                (void)controller_.setParamNormalized(tag, previous);
            }
            return;
        }
        if (controller_.parameterRevision(tag) != submitted_revision) { return; }
        // The controller may quantize a normalized value. Send that canonical
        // value to the host, rather than a different GUI-side approximation.
        if (!succeeded(controller_.performEdit(tag, value(id)))) {
            if (controller_.parameterRevision(tag) == submitted_revision) {
                (void)controller_.setParamNormalized(tag, previous);
            }
            if (cancellation_generation_ == cancellation) {
                show_parameter(id, "Host rejected parameter edit");
            }
            return;
        }
        if (cancellation_generation_ == cancellation) { show_parameter(id); }
    }

    void finish_gesture() {
        if (gesture_parameter_ < 0) { return; }
        const int id = gesture_parameter_;
        const auto cancellation = cancellation_generation_;
        // Clear before calling the host, whose callback can re-enter teardown.
        gesture_parameter_ = -1;
        if (!succeeded(controller_.endEdit(static_cast<ParamID>(id))) &&
            cancellation_generation_ == cancellation) {
            show_parameter(id, "Host did not acknowledge end edit");
        }
    }

    void edit_once(int id, double next) {
        if (!begin_gesture(id)) { return; }
        change_value(next);
        finish_gesture();
    }

    void focus_parameter(int id) {
        focused_parameter_ = id;
        if (auto* frame = getFrame()) { frame->setFocusView(this); }
        show_parameter(id);
    }

    bool release_key() {
        if (held_key_ < 0) { return true; }
        const int key = held_key_;
        held_key_ = -1;
        const bool released = controller_.auditionNote(audition_owner_, key, false, 0.0F);
        invalid();
        return released;
    }

    void move_audition(int key) {
        if (key == held_key_ && controller_.heldAuditionKey(audition_owner_) == key) { return; }
        const auto cancellation = cancellation_generation_;
        const bool released = release_key();
        if (cancellation_generation_ != cancellation) { return; }
        if (key < 0) { clear_feedback(); return; }
        const bool accepted = released &&
            controller_.auditionNote(audition_owner_, key, true, 0.75F);
        if (cancellation_generation_ != cancellation) {
            // Allocation/dispatch callbacks can cancel after controller
            // ownership is assigned but before its note-down is delivered.
            // Release only this view, after that pending down has returned.
            if (accepted && controller_.heldAuditionKey(audition_owner_) == key) {
                (void)controller_.auditionNote(audition_owner_, key, false, 0.0F);
            }
            return;
        }
        feedback_parameter_ = -1;
        feedback_key_ = key;
        status_ = nullptr;
        if (accepted) {
            held_key_ = key;
        } else {
            status_ = "Audition unavailable";
        }
        feedback_until_ = Clock::now() + kFeedbackDuration;
        invalid();
    }

    void update_hover(photo::Point position) {
        const int parameter = photo::hit_control(position.x, position.y);
        const int key = parameter < 0 ? photo::hit_key(position.x, position.y) : -1;
        if (parameter == hover_parameter_ && key == hover_key_ &&
            (feedback_visible() || (parameter < 0 && key < 0))) { return; }
        hover_parameter_ = parameter;
        hover_key_ = key;
        if (parameter >= 0) {
            show_parameter(parameter);
        } else if (key >= 0) {
            feedback_parameter_ = -1;
            feedback_key_ = key;
            status_ = nullptr;
            feedback_until_ = Clock::now() + kFeedbackDuration;
            invalid();
        } else {
            clear_feedback();
        }
    }

    [[nodiscard]] bool feedback_visible() const noexcept {
        return (feedback_parameter_ >= 0 || feedback_key_ >= 0) &&
               (dragging_control_ || Clock::now() < feedback_until_);
    }

    void show_parameter(int id, const char* status = nullptr) {
        feedback_parameter_ = id;
        feedback_key_ = -1;
        status_ = status;
        feedback_until_ = Clock::now() + kFeedbackDuration;
        last_displayed_value_ = value(id);
        invalid();
    }

    void clear_feedback() {
        const bool had_feedback = feedback_parameter_ >= 0 || feedback_key_ >= 0;
        feedback_parameter_ = -1;
        feedback_key_ = -1;
        status_ = nullptr;
        if (had_feedback) { invalid(); }
    }

    static void draw_key(CDrawContext& context, int key, bool pressed) {
        const auto& shape = photo::key_highlight(key);
        if (shape.visible_path_count == 0) { return; }
        auto path = owned(context.createGraphicsPath());
        if (!path) { return; }
        // Submit only exposed contours, including disconnected rear slivers.
        // No edge crosses a black interior and relies on another subpath to
        // cancel it after the native rasterizer quantizes its coordinates.
        for (std::size_t part = 0; part < shape.visible_path_count; ++part) {
            const auto& contour = shape.visible_paths[part];
            if (contour.size < 3) { continue; }
            path->beginSubpath(CPoint{contour.points[0].x, contour.points[0].y});
            for (std::size_t index = 1; index < contour.size; ++index) {
                path->addLine(CPoint{contour.points[index].x, contour.points[index].y});
            }
            path->closeSubpath();
        }
        // Preserve the original fractional photo-space boundaries.
        context.saveGlobalState();
        context.setDrawMode(kAntiAliasing | kNonIntegralMode);
        context.setFillColor(CColor{245, 213, 150, static_cast<uint8_t>(pressed ? 80 : 28)});
        context.setFrameColor(CColor{255, 234, 190, 185});
        context.setLineWidth(1.5);
        context.drawGraphicsPath(path, CDrawContext::kPathFilled);
        if (shape.stroke_outline) {
            context.drawGraphicsPath(path, CDrawContext::kPathStroked);
        }
        context.restoreGlobalState();
    }

    static void draw_control(CDrawContext& context, int id) {
        const auto* control = photo::control_for_parameter(static_cast<ParamID>(id));
        if (control == nullptr) { return; }
        const auto bounds = as_rect(control->bounds);
        context.setFrameColor(CColor{255, 232, 184, 220});
        context.setLineWidth(2.0);
        if (id == 0 || id == 5 || id == 6) { context.drawEllipse(bounds); }
        else { context.drawRect(bounds); }
    }

    void draw_feedback(CDrawContext& context) const {
        photo::Rect anchor{};
        std::array<char, 256> text{};
        const char* help = "Click and drag to audition; release the key";
        if (feedback_parameter_ >= 0) {
            const auto tag = static_cast<ParamID>(feedback_parameter_);
            const auto* control = photo::control_for_parameter(tag);
            if (control == nullptr) { return; }
            anchor = control->bounds;
            Steinberg::Vst::String128 formatted{};
            std::array<char, 128> ascii{};
            auto* parameter = controller_.getParameterObject(tag);
            if (parameter != nullptr && parameter->getInfo().stepCount == 0) {
                const double plain = parameter->toPlain(value(feedback_parameter_));
                (void)std::snprintf(ascii.data(), ascii.size(), "%.2f",
                                    std::abs(plain) < 0.005 ? 0.0 : plain);
                auto length = std::strlen(ascii.data());
                while (length > 0 && ascii[length - 1] == '0') { --length; }
                if (length > 0 && (ascii[length - 1] == '.' || ascii[length - 1] == ',')) {
                    --length;
                }
                const auto* units = parameter->getInfo().units;
                if (units[0] != 0 && length + 2 < ascii.size()) {
                    ascii[length++] = ' ';
                    for (std::size_t i = 0; length + 1 < ascii.size() && units[i] != 0; ++i) {
                        ascii[length++] = units[i] <= 127 ? static_cast<char>(units[i]) : '?';
                    }
                }
                ascii[length] = '\0';
            } else if (succeeded(controller_.getParamStringByValue(
                           tag, value(feedback_parameter_), formatted))) {
                for (std::size_t i = 0; i + 1 < ascii.size() && formatted[i] != 0; ++i) {
                    ascii[i] = formatted[i] <= 127 ? static_cast<char>(formatted[i]) : '?';
                }
            }
            (void)std::snprintf(text.data(), text.size(), "%s: %s",
                                control_name(feedback_parameter_), ascii.data());
            help = is_switch(feedback_parameter_) ? "Click / Space to switch; Home off, End on" :
                (is_slider(feedback_parameter_) ? "Drag horizontally; Shift for fine control" :
                                                "Drag vertically; Shift for fine control");
        } else {
            anchor = photo::key_polygon(feedback_key_).bounds();
            (void)std::snprintf(text.data(), text.size(), "Keyboard - MIDI %d",
                                photo::kFirstMidiNote + feedback_key_);
        }
        if (!anchor.valid()) { return; }
        // Keep transient help readable at the smallest logical editor size.
        // The photograph and hit regions keep their original shared transform.
        const double scale = std::clamp(0.75 / (image_fit().scale * logical_zoom_), 1.0, 2.0);
        const double width = 464.0 * scale;
        const double left = std::clamp(anchor.center().x - width * 0.5,
                                       12.0, photo::kSourceWidth - width - 12.0);
        const double top = std::max(12.0, anchor.top - 78.0 * scale);
        const CRect box{left, top, left + width, top + 66.0 * scale};
        context.setFillColor(CColor{28, 27, 25, 232});
        context.setFrameColor(CColor{231, 217, 187, 190});
        context.setLineWidth(1.0);
        context.drawRect(box, kDrawFilledAndStroked);
        context.setFontColor(CColor{255, 246, 225, 255});
        context.setFont(kNormalFont, 21.0 * scale);
        context.drawString(status_ == nullptr ? text.data() : status_,
                           CRect{left + 12.0 * scale, top + 5.0 * scale,
                                 left + 452.0 * scale, top + 34.0 * scale}, kLeftText);
        context.setFont(kNormalFont, 16.0 * scale);
        context.drawString(help, CRect{left + 12.0 * scale, top + 34.0 * scale,
                                      left + 452.0 * scale, top + 60.0 * scale}, kLeftText);
    }

    RhodesController& controller_;
    const RhodesController::AuditionOwner audition_owner_;
    SharedPointer<CBitmap> photograph_;
    PhotoNativeFocus native_focus_;
    std::uint64_t cancellation_generation_ = 0;
    int gesture_parameter_ = -1;
    int focused_parameter_ = -1;
    int hover_parameter_ = -1;
    int hover_key_ = -1;
    int feedback_parameter_ = -1;
    int feedback_key_ = -1;
    int held_key_ = -1;
    bool dragging_control_ = false;
    bool dragging_keyboard_ = false;
    photo::Point previous_position_{};
    Clock::time_point feedback_until_{};
    double last_displayed_value_ = 0.0;
    double logical_zoom_ = 1.0;
    const char* status_ = nullptr;
};

// Keep limits in logical pixels. The SDK aspect-ratio class has no maximum
// zoom, and its cached initial size may already contain the host's DPI factor.
// These hooks instead derive every physical size from the fixed 1024x768 canvas.
class PhotoEditor final : public AspectRatioVST3Editor, public VST3EditorDelegate {
public:
    explicit PhotoEditor(RhodesController& owner)
        : AspectRatioVST3Editor(&owner, "RhodesPhotoEditor", "rhodes-suitcase.uidesc"),
          rhodes_controller_(owner) {
        setDelegate(this);
        enableTooltips(false); // The bounded transient readout belongs to PhotoView.
        setMinZoomFactor(0.75);
        setEditorSizeConstrains(CPoint{768.0, 576.0}, CPoint{1536.0, 1152.0});
        enableShowEditButton(false);
    }

    ~PhotoEditor() override {
        if (getFrame() != nullptr) { close(); }
        setDelegate(nullptr);
    }

    Steinberg::tresult PLUGIN_API onFocus(Steinberg::TBool active) override {
        if (!active) {
            if (view_) { view_->cancel_interaction(); }
        }
        return Steinberg::kResultTrue;
    }

    CView* createCustomView(UTF8StringPtr name, const UIAttributes&,
                            const IUIDescription* resources, VST3Editor*) override {
        if (name == nullptr || std::strcmp(name, "RhodesPhotoView") != 0) { return nullptr; }
        auto* bitmap = resources->getBitmap("rhodes-suitcase");
        if (!valid_bitmap(bitmap)) { return nullptr; }
        view_ = new PhotoView(rhodes_controller_, bitmap);
        return view_;
    }

    bool findParameter(const CPoint& position, ParamID& id, VST3Editor*) override {
        if (view_ == nullptr) { return false; }
        const int found = view_->parameter_at(position, true);
        if (found < 0) { return false; }
        id = static_cast<ParamID>(found);
        return true;
    }

    void didOpen(VST3Editor*) override {
        if (getFrame()) { getFrame()->setFocusDrawingEnabled(false); }
        if (view_) { view_->set_logical_zoom(getZoomFactor()); }
    }

    void willClose(VST3Editor*) override {
        if (view_) { view_->cancel_interaction(); }
        view_ = nullptr;
    }

protected:
    void onViewRemoved(CFrame* owner_frame, CView* view) override {
        if (view == view_) {
            view_->cancel_interaction();
            view_ = nullptr;
        }
        VST3Editor::onViewRemoved(owner_frame, view);
    }

    bool PLUGIN_API open(void* parent, const PlatformType& platform) override {
        if (getFrame() != nullptr ||
            !valid_bitmap(getUIDescription()->getBitmap("rhodes-suitcase"))) { return false; }
        // Our size hooks handle DPI and both limits; do not seed the SDK's
        // separate initial-size/zoom cache with an already scaled frame.
        const bool opened = VST3Editor::open(parent, platform);
        if (!opened || getFrame() == nullptr || getFrame()->getPlatformFrame() == nullptr ||
            view_ == nullptr) {
            if (getFrame() != nullptr) { close(); }
            view_ = nullptr;
            return false;
        }
        return true;
    }

    void PLUGIN_API close() override {
        if (getFrame() != nullptr) { VST3Editor::close(); }
        view_ = nullptr;
    }

    Steinberg::tresult PLUGIN_API checkSizeConstraint(Steinberg::ViewRect* size) override {
        return constrain_size(size, getContentScaleFactor());
    }

    bool beforeSizeChange(const CRect& next, const CRect& previous) override {
        // onSize already represents the host's resize decision. CFrame::setZoom
        // must not turn its native size update into another host request.
        return resizing_ || VST3Editor::beforeSizeChange(next, previous);
    }

    Steinberg::tresult PLUGIN_API onSize(Steinberg::ViewRect* size) override {
        if (size == nullptr) { return Steinberg::kInvalidArgument; }
        if (resizing_) { return Steinberg::kResultFalse; }
        auto constrained = *size;
        const auto result = checkSizeConstraint(&constrained);
        if (!succeeded(result)) { return result; }
        if (constrained.right != size->right || constrained.bottom != size->bottom) {
            return Steinberg::kResultFalse;
        }
        // Preserve the SDK's pending-request check before touching the native
        // frame; its base onSize performs this same check when committing rect.
        if (sizeRequest &&
            (std::floor(sizeRequest->x) != size->getWidth() ||
             std::floor(sizeRequest->y) != size->getHeight())) {
            return Steinberg::kResultFalse;
        }
        const double next_zoom = static_cast<double>(size->getWidth()) /
                                 (kDefaultWidth * getContentScaleFactor());
        const double previous_zoom = zoomFactor;
        zoomFactor = next_zoom;
        if (getFrame() != nullptr && getFrame()->hasChildren()) {
            resizing_ = true;
            const bool accepted = getFrame()->setZoom(getAbsScaleFactor());
            resizing_ = false;
            if (!accepted) {
                zoomFactor = previous_zoom;
                return Steinberg::kResultFalse;
            }
        }
        const auto resized = VST3Editor::onSize(size);
        if (succeeded(resized) && view_) { view_->set_logical_zoom(next_zoom); }
        return resized;
    }

#ifdef VST3_CONTENT_SCALE_SUPPORT
    Steinberg::tresult PLUGIN_API setContentScaleFactor(ScaleFactor factor) override {
        if (scaling_ || resizing_) { return Steinberg::kResultFalse; }
        Steinberg::ViewRect next{};
        if (!std::isfinite(factor) || factor <= 0.0F ||
            kMaximumWidth * factor > std::numeric_limits<Steinberg::int32>::max()) {
            return Steinberg::kInvalidArgument;
        }
        next.right = static_cast<Steinberg::int32>(std::round(kDefaultWidth * getZoomFactor() * factor));
        next.bottom = static_cast<Steinberg::int32>(std::round(kDefaultHeight * getZoomFactor() * factor));
        if (!succeeded(constrain_size(&next, factor))) { return Steinberg::kInvalidArgument; }
        const double old_scale = contentScaleFactor;
        const double old_zoom = zoomFactor;
        auto old_rect = rect;
        scaling_ = true;
        contentScaleFactor = factor;
        zoomFactor = static_cast<double>(next.getWidth()) / (kDefaultWidth * factor);

        // IPlugFrame can exist before the native CFrame. In either case, keep
        // getSize() unchanged until the host synchronously calls onSize().
        // Only the no-IPlugFrame path may commit the requested size directly.
        const auto result = plugFrame ? plugFrame->resizeView(this, &next) : onSize(&next);
        const bool accepted = succeeded(result) && rect.left == next.left && rect.top == next.top &&
                              rect.right == next.right && rect.bottom == next.bottom;
        if (!accepted) {
            contentScaleFactor = old_scale;
            zoomFactor = old_zoom;
            if (rect.left != old_rect.left || rect.top != old_rect.top ||
                rect.right != old_rect.right || rect.bottom != old_rect.bottom) {
                // Also restore a host callback followed by a refused request.
                // onSize applies the native rollback without another resizeView.
                (void)onSize(&old_rect);
            }
            rect = old_rect;
            zoomFactor = old_zoom;
        }
        scaling_ = false;
        return accepted ? Steinberg::kResultTrue : Steinberg::kResultFalse;
    }
#endif

private:
    static bool valid_bitmap(CBitmap* bitmap) noexcept {
        return bitmap != nullptr && bitmap->isLoaded() &&
               bitmap->getWidth() == photo::kSourceWidth &&
               bitmap->getHeight() == photo::kSourceHeight;
    }

    static Steinberg::tresult constrain_size(Steinberg::ViewRect* size, double scale) {
        if (size == nullptr || !std::isfinite(scale) || scale <= 0.0) {
            return Steinberg::kInvalidArgument;
        }
        // A four-pixel width quantum gives an exact integer 4:3 native surface.
        const double lower = std::ceil(kMinimumWidth * scale / 4.0);
        const double upper = std::floor(kMaximumWidth * scale / 4.0);
        if (lower < 1.0 || upper < lower ||
            upper > std::numeric_limits<Steinberg::int32>::max() / 4.0) {
            return Steinberg::kInvalidArgument;
        }
        const double requested = static_cast<double>(static_cast<std::int64_t>(size->right) - size->left);
        const auto units = static_cast<std::int64_t>(std::clamp(std::round(requested / 4.0), lower, upper));
        const auto right = static_cast<std::int64_t>(size->left) + units * 4;
        const auto bottom = static_cast<std::int64_t>(size->top) + units * 3;
        if (right > std::numeric_limits<Steinberg::int32>::max() ||
            bottom > std::numeric_limits<Steinberg::int32>::max()) {
            return Steinberg::kInvalidArgument;
        }
        size->right = static_cast<Steinberg::int32>(right);
        size->bottom = static_cast<Steinberg::int32>(bottom);
        return Steinberg::kResultTrue;
    }

    RhodesController& rhodes_controller_;
    PhotoView* view_ = nullptr; // Owned by the editor frame, cleared before teardown.
    bool resizing_ = false;
    bool scaling_ = false;
};

} // namespace

Steinberg::IPlugView* create_photo_editor(RhodesController* controller) {
    return controller == nullptr ? nullptr : new PhotoEditor(*controller);
}

} // namespace rhodes::vst3
