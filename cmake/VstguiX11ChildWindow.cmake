# Destroy the pinned backend's owned child while a shared XCB connection lives.
# Adapt only the build copy; external SDK source and license files stay unchanged.
# Source, ownership and license provenance:
# docs/agents/reports/photo-continuation-20261124/w3-055.md.
function(rhodes_patch_vstgui_x11_child_window)
    get_target_property(vstgui_dir vstgui SOURCE_DIR)
    set(original "${vstgui_dir}/platform/linux/x11utils.cpp")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${original}")
    file(READ "${original}" contents)
    string(REPLACE "\r\n" "\n" contents "${contents}")
    string(SHA256 original_hash "${contents}")
    if(NOT original_hash STREQUAL "67c30ca1e6afa5905a5205a3b2d0dc82ca640728c5c77b3e0ae42a0b46a19bac")
        message(FATAL_ERROR "Unexpected pinned VSTGUI X11 child-window source; review local adaptation")
    endif()

    set(include_before "#include \"x11utils.h\"\n")
    set(include_after "${include_before}#include <cstdlib>\n")
    set(destructor_before "ChildWindow::~ChildWindow () noexcept {}")
    set(destructor_after [=[
ChildWindow::~ChildWindow () noexcept
{
	if (auto connection = RunLoop::instance ().getXcbConnection ())
	{
		auto cookie = xcb_destroy_window_checked (connection, id);
		// The host may already have destroyed the parent and therefore this child.
		// Checking consumes that BadWindow and completes cleanup without event pumping.
		std::free (xcb_request_check (connection, cookie));
	}
}]=])
    foreach(change IN ITEMS include destructor)
        set(before "${${change}_before}")
        set(after "${${change}_after}")
        string(REPLACE "${before}" "" without_anchor "${contents}")
        string(LENGTH "${contents}" contents_length)
        string(LENGTH "${without_anchor}" without_anchor_length)
        string(LENGTH "${before}" anchor_length)
        math(EXPR matched_length "${contents_length} - ${without_anchor_length}")
        if(NOT matched_length EQUAL anchor_length)
            message(FATAL_ERROR "Expected exactly one VSTGUI X11 child-window ${change} anchor")
        endif()
        string(REPLACE "${before}" "${after}" contents "${contents}")
    endforeach()
    string(SHA256 patched_hash "${contents}")
    if(NOT patched_hash STREQUAL "47554d4c4e7d330e3791a22f7920e7cfc214a84bff88b5493b4906e36b2a2eae")
        message(FATAL_ERROR "VSTGUI X11 child-window adaptation mismatch")
    endif()

    set(overlay_dir "${CMAKE_CURRENT_BINARY_DIR}/rhodes-vstgui-overlay")
    set(overlay "${overlay_dir}/x11utils.cpp")
    get_target_property(sources vstgui SOURCES)
    set(replacements 0)
    set(patched_sources)
    foreach(source IN LISTS sources)
        if(source STREQUAL "platform/linux/x11utils.cpp" OR source STREQUAL "${original}")
            list(APPEND patched_sources "${overlay}")
            math(EXPR replacements "${replacements} + 1")
        else()
            list(APPEND patched_sources "${source}")
        endif()
    endforeach()
    if(NOT replacements EQUAL 1)
        message(FATAL_ERROR "Expected exactly one pinned VSTGUI X11 child-window source")
    endif()
    file(MAKE_DIRECTORY "${overlay_dir}")
    if(EXISTS "${overlay}")
        file(SHA256 "${overlay}" existing_hash)
    endif()
    if(NOT existing_hash STREQUAL patched_hash)
        file(WRITE "${overlay}" "${contents}")
    endif()
    set_property(TARGET vstgui PROPERTY SOURCES "${patched_sources}")
    set_source_files_properties("${overlay}" TARGET_DIRECTORY vstgui
        PROPERTIES INCLUDE_DIRECTORIES "${vstgui_dir}/platform/linux")
endfunction()
