// Linux/X11 regression, registered as vst3.photo_native_focus for editor builds.
// CTest uses tools/run_xvfb_test.py for its own authenticated Xvfb server.
// rhodes_vst3_checks includes this executable; it links only the helper and libxcb.
#include "plugins/vst3/photo_native_focus.hpp"

#include <xcb/xcb.h>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

using rhodes::vst3::PhotoNativeFocus;

namespace {
void check(bool success, const char* name) {
    std::cout << (success ? "PASS " : "FAIL ") << name << std::endl;
    if (!success) { throw std::runtime_error(name); }
}

void synchronize(xcb_connection_t* connection) {
    auto* reply = xcb_get_input_focus_reply(connection, xcb_get_input_focus(connection), nullptr);
    if (reply == nullptr) { throw std::runtime_error("X11 synchronization failed"); }
    std::free(reply);
}

void focus(xcb_connection_t* connection, xcb_window_t window) {
    xcb_set_input_focus(connection, XCB_INPUT_FOCUS_PARENT, window, XCB_CURRENT_TIME);
    synchronize(connection);
}

xcb_window_t window(xcb_connection_t* connection, xcb_window_t parent) {
    const auto id = xcb_generate_id(connection);
    const std::uint32_t mask = XCB_EVENT_MASK_FOCUS_CHANGE;
    xcb_create_window(connection, XCB_COPY_FROM_PARENT, id, parent,
                      0, 0, 100, 100, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
                      XCB_COPY_FROM_PARENT, XCB_CW_EVENT_MASK, &mask);
    xcb_map_window(connection, id);
    synchronize(connection);
    return id;
}

void grab(xcb_connection_t* connection, xcb_window_t popup) {
    auto* reply = xcb_grab_keyboard_reply(connection,
        xcb_grab_keyboard(connection, false, popup, XCB_CURRENT_TIME,
                          XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC), nullptr);
    const bool success = reply != nullptr && reply->status == XCB_GRAB_STATUS_SUCCESS;
    std::free(reply);
    if (!success) { throw std::runtime_error("X11 keyboard grab failed"); }
}
}

int w2_native_focus_regressions() {
    auto* connection = xcb_connect(nullptr, nullptr);
    if (connection == nullptr || xcb_connection_has_error(connection) != 0) { return 2; }
    try {
        const auto root = xcb_setup_roots_iterator(xcb_get_setup(connection)).data->root;
        const auto editor = window(connection, root);
        const auto child = window(connection, editor);
        const auto sibling = window(connection, root);
        const auto popup = window(connection, root);
        focus(connection, editor);
        PhotoNativeFocus observer;
        observer.attach(editor);
        check(!observer.poll(true), "initial focused editor has no loss");

        const std::uint32_t above = XCB_STACK_MODE_ABOVE;
        xcb_configure_window(connection, editor, XCB_CONFIG_WINDOW_STACK_MODE, &above);
        xcb_warp_pointer(connection, XCB_WINDOW_NONE, editor, 0, 0, 0, 0, 50, 50);
        focus(connection, XCB_INPUT_FOCUS_POINTER_ROOT);
        (void)observer.poll(true);
        focus(connection, editor);
        check(!observer.poll(true), "PointerRoot to explicit editor focus preserves first click");

        focus(connection, child);
        check(!observer.poll(true), "focus to native descendant is preserved");
        focus(connection, editor);
        check(!observer.poll(true), "focus from descendant back to editor is preserved");
        focus(connection, child);
        check(!observer.poll(true), "second native descendant focus is preserved");
        focus(connection, sibling);
        check(observer.poll(true), "focus from descendant to sibling cancels");
        check(!observer.poll(true), "loss is consumed once");

        focus(connection, editor);
        check(!observer.poll(true), "focus gain alone does not cancel");
        focus(connection, sibling);
        focus(connection, editor);
        check(observer.poll(true), "out and back before polling retains loss");

        grab(connection, popup);
        check(!observer.poll(true), "popup keyboard grab alone does not cancel");
        xcb_ungrab_keyboard(connection, XCB_CURRENT_TIME);
        synchronize(connection);
        check(!observer.poll(true), "popup keyboard ungrab alone does not cancel");

        grab(connection, popup);
        check(!observer.poll(true), "second popup grab alone does not cancel");
        focus(connection, sibling);
        check(observer.poll(true), "actual focus loss while keyboard grabbed cancels");
        xcb_ungrab_keyboard(connection, XCB_CURRENT_TIME);
        synchronize(connection);
        (void)observer.poll(true);

        PhotoNativeFocus second;
        second.attach(sibling);
        focus(connection, editor);
        (void)observer.poll(true);
        (void)second.poll(true);
        focus(connection, sibling);
        check(observer.poll(true) && !second.poll(true), "two observers preserve independent focus ownership");
        focus(connection, editor);
        check(!observer.poll(true) && second.poll(true), "opposite editor transition cancels only losing observer");

        while (auto* event = xcb_poll_for_event(connection)) { std::free(event); }
        focus(connection, sibling);
        check(observer.poll(true), "independent observer receives raw loss");
        bool original_received_loss = false;
        while (auto* event = xcb_poll_for_event(connection)) {
            if ((event->response_type & 0x7fU) == XCB_FOCUS_OUT &&
                reinterpret_cast<xcb_focus_out_event_t*>(event)->event == editor) {
                original_received_loss = true;
            }
            std::free(event);
        }
        check(original_received_loss, "original client event queue and subscription are preserved");

        focus(connection, editor);
        (void)observer.poll(true);
        for (unsigned index = 0; index < 70; ++index) {
            xcb_set_input_focus(connection, XCB_INPUT_FOCUS_PARENT,
                                index % 2 ? editor : sibling, XCB_CURRENT_TIME);
        }
        synchronize(connection);
        check(observer.poll(true), "bounded focus-event burst cancels conservatively");

        observer.reset();
        check(!observer.poll(true), "removed observer has no live connection");
        observer.attach(xcb_generate_id(connection));
        check(!observer.poll(true), "invalid native window degrades without termination");
        observer.reset();
        observer.attach(editor);
        (void)observer.poll(true);
        focus(connection, sibling);
        check(observer.poll(true), "reattachment restores native monitoring");
        observer.reset();
        second.reset();
        xcb_disconnect(connection);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        xcb_disconnect(connection);
        return 1;
    }
}


namespace {
void prepare_cold_focus(xcb_connection_t* connection, PhotoNativeFocus& observer,
                       xcb_window_t top, xcb_window_t container, xcb_window_t editor) {
    observer.reset();
    const std::uint32_t above = XCB_STACK_MODE_ABOVE;
    xcb_configure_window(connection, top, XCB_CONFIG_WINDOW_STACK_MODE, &above);
    xcb_configure_window(connection, container, XCB_CONFIG_WINDOW_STACK_MODE, &above);
    xcb_warp_pointer(connection, XCB_WINDOW_NONE, editor, 0, 0, 0, 0, 50, 50);
    focus(connection, XCB_INPUT_FOCUS_POINTER_ROOT);
    observer.attach(editor);
    check(!observer.poll(true), "cold embedded attachment has no loss");
}

void complete_cold_focus(xcb_connection_t* connection, PhotoNativeFocus& observer,
                        xcb_window_t editor, xcb_window_t proxy) {
    focus(connection, editor);
    focus(connection, proxy);
    check(!observer.poll(true), "cold explicit editor to exact host proxy preserves first gesture");
}
}

int main() {
    const int previous = w2_native_focus_regressions();
    if (previous != 0) { return previous; }
    auto* connection = xcb_connect(nullptr, nullptr);
    if (connection == nullptr || xcb_connection_has_error(connection) != 0) { return 2; }
    try {
        const auto root = xcb_setup_roots_iterator(xcb_get_setup(connection)).data->root;
        const auto top = window(connection, root);
        const auto container = window(connection, top);
        const auto editor = window(connection, container);
        const auto main_window = window(connection, root);
        const auto other_host_child = window(connection, top);
        PhotoNativeFocus observer;

        // REAPER creates its embedding container below root, then moves it
        // into the floating host after the native observer has attached.
        const auto moving_container = window(connection, root);
        const auto moving_editor = window(connection, moving_container);
        PhotoNativeFocus moving_observer;
        focus(connection, main_window);
        moving_observer.attach(moving_editor);
        (void)moving_observer.poll(true);
        xcb_reparent_window(connection, moving_container, top, 0, 0);
        synchronize(connection);
        check(moving_observer.poll(true), "initial embedding reparent cancels before topology refresh");
        moving_observer.attach(moving_editor);
        const std::uint32_t moving_above = XCB_STACK_MODE_ABOVE;
        xcb_configure_window(connection, top, XCB_CONFIG_WINDOW_STACK_MODE, &moving_above);
        xcb_configure_window(connection, moving_container, XCB_CONFIG_WINDOW_STACK_MODE, &moving_above);
        xcb_warp_pointer(connection, XCB_WINDOW_NONE, moving_editor, 0, 0, 0, 0, 50, 50);
        focus(connection, XCB_INPUT_FOCUS_POINTER_ROOT);
        (void)moving_observer.poll(true);
        focus(connection, moving_editor);
        focus(connection, top);
        check(!moving_observer.poll(true), "same native window refreshes reparented host before first cold gesture");
        xcb_destroy_window(connection, moving_container);
        synchronize(connection);
        check(moving_observer.poll(true), "destroyed embedded path cancels its active proxy");
        moving_observer.attach(moving_editor);
        moving_observer.attach(moving_editor);
        check(!moving_observer.poll(true), "destroyed native handle does not repeat loss on attachment calls");
        moving_observer.reset();

        // Attachment may precede dismissal of a host startup dialog. The
        // first editor acquisition must be recognized from its own events.
        focus(connection, main_window);
        observer.attach(editor);
        check(!observer.poll(true), "attachment while startup host window is focused has no loss");
        const std::uint32_t above = XCB_STACK_MODE_ABOVE;
        xcb_configure_window(connection, top, XCB_CONFIG_WINDOW_STACK_MODE, &above);
        xcb_configure_window(connection, container, XCB_CONFIG_WINDOW_STACK_MODE, &above);
        xcb_warp_pointer(connection, XCB_WINDOW_NONE, editor, 0, 0, 0, 0, 50, 50);
        focus(connection, XCB_INPUT_FOCUS_POINTER_ROOT);
        check(!observer.poll(true), "PointerRoot after startup host focus has no editor loss");
        focus(connection, editor);
        focus(connection, top);
        check(!observer.poll(true), "first cold gesture survives host focus established after attachment");

        prepare_cold_focus(connection, observer, top, container, editor);
        complete_cold_focus(connection, observer, editor, top);
        check(!observer.poll(true), "accepted cold handoff is consumed once");
        focus(connection, main_window);
        check(observer.poll(true), "host proxy to external main cancels without child explicit focus");
        check(!observer.poll(true), "external proxy loss is consumed once");

        prepare_cold_focus(connection, observer, top, container, editor);
        complete_cold_focus(connection, observer, editor, top);
        focus(connection, main_window);
        focus(connection, top);
        check(observer.poll(true), "host proxy out and back before polling retains loss");

        prepare_cold_focus(connection, observer, top, container, editor);
        complete_cold_focus(connection, observer, editor, top);
        focus(connection, editor);
        check(!observer.poll(true), "accepted host proxy can return to its editor");
        focus(connection, top);
        check(observer.poll(true), "ordinary editor to top ancestor departure cancels after cold handoff");

        focus(connection, editor);
        (void)observer.poll(true);
        focus(connection, container);
        check(observer.poll(true), "ordinary editor to immediate parent departure cancels");
        focus(connection, editor);
        (void)observer.poll(true);
        focus(connection, top);
        focus(connection, editor);
        check(observer.poll(true), "ordinary ancestor departure and return retains loss");
        focus(connection, root);
        focus(connection, editor);
        check(observer.poll(true), "root ancestor departure and return retains loss");

        prepare_cold_focus(connection, observer, top, container, editor);
        focus(connection, editor);
        check(!observer.poll(true), "synchronized cold editor acquisition has no loss");
        focus(connection, top);
        check(observer.poll(true), "later ordinary ancestor departure is not a cold negotiation");

        prepare_cold_focus(connection, observer, top, container, editor);
        focus(connection, editor);
        focus(connection, root);
        focus(connection, top);
        check(observer.poll(true), "cold departure past hosting top then return is never waived");

        prepare_cold_focus(connection, observer, top, container, editor);
        focus(connection, editor);
        focus(connection, main_window);
        focus(connection, editor);
        focus(connection, top);
        check(observer.poll(true), "cold external out and back before host handoff retains loss");

        prepare_cold_focus(connection, observer, top, container, editor);
        complete_cold_focus(connection, observer, editor, top);
        focus(connection, other_host_child);
        check(observer.poll(true), "proxy transfer to another host child cancels");

        prepare_cold_focus(connection, observer, top, container, editor);
        complete_cold_focus(connection, observer, editor, container);
        focus(connection, main_window);
        focus(connection, container);
        check(observer.poll(true), "immediate-parent proxy out and back retains loss");

        prepare_cold_focus(connection, observer, top, container, editor);
        focus(connection, editor);
        check(!observer.poll(false), "asynchronous cold acquisition poll has no loss");
        focus(connection, top);
        check(!observer.poll(true), "cold handoff can finish after an asynchronous poll");

        const auto descendant = window(connection, editor);
        focus(connection, descendant);
        check(!observer.poll(true), "host proxy may return to an editor descendant");
        focus(connection, main_window);
        focus(connection, descendant);
        check(observer.poll(true), "editor descendant external out and back remains loss");

        prepare_cold_focus(connection, observer, top, container, editor);
        complete_cold_focus(connection, observer, editor, top);
        xcb_reparent_window(connection, container, main_window, 0, 0);
        synchronize(connection);
        check(observer.poll(true), "reparenting cached embedding path cancels and invalidates proxy");
        observer.reset();
        xcb_disconnect(connection);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        xcb_disconnect(connection);
        return 1;
    }
}
