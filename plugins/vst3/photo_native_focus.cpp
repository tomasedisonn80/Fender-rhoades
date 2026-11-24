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
    host_parent_ = 0;
    host_top_ = 0;
    focus_proxy_ = 0;
    cold_focus_ = ColdFocus::disabled;
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
    const auto subscribe = [connection](std::uintptr_t window) {
        const std::uint32_t mask = XCB_EVENT_MASK_FOCUS_CHANGE | XCB_EVENT_MASK_STRUCTURE_NOTIFY;
        const auto cookie = xcb_change_window_attributes_checked(
            connection, static_cast<xcb_window_t>(window), XCB_CW_EVENT_MASK, &mask);
        auto* error = xcb_request_check(connection, cookie);
        const bool success = error == nullptr && xcb_connection_has_error(connection) == 0;
        std::free(error);
        return success;
    };
    if (!subscribe(window_)) {
        xcb_disconnect(connection);
        return;
    }

    // Cache only the actual embedder and, when directly below the root, its
    // immediate hosting window. REAPER's floating editor uses this extra level.
    // Three QueryTree requests bound this discovery; the root is never a proxy.
    auto* tree = xcb_query_tree_reply(
        connection, xcb_query_tree(connection, static_cast<xcb_window_t>(window_)), nullptr);
    const xcb_window_t root = tree == nullptr ? 0U : tree->root;
    if (tree != nullptr && tree->parent != root) { host_parent_ = tree->parent; }
    std::free(tree);
    if (host_parent_ != 0) {
        tree = xcb_query_tree_reply(connection,
            xcb_query_tree(connection, static_cast<xcb_window_t>(host_parent_)), nullptr);
        const xcb_window_t candidate = tree == nullptr ? 0U : tree->parent;
        std::free(tree);
        if (candidate != XCB_WINDOW_NONE && candidate != root) {
            tree = xcb_query_tree_reply(connection, xcb_query_tree(connection, candidate), nullptr);
            if (tree != nullptr && tree->root == root && tree->parent == root) {
                host_top_ = candidate;
            }
            std::free(tree);
        }
        if (!subscribe(host_parent_) || (host_top_ != 0 && !subscribe(host_top_))) {
            host_parent_ = 0;
            host_top_ = 0;
        }
    }
    auto* focus = xcb_get_input_focus_reply(connection, xcb_get_input_focus(connection), nullptr);
    if (host_parent_ != 0 && focus != nullptr && focus->focus == XCB_INPUT_FOCUS_POINTER_ROOT) {
        cold_focus_ = ColdFocus::pointer_root;
    }
    std::free(focus);
    connection_ = connection;
#endif
}

bool PhotoNativeFocus::poll(bool synchronize) noexcept {
#if defined(__linux__)
    auto* connection = static_cast<xcb_connection_t*>(connection_);
    if (connection == nullptr) { return false; }
    const auto fence = [connection] {
        // The reply is an ordering fence, not the loss decision: polling only
        // the current focus would miss a genuine loss followed by a quick return.
        auto* reply = xcb_get_input_focus_reply(
            connection, xcb_get_input_focus(connection), nullptr);
        const bool success = reply != nullptr;
        std::free(reply);
        return success;
    };
    if (synchronize && !fence()) {
        xcb_disconnect(connection);
        connection_ = nullptr;
        cold_focus_ = ColdFocus::disabled;
        focus_proxy_ = 0;
        return true;
    }

    bool lost = false;
    enum class Pending { none, cold_handoff, proxy_return };
    Pending pending = Pending::none;
    bool through_parent = false;
    bool pair_fenced = false;
    bool fence_failed = false;
    constexpr unsigned kMaximumEventsPerPoll = 64;
    unsigned count = 0;
    while (count < kMaximumEventsPerPoll) {
        auto* event = xcb_poll_for_event(connection);
        if (event == nullptr) {
            // A FocusOut/FocusIn pair can straddle socket reads. Resolve it on
            // this UI callback, with at most one extra reply and the same budget.
            if (pending != Pending::none && !pair_fenced) {
                pair_fenced = true;
                if (fence()) { continue; }
                fence_failed = true;
                lost = true;
            }
            break;
        }
        ++count;
        const auto type = event->response_type & 0x7fU;
        std::uintptr_t changed_window = 0;
        if (type == XCB_REPARENT_NOTIFY) {
            changed_window = reinterpret_cast<xcb_reparent_notify_event_t*>(event)->window;
        } else if (type == XCB_DESTROY_NOTIFY) {
            changed_window = reinterpret_cast<xcb_destroy_notify_event_t*>(event)->window;
        }
        if (changed_window != 0 && (changed_window == window_ || changed_window == host_parent_ ||
                                    changed_window == host_top_)) {
            // A cached embedding relationship never survives reparent/removal.
            host_parent_ = 0;
            host_top_ = 0;
            lost = true;
        }
        if (!lost && (type == XCB_FOCUS_OUT || type == XCB_FOCUS_IN)) {
            const auto& focus = *reinterpret_cast<const xcb_focus_out_event_t*>(event);
            const bool out = type == XCB_FOCUS_OUT;
            const bool normal = focus.mode == XCB_NOTIFY_MODE_NORMAL;
            const bool actual = focus.mode != XCB_NOTIFY_MODE_GRAB &&
                                focus.mode != XCB_NOTIFY_MODE_UNGRAB;
            if (actual && focus.event == window_) {
                if (out) {
                    if (focus.detail == XCB_NOTIFY_DETAIL_POINTER) {
                        if (normal && cold_focus_ == ColdFocus::pointer_root) {
                            cold_focus_ = ColdFocus::pointer_out;
                        }
                    } else if (focus.detail == XCB_NOTIFY_DETAIL_INFERIOR) {
                        cold_focus_ = ColdFocus::disabled;
                    } else if (normal && focus.detail == XCB_NOTIFY_DETAIL_ANCESTOR &&
                               cold_focus_ == ColdFocus::editor_in) {
                        // Only the first PointerRoot acquisition can negotiate
                        // focus back to the host, and only a matching event on
                        // the exact cached parent/top can complete that transfer.
                        cold_focus_ = ColdFocus::disabled;
                        pending = Pending::cold_handoff;
                        through_parent = false;
                    } else {
                        lost = true;
                    }
                } else if (pending == Pending::proxy_return &&
                           (focus.detail == XCB_NOTIFY_DETAIL_ANCESTOR ||
                            focus.detail == XCB_NOTIFY_DETAIL_VIRTUAL)) {
                    pending = Pending::none;
                } else if (pending != Pending::none) {
                    lost = true;
                } else if (normal && focus.detail == XCB_NOTIFY_DETAIL_NONLINEAR &&
                           cold_focus_ == ColdFocus::pointer_out) {
                    cold_focus_ = ColdFocus::editor_in;
                } else if (focus.detail != XCB_NOTIFY_DETAIL_POINTER) {
                    cold_focus_ = ColdFocus::disabled;
                }
            } else if (actual && (focus.event == host_parent_ || focus.event == host_top_)) {
                if (pending == Pending::cold_handoff) {
                    if (normal && out && focus.event == host_parent_ && host_top_ != 0 &&
                        focus.detail == XCB_NOTIFY_DETAIL_VIRTUAL && !through_parent) {
                        through_parent = true;
                    } else if (normal && !out && focus.detail == XCB_NOTIFY_DETAIL_INFERIOR &&
                               ((!through_parent && focus.event == host_parent_) ||
                                (through_parent && focus.event == host_top_))) {
                        focus_proxy_ = focus.event;
                        pending = Pending::none;
                    } else {
                        lost = true;
                    }
                } else if (pending == Pending::proxy_return) {
                    // Focus travelling from the outer host to the editor may
                    // traverse its immediate embedder; other host widgets lose.
                    if (out || focus.event != host_parent_ ||
                        focus.detail != XCB_NOTIFY_DETAIL_VIRTUAL) {
                        lost = true;
                    }
                } else if (out && focus.event == focus_proxy_ &&
                           focus.detail != XCB_NOTIFY_DETAIL_POINTER) {
                    focus_proxy_ = 0;
                    if (focus.detail == XCB_NOTIFY_DETAIL_INFERIOR) {
                        pending = Pending::proxy_return;
                    } else {
                        lost = true;
                    }
                }
            }
        }
        std::free(event);
        // An unexpected event burst must not let a held drag run ahead of a
        // queued loss. Cancel conservatively and finish draining on later idle.
        if (count == kMaximumEventsPerPoll) { lost = true; }
    }
    if (pending != Pending::none) { lost = true; }
    // Once a synchronized pointer callback accepts explicit editor focus, a
    // later upward transfer is an ordinary loss, even to this same host window.
    if (synchronize && cold_focus_ == ColdFocus::editor_in) {
        cold_focus_ = ColdFocus::disabled;
    }
    if (fence_failed || xcb_connection_has_error(connection) != 0) {
        xcb_disconnect(connection);
        connection_ = nullptr;
        lost = true;
    }
    if (lost) {
        cold_focus_ = ColdFocus::disabled;
        focus_proxy_ = 0;
    }
    return lost;
#else
    (void)synchronize;
    return false;
#endif
}

} // namespace rhodes::vst3
