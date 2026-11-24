# Finish the pinned X11 Cairo device before disconnecting its XCB connection.
# Apply after rhodes_patch_vstgui_x11_keyboard(); the verified external SDK and
# both generated copies' upstream license headers remain unchanged.
# Source, newline and license provenance:
# docs/agents/reports/photo-continuation-20261124/w3-050.md.
function(rhodes_patch_vstgui_x11_lifetime)
    get_target_property(vstgui_dir vstgui SOURCE_DIR)
    set(platform_original "${vstgui_dir}/platform/linux/x11platform.cpp")
    set(frame_original "${vstgui_dir}/platform/linux/x11frame.cpp")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${platform_original}" "${frame_original}")
    file(READ "${platform_original}" platform_original_contents)
    file(READ "${frame_original}" frame_contents)
    string(REPLACE "\r\n" "\n" platform_original_contents "${platform_original_contents}")
    string(REPLACE "\r\n" "\n" frame_contents "${frame_contents}")
    string(SHA256 platform_original_hash "${platform_original_contents}")
    string(SHA256 frame_original_hash "${frame_contents}")
    if(NOT platform_original_hash STREQUAL "7158057162bc716aeb52efc5e6744b9432ffb8b86e9e0a68895d9d340cc87bb3"
            OR NOT frame_original_hash STREQUAL "8dc07b3212e9710d23ca5c960ace39900705aa8cb3c09f760ad78ea160200dd5")
        message(FATAL_ERROR "Unexpected pinned VSTGUI X11 lifetime source; review local adaptation")
    endif()

    set(overlay_dir "${CMAKE_CURRENT_BINARY_DIR}/rhodes-vstgui-overlay")
    set(keyboard_overlay "${overlay_dir}/x11platform.cpp")
    set(platform_overlay "${overlay_dir}/x11platform-lifetime.cpp")
    set(frame_overlay "${overlay_dir}/x11frame-lifetime.cpp")
    get_target_property(sources vstgui SOURCES)
    set(platform_replacements 0)
    set(frame_replacements 0)
    set(patched_sources)
    foreach(source IN LISTS sources)
        if(source STREQUAL "${keyboard_overlay}")
            list(APPEND patched_sources "${platform_overlay}")
            math(EXPR platform_replacements "${platform_replacements} + 1")
        elseif(source STREQUAL "platform/linux/x11frame.cpp" OR source STREQUAL "${frame_original}")
            list(APPEND patched_sources "${frame_overlay}")
            math(EXPR frame_replacements "${frame_replacements} + 1")
        elseif(source STREQUAL "platform/linux/x11platform.cpp" OR source STREQUAL "${platform_original}")
            message(FATAL_ERROR "Apply the VSTGUI X11 keyboard adaptation before the lifetime adaptation")
        else()
            list(APPEND patched_sources "${source}")
        endif()
    endforeach()
    if(NOT platform_replacements EQUAL 1 OR NOT frame_replacements EQUAL 1)
        message(FATAL_ERROR "Expected exactly one keyboard-adapted X11 platform source and one pinned X11 frame source")
    endif()
    file(READ "${keyboard_overlay}" platform_contents)
    string(SHA256 keyboard_hash "${platform_contents}")
    if(NOT keyboard_hash STREQUAL "c186a90e423b3895347a676e07c6059accb3b9ab0c187a795d318a1f6af361be")
        message(FATAL_ERROR "Unexpected VSTGUI X11 keyboard overlay; review lifetime adaptation ordering")
    endif()

    set(platform_member_before "\txcb_connection_t* xcbConnection {nullptr};")
    set(platform_member_after "${platform_member_before}\n\tcairo_device_t* device {nullptr};")
    set(platform_exit_before [=[
	void exit ()
	{
		if (--useCount != 0)
			return;

		if (xcbConnection)
		{
			if (xkbUnprocessedState)
				xkb_state_unref (xkbUnprocessedState);
			if (xkbState)
				xkb_state_unref (xkbState);
			if (xkbKeymap)
				xkb_keymap_unref (xkbKeymap);
			if (xkbContext)
				xkb_context_unref (xkbContext);
			if (cursorContext)
			{
				for (auto c : cursors)
				{
					if (c != XCB_CURSOR_NONE)
						xcb_free_cursor (xcbConnection, c);
				}
				xcb_cursor_context_free (cursorContext);
			}

			xcb_disconnect (xcbConnection);
		}
		RunLoop::get ()->unregisterEventHandler (this);
	}
]=])
    set(platform_exit_after [=[
	void exit ()
	{
		if (--useCount != 0)
			return;

		if (device)
		{
			auto retiringDevice = device;
			device = nullptr;
			cairo_device_finish (retiringDevice);
			cairo_device_destroy (retiringDevice);
		}

		if (xcbConnection)
		{
			if (xkbUnprocessedState)
				xkb_state_unref (xkbUnprocessedState);
			xkbUnprocessedState = nullptr;
			if (xkbState)
				xkb_state_unref (xkbState);
			xkbState = nullptr;
			if (xkbKeymap)
				xkb_keymap_unref (xkbKeymap);
			xkbKeymap = nullptr;
			if (xkbContext)
				xkb_context_unref (xkbContext);
			xkbContext = nullptr;
			if (cursorContext)
			{
				for (auto c : cursors)
				{
					if (c != XCB_CURSOR_NONE)
						xcb_free_cursor (xcbConnection, c);
				}
				xcb_cursor_context_free (cursorContext);
				cursorContext = nullptr;
			}
			cursors.fill (XCB_CURSOR_NONE);

			xcb_disconnect (xcbConnection);
			xcbConnection = nullptr;
		}
		RunLoop::get ()->unregisterEventHandler (this);
	}
]=])
    set(platform_setter_before "RunLoop::~RunLoop () noexcept = default;")
    set(platform_setter_after [=[
RunLoop::~RunLoop () noexcept = default;

//------------------------------------------------------------------------
void RunLoop::setDevice (cairo_device_t* device)
{
	if (!device || impl->device == device)
		return;

	auto previousDevice = impl->device;
	impl->device = cairo_device_reference (device);
	if (previousDevice)
		cairo_device_destroy (previousDevice);
}]=])
    set(frame_surface_before "\t\twindowSurface.assign (s);\n\t\tdevice =")
    set(frame_surface_after "\t\twindowSurface.assign (s);\n\t\tRunLoop::instance ().setDevice (cairo_surface_get_device (s));\n\t\tdevice =")

    # Check each exact anchor independently before replacing it. Final hashes
    # also bind the complete output, including the pre-existing keyboard fix.
    foreach(change IN ITEMS platform_member platform_exit platform_setter frame_surface)
        if(change STREQUAL "frame_surface")
            set(contents_name frame_contents)
        else()
            set(contents_name platform_contents)
        endif()
        set(before "${${change}_before}")
        set(after "${${change}_after}")
        string(REPLACE "${before}" "" without_anchor "${${contents_name}}")
        string(LENGTH "${${contents_name}}" contents_length)
        string(LENGTH "${without_anchor}" without_anchor_length)
        string(LENGTH "${before}" anchor_length)
        math(EXPR matched_length "${contents_length} - ${without_anchor_length}")
        if(NOT matched_length EQUAL anchor_length)
            message(FATAL_ERROR "Expected exactly one VSTGUI X11 lifetime ${change} anchor")
        endif()
        string(REPLACE "${before}" "${after}" ${contents_name} "${${contents_name}}")
    endforeach()
    string(SHA256 platform_patched_hash "${platform_contents}")
    string(SHA256 frame_patched_hash "${frame_contents}")
    if(NOT platform_patched_hash STREQUAL "e0c889e4efa8bf0b2f76feb5592084b64987ba2ae012de3a7afd889b131b87f2"
            OR NOT frame_patched_hash STREQUAL "66fa490278825250966aad9f4a054942f3105ab131ff0cf45ba861c1b42dfd7c")
        message(FATAL_ERROR "VSTGUI X11 lifetime adaptation mismatch")
    endif()

    file(MAKE_DIRECTORY "${overlay_dir}")
    foreach(unit IN ITEMS platform frame)
        unset(existing_hash)
        if(EXISTS "${${unit}_overlay}")
            file(SHA256 "${${unit}_overlay}" existing_hash)
        endif()
        if(NOT existing_hash STREQUAL "${${unit}_patched_hash}")
            file(WRITE "${${unit}_overlay}" "${${unit}_contents}")
        endif()
    endforeach()
    set_property(TARGET vstgui PROPERTY SOURCES "${patched_sources}")
    set_source_files_properties("${platform_overlay}" "${frame_overlay}" TARGET_DIRECTORY vstgui
        PROPERTIES INCLUDE_DIRECTORIES "${vstgui_dir}/platform/linux")
endfunction()
