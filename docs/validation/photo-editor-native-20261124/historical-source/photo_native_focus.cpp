#include "photo_native_focus.hpp"

#if defined(__linux__)
#include <xcb/xcb.h>

#include <cstdlib>
#include <limits>
#endif

namespace rhodes::vst3 {

PhotoNativeFocus::~PhotoNativeFocus() { reset(); }

void PhotoNativeFocus::reset() noexcept {
#if defined(__linux__)
    if (connection_ != nullptr) {
        xcb_disconnect(static_cast<xcb_connection_t*>(connection_));
    }
#endif
    connection_ = nullptr;
    window_ = 0;
}

void PhotoNativeFocus::attach(std::uintptr_t x11_window) noexcept {
    if (window_ == x11_window) { return; }
    reset();
    window_ = x11_window;
#if defined(__linux__)
    if (window_ == 0 || window_ > std::numeric_limits<xcb_window_t>::max()) { return; }
    auto* connection = xcb_connect(nullptr, nullptr);
    if (connection == nullptr) { return; }
    if (xcb_connection_has_error(connection) != 0) {
        xcb_disconnect(connection);
        return;
    }

    // Event masks belong to a client connection. Selecting FocusChange here
    // preserves the SDK's mask and gives this editor its own copy of the events.
    const std::uint32_t mask = XCB_EVENT_MASK_FOCUS_CHANGE;
    const auto cookie = xcb_change_window_attributes_checked(
        connection, static_cast<xcb_window_t>(window_), XCB_CW_EVENT_MASK, &mask);
    auto* error = xcb_request_check(connection, cookie);
    const bool failed = error != nullptr || xcb_connection_has_error(connection) != 0;
    std::free(error);
    if (failed) {
        xcb_disconnect(connection);
        return;
    }
    connection_ = connection;
#endif
}

bool PhotoNativeFocus::poll(bool synchronize) noexcept {
#if defined(__linux__)
    auto* connection = static_cast<xcb_connection_t*>(connection_);
    if (connection == nullptr) { return false; }
    if (synchronize) {
        // The reply is an ordering fence, not the loss decision: polling only
        // the current focus would miss a genuine loss followed by a quick return.
        auto* reply = xcb_get_input_focus_reply(
            connection, xcb_get_input_focus(connection), nullptr);
        const bool failed = reply == nullptr;
        std::free(reply);
        if (failed) {
            xcb_disconnect(connection);
            connection_ = nullptr;
            return true;
        }
    }

    bool lost = false;
    constexpr unsigned kMaximumEventsPerPoll = 64;
    for (unsigned index = 0; index < kMaximumEventsPerPoll; ++index) {
        auto* event = xcb_poll_for_event(connection);
        if (event == nullptr) { break; }
        if ((event->response_type & 0x7fU) == XCB_FOCUS_OUT) {
            const auto& focus = *reinterpret_cast<const xcb_focus_out_event_t*>(event);
            // Focus moving into a native descendant remains inside this view.
            // Pointer-focus bookkeeping also occurs when the first click gives
            // this window explicit focus; it does not mean that focus was lost.
            // Keyboard-grab/ungrab notifications (for example a popup) do not
            // change input focus. Actual changes while grabbed still count.
            if (focus.event == window_ && focus.detail != XCB_NOTIFY_DETAIL_INFERIOR &&
                focus.detail != XCB_NOTIFY_DETAIL_POINTER &&
                focus.mode != XCB_NOTIFY_MODE_GRAB && focus.mode != XCB_NOTIFY_MODE_UNGRAB) {
                lost = true;
            }
        }
        std::free(event);
        // An unexpected event burst must not let a held drag run ahead of a
        // queued loss. Cancel conservatively and finish draining on later idle.
        if (index + 1 == kMaximumEventsPerPoll) { lost = true; }
    }
    if (xcb_connection_has_error(connection) != 0) {
        xcb_disconnect(connection);
        connection_ = nullptr;
        lost = true;
    }
    return lost;
#else
    (void)synchronize;
    return false;
#endif
}

} // namespace rhodes::vst3
