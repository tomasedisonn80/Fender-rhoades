# Optional, explicitly supplied SDK. The default core build never downloads it.
if(CMAKE_VERSION VERSION_LESS 3.25)
    message(FATAL_ERROR "The optional VST3 SDK requires CMake 3.25 or newer")
endif()
if(NOT EXISTS "${RHODES_VST3_SDK_ROOT}/pluginterfaces/vst/ivstaudioprocessor.h"
        OR NOT EXISTS "${RHODES_VST3_SDK_ROOT}/public.sdk/source/vst/vstaudioeffect.cpp"
        OR NOT EXISTS "${RHODES_VST3_SDK_ROOT}/cmake/modules/SMTG_VST3_SDK.cmake")
    message(FATAL_ERROR "Set RHODES_VST3_SDK_ROOT to the complete pinned SDK checkout; see docs/DEPENDENCIES.md")
endif()
find_package(Git REQUIRED)
function(rhodes_verify_sdk_checkout component expected_revision)
    set(checkout "${RHODES_VST3_SDK_ROOT}/${component}")
    # A missing submodule must not inherit the enclosing SDK's Git repository.
    if(NOT EXISTS "${checkout}/.git")
        message(FATAL_ERROR "VST3 SDK ${component}: missing Git checkout. Initialize the documented submodules for the selected editor option; see docs/VST3.md")
    endif()
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${checkout}" rev-parse --verify HEAD
        OUTPUT_VARIABLE actual_revision OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE revision_result ERROR_QUIET)
    if(NOT revision_result EQUAL 0 OR NOT actual_revision STREQUAL expected_revision)
        message(FATAL_ERROR "VST3 SDK ${component}: expected ${expected_revision}, found '${actual_revision}'. Use the documented pinned checkout; see docs/DEPENDENCIES.md")
    endif()
    # Check built tracked contents, including staged edits, against the pinned
    # commit. Ignore gitlinks here: each required submodule is checked separately.
    # Untracked build directories and unused optional submodules are permitted.
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${checkout}"
        diff --quiet --ignore-submodules=all HEAD --
        RESULT_VARIABLE contents_result ERROR_QUIET)
    if(NOT contents_result EQUAL 0)
        message(FATAL_ERROR "VST3 SDK ${component}: tracked contents differ from the pinned commit or cannot be checked. Use a clean separate checkout; local files are never modified by this build.")
    endif()
endfunction()

rhodes_verify_sdk_checkout("." "3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96")
rhodes_verify_sdk_checkout("base" "fcf9da0bd27a16f7f03773a3a39822f28f5c8477")
rhodes_verify_sdk_checkout("cmake" "054c9143cbb8d47fc4694e473f2ee3b4d951a8f5")
rhodes_verify_sdk_checkout("pluginterfaces" "4f547e8e102b47de4a8b8aaf343c73b700786372")
rhodes_verify_sdk_checkout("public.sdk" "586dc5e6c8012c3e4b01c79389375cbe96bdb1da")

if(RHODES_BUILD_VST3_EDITOR)
    rhodes_verify_sdk_checkout("vstgui4" "5db272256172557818b6158cf0bb2c4410bddb25")
endif()
set(SMTG_ENABLE_VSTGUI_SUPPORT ${RHODES_BUILD_VST3_EDITOR} CACHE BOOL "Native photo editor" FORCE)
set(SMTG_ENABLE_WAYLAND_SUPPORT OFF CACHE BOOL "Use the pinned native X11 editor backend" FORCE)
set(VSTGUI_UISCRIPTING OFF CACHE BOOL "No editor scripting engine" FORCE)
set(VSTGUI_STANDALONE OFF CACHE BOOL "No unrelated standalone framework" FORCE)
set(VSTGUI_STANDALONE_EXAMPLES OFF CACHE BOOL "No standalone examples" FORCE)
set(VSTGUI_BUILD_EXAMPLES OFF CACHE BOOL "No VSTGUI examples" FORCE)
set(VSTGUI_BUILD_TESTS OFF CACHE BOOL "Project supplies editor verification" FORCE)
set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "No SDK plugin examples" FORCE)
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES OFF CACHE BOOL "No SDK audio/editor hosts" FORCE)
set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "Do not install into a user plugin directory" FORCE)
set(SMTG_RUN_VST_VALIDATOR OFF CACHE BOOL "Run the validator explicitly through CTest" FORCE)
set(SMTG_CREATE_MODULE_INFO ON CACHE BOOL "Create VST3 module metadata" FORCE)
# SDK helper functions are invoked in this directory after add_subdirectory;
# their platform-entry source paths must remain visible in the caller's scope.
set(SDK_ROOT "${RHODES_VST3_SDK_ROOT}")
set(public_sdk_SOURCE_DIR "${RHODES_VST3_SDK_ROOT}/public.sdk")
set(pluginterfaces_SOURCE_DIR "${RHODES_VST3_SDK_ROOT}/pluginterfaces")
add_subdirectory("${RHODES_VST3_SDK_ROOT}" "${CMAKE_BINARY_DIR}/vst3-sdk" EXCLUDE_FROM_ALL)
if(RHODES_BUILD_VST3_EDITOR AND WIN32)
    include("${PROJECT_SOURCE_DIR}/cmake/VstguiWin32Timer.cmake")
    rhodes_patch_vstgui_win32_timer()
endif()
if(RHODES_BUILD_VST3_EDITOR AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    include("${PROJECT_SOURCE_DIR}/cmake/VstguiX11Keyboard.cmake")
    rhodes_patch_vstgui_x11_keyboard()
    include("${PROJECT_SOURCE_DIR}/cmake/VstguiX11Lifetime.cmake")
    rhodes_patch_vstgui_x11_lifetime()
    include("${PROJECT_SOURCE_DIR}/cmake/VstguiX11ChildWindow.cmake")
    rhodes_patch_vstgui_x11_child_window()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(RHODES_EDITOR_XCB REQUIRED IMPORTED_TARGET xcb)
endif()

# The pinned SDK's base archive uses CoreFoundation in fstring.cpp but does not
# propagate that dependency. Plug-in helpers add platform frameworks themselves;
# our plain benchmark/test executables also need the base archive's dependency.
# Attach it once to the static archive's interface without editing the SDK.
if(APPLE)
    find_library(RHODES_COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
    target_link_libraries(base INTERFACE "${RHODES_COREFOUNDATION_FRAMEWORK}")
endif()

# The optional fifth argument selects the quadrature variant.
function(rhodes_add_plugin target package filtered bundle_id)
    if(ARGC GREATER 4 AND ARGV4)
        # Local SDK metadata scope; existing bundles keep their versions.
        set(PROJECT_VERSION "0.3.0")
        set(PROJECT_VERSION_MAJOR 0)
        set(PROJECT_VERSION_MINOR 3)
        set(PROJECT_VERSION_PATCH 0)
    elseif(filtered)
        # Local SDK metadata scope; the original bundle keeps its version.
        set(PROJECT_VERSION "0.2.0")
        set(PROJECT_VERSION_MAJOR 0)
        set(PROJECT_VERSION_MINOR 2)
        set(PROJECT_VERSION_PATCH 0)
    endif()
    smtg_add_vst3plugin(${target} PACKAGE_NAME "${package}" SOURCES_LIST
        "${PROJECT_SOURCE_DIR}/plugins/vst3/processor.cpp"
        "${PROJECT_SOURCE_DIR}/plugins/vst3/controller.cpp"
        "${PROJECT_SOURCE_DIR}/plugins/vst3/state_stream.cpp"
        "${PROJECT_SOURCE_DIR}/plugins/vst3/factory.cpp")
    target_compile_features(${target} PRIVATE cxx_std_20)
    # All variant-sensitive TUs belong to this target. The common core and SDK
    # never inherit this definition; only platform/factory entry points export.
    if(ARGC GREATER 4 AND ARGV4)
        target_compile_definitions(${target} PRIVATE RHODES_QUADRATURE_VST3=1)
    elseif(filtered)
        target_compile_definitions(${target} PRIVATE RHODES_FILTERED_VST3=1)
    endif()
    set_target_properties(${target} PROPERTIES
        CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
    target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}")
    target_include_directories(${target} SYSTEM PRIVATE "${RHODES_VST3_SDK_ROOT}")
    target_link_libraries(${target} PRIVATE rhodes_core sdk)
    if(RHODES_BUILD_VST3_EDITOR)
        target_sources(${target} PRIVATE "${PROJECT_SOURCE_DIR}/plugins/vst3/photo_editor.cpp"
            "${PROJECT_SOURCE_DIR}/plugins/vst3/photo_native_focus.cpp")
        target_compile_definitions(${target} PRIVATE RHODES_HAS_PHOTO_EDITOR=1)
        target_include_directories(${target} SYSTEM PRIVATE "${RHODES_VST3_SDK_ROOT}/vstgui4")
        target_link_libraries(${target} PRIVATE vstgui_support)
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            target_link_libraries(${target} PRIVATE PkgConfig::RHODES_EDITOR_XCB)
        endif()
    endif()
    rhodes_target_options(${target})
    if(SMTG_MAC)
        smtg_target_set_bundle(${target} BUNDLE_IDENTIFIER "${bundle_id}"
            COMPANY_NAME "Independent Rhodes Research")
    endif()
    # The pinned SDK associates copy commands with MAIN_DEPENDENCY source
    # paths. Reusing a source across bundles in a directory collides, so each
    # target gets distinct configure-tracked inputs with the original basenames.
    set(resource_inputs "${CMAKE_CURRENT_BINARY_DIR}/rhodes-resources/${target}")
    file(MAKE_DIRECTORY "${resource_inputs}")
    configure_file("${RHODES_VST3_SDK_ROOT}/LICENSE.txt"
        "${resource_inputs}/LICENSE.txt" COPYONLY)
    configure_file("${PROJECT_SOURCE_DIR}/plugins/vst3/THIRD_PARTY_NOTICES.md"
        "${resource_inputs}/THIRD_PARTY_NOTICES.md" COPYONLY)
    smtg_target_add_plugin_resources(${target} RESOURCES
        "${resource_inputs}/LICENSE.txt" "${resource_inputs}/THIRD_PARTY_NOTICES.md")
    if(RHODES_BUILD_VST3_EDITOR)
        foreach(editor_resource IN ITEMS rhodes-suitcase.png rhodes-suitcase.uidesc Expat-LICENSE.txt)
            configure_file("${PROJECT_SOURCE_DIR}/plugins/vst3/resources/${editor_resource}"
                "${resource_inputs}/${editor_resource}" COPYONLY)
            smtg_target_add_plugin_resources(${target} RESOURCES "${resource_inputs}/${editor_resource}")
        endforeach()
        configure_file("${RHODES_VST3_SDK_ROOT}/vstgui4/LICENSE"
            "${resource_inputs}/VSTGUI-LICENSE.txt" COPYONLY)
        configure_file("${RHODES_VST3_SDK_ROOT}/vstgui4/vstgui/thirdparty/rapidjson/license.txt"
            "${resource_inputs}/RapidJSON-LICENSE.txt" COPYONLY)
        configure_file("${RHODES_VST3_SDK_ROOT}/vstgui4/vstgui/thirdparty/miniz/LICENSE"
            "${resource_inputs}/miniz-LICENSE.txt" COPYONLY)
        smtg_target_add_plugin_resources(${target} RESOURCES "${resource_inputs}/VSTGUI-LICENSE.txt"
            "${resource_inputs}/RapidJSON-LICENSE.txt" "${resource_inputs}/miniz-LICENSE.txt")
    else()
        # Reusing an editor-enabled build directory leaves copied resources
        # behind. Remove only this project's GUI resource names from the bundle.
        get_target_property(plugin_bundle ${target} SMTG_PLUGIN_PACKAGE_PATH)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E rm -f
                "${plugin_bundle}/Contents/Resources/rhodes-suitcase.png"
                "${plugin_bundle}/Contents/Resources/rhodes-suitcase.uidesc"
                "${plugin_bundle}/Contents/Resources/Expat-LICENSE.txt"
                "${plugin_bundle}/Contents/Resources/VSTGUI-LICENSE.txt"
                "${plugin_bundle}/Contents/Resources/RapidJSON-LICENSE.txt"
                "${plugin_bundle}/Contents/Resources/miniz-LICENSE.txt"
            VERBATIM)
    endif()
endfunction()
rhodes_add_plugin(rhodes_vst3 "RhodesResearch" FALSE "org.rhodesresearch.experimental")
rhodes_add_plugin(rhodes_vst3_filtered "RhodesResearchFiltered" TRUE "org.rhodesresearch.filtered")
rhodes_add_plugin(rhodes_vst3_quadrature "RhodesResearchQuadrature" FALSE "org.rhodesresearch.quadrature" TRUE)

add_executable(rhodes_vst3_benchmark
    "${PROJECT_SOURCE_DIR}/apps/vst3_benchmark.cpp"
    "${PROJECT_SOURCE_DIR}/plugins/vst3/processor.cpp"
    "${PROJECT_SOURCE_DIR}/plugins/vst3/state_stream.cpp")
target_compile_features(rhodes_vst3_benchmark PRIVATE cxx_std_20)
target_include_directories(rhodes_vst3_benchmark PRIVATE "${PROJECT_SOURCE_DIR}")
target_include_directories(rhodes_vst3_benchmark SYSTEM PRIVATE "${RHODES_VST3_SDK_ROOT}")
target_link_libraries(rhodes_vst3_benchmark PRIVATE rhodes_core sdk)
target_compile_definitions(rhodes_vst3_benchmark PRIVATE RHODES_BUILD_CONFIGURATION="$<CONFIG>")
rhodes_target_options(rhodes_vst3_benchmark)

add_executable(rhodes_vst3_filtered_benchmark
    "${PROJECT_SOURCE_DIR}/apps/vst3_benchmark.cpp"
    "${PROJECT_SOURCE_DIR}/plugins/vst3/processor.cpp"
    "${PROJECT_SOURCE_DIR}/plugins/vst3/state_stream.cpp")
target_compile_features(rhodes_vst3_filtered_benchmark PRIVATE cxx_std_20)
target_include_directories(rhodes_vst3_filtered_benchmark PRIVATE "${PROJECT_SOURCE_DIR}")
target_include_directories(rhodes_vst3_filtered_benchmark SYSTEM PRIVATE "${RHODES_VST3_SDK_ROOT}")
target_link_libraries(rhodes_vst3_filtered_benchmark PRIVATE rhodes_core sdk)
target_compile_definitions(rhodes_vst3_filtered_benchmark PRIVATE
    RHODES_FILTERED_VST3=1 RHODES_BUILD_CONFIGURATION="$<CONFIG>")
rhodes_target_options(rhodes_vst3_filtered_benchmark)

add_executable(rhodes_vst3_quadrature_benchmark
    "${PROJECT_SOURCE_DIR}/apps/vst3_benchmark.cpp"
    "${PROJECT_SOURCE_DIR}/plugins/vst3/processor.cpp"
    "${PROJECT_SOURCE_DIR}/plugins/vst3/state_stream.cpp")
target_compile_features(rhodes_vst3_quadrature_benchmark PRIVATE cxx_std_20)
target_include_directories(rhodes_vst3_quadrature_benchmark PRIVATE "${PROJECT_SOURCE_DIR}")
target_include_directories(rhodes_vst3_quadrature_benchmark SYSTEM PRIVATE "${RHODES_VST3_SDK_ROOT}")
target_link_libraries(rhodes_vst3_quadrature_benchmark PRIVATE rhodes_core sdk)
target_compile_definitions(rhodes_vst3_quadrature_benchmark PRIVATE
    RHODES_QUADRATURE_VST3=1 RHODES_BUILD_CONFIGURATION="$<CONFIG>")
rhodes_target_options(rhodes_vst3_quadrature_benchmark)

if(BUILD_TESTING)
    # One explicit build target keeps CI and documented test builds complete.
    add_custom_target(rhodes_vst3_checks)
    add_dependencies(rhodes_vst3_checks rhodes_vst3 rhodes_vst3_benchmark
        rhodes_vst3_filtered rhodes_vst3_filtered_benchmark
        rhodes_vst3_quadrature rhodes_vst3_quadrature_benchmark)
    if(RHODES_BUILD_VST3_EDITOR AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
        find_package(Python3 COMPONENTS Interpreter REQUIRED)
        find_program(RHODES_XVFB_EXECUTABLE NAMES Xvfb)
        find_program(RHODES_XAUTH_EXECUTABLE NAMES xauth)
        find_program(RHODES_XDPYINFO_EXECUTABLE NAMES xdpyinfo)
        if(NOT RHODES_XVFB_EXECUTABLE OR NOT RHODES_XAUTH_EXECUTABLE
                OR NOT RHODES_XDPYINFO_EXECUTABLE)
            message(FATAL_ERROR "Linux photo-editor tests require Xvfb, xauth and xdpyinfo. Install xvfb, xauth and x11-utils, or set RHODES_XVFB_EXECUTABLE, RHODES_XAUTH_EXECUTABLE and RHODES_XDPYINFO_EXECUTABLE. Use BUILD_TESTING=OFF for a build without tests.")
        endif()
        # This XCB fixture exercises the native helper without linking the SDK.
        add_executable(rhodes_vst3_photo_native_focus_tests
            "${PROJECT_SOURCE_DIR}/tests/cpp/photo_native_focus_test.cpp"
            "${PROJECT_SOURCE_DIR}/plugins/vst3/photo_native_focus.cpp")
        target_compile_features(rhodes_vst3_photo_native_focus_tests PRIVATE cxx_std_20)
        target_include_directories(rhodes_vst3_photo_native_focus_tests PRIVATE "${PROJECT_SOURCE_DIR}")
        target_link_libraries(rhodes_vst3_photo_native_focus_tests PRIVATE PkgConfig::RHODES_EDITOR_XCB)
        rhodes_target_options(rhodes_vst3_photo_native_focus_tests)
        add_dependencies(rhodes_vst3_checks rhodes_vst3_photo_native_focus_tests)
        add_test(NAME vst3.photo_native_focus
            COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/run_xvfb_test.py"
                --xvfb "${RHODES_XVFB_EXECUTABLE}" --xauth "${RHODES_XAUTH_EXECUTABLE}"
                --xdpyinfo "${RHODES_XDPYINFO_EXECUTABLE}"
                --startup-timeout 10 --test-timeout 30 --
                $<TARGET_FILE:rhodes_vst3_photo_native_focus_tests>)
        set_tests_properties(vst3.photo_native_focus PROPERTIES TIMEOUT 60 LABELS "editor;linux-xcb")
        # This test includes the production editor TU to reach its private view;
        # compiling photo_editor.cpp separately would duplicate its definitions.
        # Keep upstream module support outside project-only warning policies.
        add_library(rhodes_photo_test_module_support OBJECT
            "${RHODES_VST3_SDK_ROOT}/public.sdk/source/main/moduleinit.cpp"
            "${RHODES_VST3_SDK_ROOT}/public.sdk/source/main/pluginfactory.cpp"
            "${RHODES_VST3_SDK_ROOT}/public.sdk/source/main/linuxmain.cpp")
        target_compile_features(rhodes_photo_test_module_support PRIVATE cxx_std_20)
        target_link_libraries(rhodes_photo_test_module_support PRIVATE sdk)
        add_executable(rhodes_vst3_photo_view_tests
            "${PROJECT_SOURCE_DIR}/tests/cpp/photo_view_interaction_test.cpp"
            "${PROJECT_SOURCE_DIR}/plugins/vst3/controller.cpp"
            "${PROJECT_SOURCE_DIR}/plugins/vst3/state_stream.cpp"
            "${PROJECT_SOURCE_DIR}/plugins/vst3/photo_native_focus.cpp"
            "${PROJECT_SOURCE_DIR}/plugins/vst3/processor.cpp"
            "${PROJECT_SOURCE_DIR}/plugins/vst3/factory.cpp")
        target_compile_features(rhodes_vst3_photo_view_tests PRIVATE cxx_std_20)
        target_compile_definitions(rhodes_vst3_photo_view_tests PRIVATE RHODES_HAS_PHOTO_EDITOR=1)
        target_include_directories(rhodes_vst3_photo_view_tests PRIVATE "${PROJECT_SOURCE_DIR}")
        target_include_directories(rhodes_vst3_photo_view_tests SYSTEM PRIVATE
            "${RHODES_VST3_SDK_ROOT}" "${RHODES_VST3_SDK_ROOT}/vstgui4")
        target_link_libraries(rhodes_vst3_photo_view_tests PRIVATE
            rhodes_core sdk vstgui_support rhodes_photo_test_module_support PkgConfig::RHODES_EDITOR_XCB)
        rhodes_target_options(rhodes_vst3_photo_view_tests)
        add_dependencies(rhodes_vst3_checks rhodes_vst3_photo_view_tests)
        foreach(test_name IN ITEMS rapid_switches continuous_reset host_shortcuts fine_steps
                rejected_edits end_cancel_idempotent begin_cancel_drag begin_cancel_wheel
                begin_cancel_switch end_cancel_new_press perform_cancel_drag
                perform_cancel_host_update audition_drag audition_cancel_dispatch
                audition_cancel_allocation audition_cancel_sibling
                observer_cancel_rollback observer_cancel_host_changed observer_cancel_host_equal
                perform_reject_host_equal perform_reject_other_parameter keyboard_cancel_edit
                keyboard_cancel_tab keyboard_audition_cancel_edit
                audition_glissando_cancel_dispatch audition_glissando_cancel_allocation)
            add_test(NAME vst3.photo_view.${test_name}
                COMMAND rhodes_vst3_photo_view_tests ${test_name})
            set_tests_properties(vst3.photo_view.${test_name} PROPERTIES TIMEOUT 30 LABELS "editor;interaction")
        endforeach()
        add_executable(rhodes_vstgui_x11_keyboard_tests
            "${PROJECT_SOURCE_DIR}/tests/cpp/vstgui_x11_keyboard_test.cpp")
        target_compile_features(rhodes_vstgui_x11_keyboard_tests PRIVATE cxx_std_20)
        target_include_directories(rhodes_vstgui_x11_keyboard_tests SYSTEM PRIVATE
            "${RHODES_VST3_SDK_ROOT}/vstgui4")
        target_link_libraries(rhodes_vstgui_x11_keyboard_tests PRIVATE vstgui PkgConfig::RHODES_EDITOR_XCB)
        rhodes_target_options(rhodes_vstgui_x11_keyboard_tests)
        add_dependencies(rhodes_vst3_checks rhodes_vstgui_x11_keyboard_tests)
        add_test(NAME vst3.photo_x11_keyboard
            COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/run_xvfb_test.py"
                --xvfb "${RHODES_XVFB_EXECUTABLE}" --xauth "${RHODES_XAUTH_EXECUTABLE}"
                --xdpyinfo "${RHODES_XDPYINFO_EXECUTABLE}"
                --startup-timeout 10 --test-timeout 30 --
                $<TARGET_FILE:rhodes_vstgui_x11_keyboard_tests>)
        set_tests_properties(vst3.photo_x11_keyboard PROPERTIES TIMEOUT 60 LABELS "editor;linux-xcb")
    endif()
    # Optional third/fourth arguments select filtered/quadrature respectively;
    # the fifth omits the processor for controller-only public-interface tests.
    function(rhodes_add_vst3_test target source)
        set(test_sources
            "${PROJECT_SOURCE_DIR}/${source}"
            "${PROJECT_SOURCE_DIR}/plugins/vst3/controller.cpp"
            "${PROJECT_SOURCE_DIR}/plugins/vst3/state_stream.cpp")
        if(NOT (ARGC GREATER 4 AND ARGV4))
            list(APPEND test_sources "${PROJECT_SOURCE_DIR}/plugins/vst3/processor.cpp")
        endif()
        add_executable(${target} ${test_sources})
        target_compile_features(${target} PRIVATE cxx_std_20)
        target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}")
        target_include_directories(${target} SYSTEM PRIVATE "${RHODES_VST3_SDK_ROOT}")
        target_link_libraries(${target} PRIVATE rhodes_core sdk)
        if(ARGC GREATER 3 AND ARGV3)
            target_compile_definitions(${target} PRIVATE RHODES_QUADRATURE_VST3=1)
        elseif(ARGC GREATER 2 AND ARGV2)
            target_compile_definitions(${target} PRIVATE RHODES_FILTERED_VST3=1)
        endif()
        rhodes_target_options(${target})
        add_dependencies(rhodes_vst3_checks ${target})
    endfunction()
    rhodes_add_vst3_test(rhodes_vst3_controller_revision_tests
        tests/cpp/vst3_controller_revision_test.cpp FALSE FALSE TRUE)
    add_test(NAME vst3.controller_revision COMMAND rhodes_vst3_controller_revision_tests)
    set_tests_properties(vst3.controller_revision PROPERTIES TIMEOUT 60)
    rhodes_add_vst3_test(rhodes_vst3_audition_reentrancy_tests
        tests/cpp/vst3_audition_reentrancy_test.cpp FALSE FALSE TRUE)
    foreach(test_name IN ITEMS construction notify pending siblings coalescing)
        add_test(NAME vst3.audition_reentrancy.${test_name}
            COMMAND rhodes_vst3_audition_reentrancy_tests ${test_name})
        set_tests_properties(vst3.audition_reentrancy.${test_name} PROPERTIES TIMEOUT 60)
    endforeach()
    rhodes_add_vst3_test(rhodes_vst3_editor_audition_tests tests/cpp/vst3_editor_audition_test.cpp)
    rhodes_add_vst3_test(rhodes_vst3_audition_ownership_tests tests/cpp/vst3_audition_ownership_test.cpp FALSE FALSE TRUE)
    rhodes_add_vst3_test(rhodes_vst3_filtered_audition_ownership_tests tests/cpp/vst3_audition_ownership_test.cpp TRUE FALSE TRUE)
    rhodes_add_vst3_test(rhodes_vst3_quadrature_audition_ownership_tests tests/cpp/vst3_audition_ownership_test.cpp FALSE TRUE TRUE)
    foreach(variant IN ITEMS "" "filtered_" "quadrature_")
        add_test(NAME vst3.${variant}audition_ownership COMMAND rhodes_vst3_${variant}audition_ownership_tests)
        set_tests_properties(vst3.${variant}audition_ownership PROPERTIES TIMEOUT 60)
    endforeach()
    rhodes_add_vst3_test(rhodes_vst3_filtered_editor_audition_tests tests/cpp/vst3_editor_audition_test.cpp TRUE)
    rhodes_add_vst3_test(rhodes_vst3_quadrature_editor_audition_tests tests/cpp/vst3_editor_audition_test.cpp FALSE TRUE)
    foreach(variant IN ITEMS "" "filtered_" "quadrature_")
        add_test(NAME vst3.${variant}editor_audition COMMAND rhodes_vst3_${variant}editor_audition_tests)
        set_tests_properties(vst3.${variant}editor_audition PROPERTIES TIMEOUT 60)
    endforeach()
    rhodes_add_vst3_test(rhodes_vst3_panel_control_tests tests/cpp/vst3_panel_control_test.cpp FALSE FALSE TRUE)
    rhodes_add_vst3_test(rhodes_vst3_filtered_panel_control_tests tests/cpp/vst3_panel_control_test.cpp TRUE FALSE TRUE)
    rhodes_add_vst3_test(rhodes_vst3_quadrature_panel_control_tests tests/cpp/vst3_panel_control_test.cpp FALSE TRUE TRUE)
    foreach(variant IN ITEMS "" "filtered_" "quadrature_")
        foreach(test_name IN ITEMS metadata values text stream recall instances)
            add_test(NAME vst3.${variant}panel_${test_name}
                COMMAND rhodes_vst3_${variant}panel_control_tests ${test_name})
            set_tests_properties(vst3.${variant}panel_${test_name} PROPERTIES TIMEOUT 60)
        endforeach()
    endforeach()
    add_executable(rhodes_model_stream_tests
        "${PROJECT_SOURCE_DIR}/tests/cpp/instrument_model_state_test.cpp"
        "${PROJECT_SOURCE_DIR}/plugins/vst3/state_stream.cpp")
    target_compile_definitions(rhodes_model_stream_tests PRIVATE RHODES_TEST_STATE_STREAM=1)
    target_include_directories(rhodes_model_stream_tests PRIVATE "${PROJECT_SOURCE_DIR}")
    target_include_directories(rhodes_model_stream_tests SYSTEM PRIVATE "${RHODES_VST3_SDK_ROOT}")
    target_link_libraries(rhodes_model_stream_tests PRIVATE rhodes_core)
    rhodes_target_options(rhodes_model_stream_tests)
    add_dependencies(rhodes_vst3_checks rhodes_model_stream_tests)
    foreach(test_name IN ITEMS stream_roundtrip stream_invalid_model stream_rejection stream_transfers)
        add_test(NAME vst3.model_${test_name} COMMAND rhodes_model_stream_tests ${test_name})
    endforeach()
    add_executable(rhodes_expression_stream_tests
        "${PROJECT_SOURCE_DIR}/tests/cpp/vst3_expression_stream_test.cpp"
        "${PROJECT_SOURCE_DIR}/plugins/vst3/state_stream.cpp")
    target_include_directories(rhodes_expression_stream_tests PRIVATE "${PROJECT_SOURCE_DIR}")
    target_include_directories(rhodes_expression_stream_tests SYSTEM PRIVATE "${RHODES_VST3_SDK_ROOT}")
    target_link_libraries(rhodes_expression_stream_tests PRIVATE rhodes_core)
    rhodes_target_options(rhodes_expression_stream_tests)
    add_dependencies(rhodes_vst3_checks rhodes_expression_stream_tests)
    foreach(test_name IN ITEMS roundtrip malformed transfers overwrite)
        add_test(NAME vst3.expression_stream_${test_name} COMMAND rhodes_expression_stream_tests ${test_name})
    endforeach()
    rhodes_add_vst3_test(rhodes_vst3_expression_audio_tests tests/cpp/vst3_expression_audio_test.cpp)
    rhodes_add_vst3_test(rhodes_vst3_filtered_expression_audio_tests tests/cpp/vst3_expression_audio_test.cpp TRUE)
    rhodes_add_vst3_test(rhodes_vst3_quadrature_expression_audio_tests tests/cpp/vst3_expression_audio_test.cpp FALSE TRUE)
    foreach(test_name IN ITEMS default_queues anchor_duplicates combined_curves nondyadic_curve
            muted_resume flush_recall partition_curves rate_matrix)
        add_test(NAME vst3.expression_audio_${test_name} COMMAND rhodes_vst3_expression_audio_tests ${test_name})
        add_test(NAME vst3.filtered_expression_audio_${test_name} COMMAND rhodes_vst3_filtered_expression_audio_tests ${test_name})
        add_test(NAME vst3.quadrature_expression_audio_${test_name} COMMAND rhodes_vst3_quadrature_expression_audio_tests ${test_name})
    endforeach()
    rhodes_add_vst3_test(rhodes_vst3_mono_tests tests/cpp/vst3_mono_contract_test.cpp)
    rhodes_add_vst3_test(rhodes_vst3_filtered_mono_tests tests/cpp/vst3_mono_contract_test.cpp TRUE)
    rhodes_add_vst3_test(rhodes_vst3_quadrature_mono_tests tests/cpp/vst3_mono_contract_test.cpp FALSE TRUE)
    foreach(test_name IN ITEMS negotiation scalar pointers omission flush rejection lifecycle)
        add_test(NAME vst3.mono_${test_name} COMMAND rhodes_vst3_mono_tests ${test_name})
        add_test(NAME vst3.filtered_mono_${test_name} COMMAND rhodes_vst3_filtered_mono_tests ${test_name})
        add_test(NAME vst3.quadrature_mono_${test_name} COMMAND rhodes_vst3_quadrature_mono_tests ${test_name})
    endforeach()
    rhodes_add_vst3_test(rhodes_vst3_expression_control_tests tests/cpp/vst3_expression_control_test.cpp FALSE FALSE TRUE)
    rhodes_add_vst3_test(rhodes_vst3_filtered_expression_control_tests tests/cpp/vst3_expression_control_test.cpp TRUE FALSE TRUE)
    rhodes_add_vst3_test(rhodes_vst3_quadrature_expression_control_tests tests/cpp/vst3_expression_control_test.cpp FALSE TRUE TRUE)
    foreach(test_name IN ITEMS metadata conversion recall lifecycle fp_rounding fp_flush)
        add_test(NAME vst3.expression_control_${test_name} COMMAND rhodes_vst3_expression_control_tests ${test_name})
        add_test(NAME vst3.filtered_expression_control_${test_name} COMMAND rhodes_vst3_filtered_expression_control_tests ${test_name})
        add_test(NAME vst3.quadrature_expression_control_${test_name} COMMAND rhodes_vst3_quadrature_expression_control_tests ${test_name})
        if(test_name MATCHES "^fp_")
            set_tests_properties(vst3.expression_control_${test_name}
                vst3.filtered_expression_control_${test_name}
                vst3.quadrature_expression_control_${test_name} PROPERTIES SKIP_RETURN_CODE 77)
        endif()
    endforeach()
    rhodes_add_vst3_test(rhodes_vst3_expression_transaction_tests tests/cpp/vst3_expression_transaction_test.cpp)
    rhodes_add_vst3_test(rhodes_vst3_filtered_expression_transaction_tests tests/cpp/vst3_expression_transaction_test.cpp TRUE)
    rhodes_add_vst3_test(rhodes_vst3_quadrature_expression_transaction_tests tests/cpp/vst3_expression_transaction_test.cpp FALSE TRUE)
    foreach(test_name IN ITEMS queue_shapes expression_points late_co_batches ownership_recall flush_recall inactive_transactions)
        add_test(NAME vst3.expression_transaction_${test_name} COMMAND rhodes_vst3_expression_transaction_tests ${test_name})
        add_test(NAME vst3.filtered_expression_transaction_${test_name} COMMAND rhodes_vst3_filtered_expression_transaction_tests ${test_name})
        add_test(NAME vst3.quadrature_expression_transaction_${test_name} COMMAND rhodes_vst3_quadrature_expression_transaction_tests ${test_name})
    endforeach()
    rhodes_add_vst3_test(rhodes_vst3_expression_bounds_tests tests/cpp/vst3_expression_bounds_test.cpp)
    rhodes_add_vst3_test(rhodes_vst3_filtered_expression_bounds_tests tests/cpp/vst3_expression_bounds_test.cpp TRUE)
    rhodes_add_vst3_test(rhodes_vst3_quadrature_expression_bounds_tests tests/cpp/vst3_expression_bounds_test.cpp FALSE TRUE)
    foreach(test_name IN ITEMS queue_limits persistent_limits maximum_points command_events dispatch allocations)
        add_test(NAME vst3.expression_bounds_${test_name} COMMAND rhodes_vst3_expression_bounds_tests ${test_name})
        add_test(NAME vst3.filtered_expression_bounds_${test_name} COMMAND rhodes_vst3_filtered_expression_bounds_tests ${test_name})
        add_test(NAME vst3.quadrature_expression_bounds_${test_name} COMMAND rhodes_vst3_quadrature_expression_bounds_tests ${test_name})
    endforeach()
    foreach(sample_format IN ITEMS 32 64)
        add_test(NAME vst3.filtered_benchmark_smoke_${sample_format}
            COMMAND rhodes_vst3_filtered_benchmark --blocks 5 --warmup-blocks 2
                --block-size 64 --sample-rate 48000 --active-keys 73 --workload pedal
                --sample-format ${sample_format} --machine ci-smoke)
        add_test(NAME vst3.quadrature_benchmark_smoke_${sample_format}
            COMMAND rhodes_vst3_quadrature_benchmark --blocks 5 --warmup-blocks 2
                --block-size 64 --sample-rate 48000 --active-keys 73 --workload pedal
                --sample-format ${sample_format} --machine ci-smoke)
    endforeach()
    rhodes_add_vst3_test(rhodes_vst3_filtered_tests tests/cpp/vst3_filtered_test.cpp TRUE)
    foreach(test_name IN ITEMS scalar_output rate_contract state_contract reset_and_flush rejected_calls)
        add_test(NAME vst3.filtered_${test_name} COMMAND rhodes_vst3_filtered_tests ${test_name})
    endforeach()
    rhodes_add_vst3_test(rhodes_vst3_quadrature_contract_tests tests/cpp/vst3_quadrature_contract_test.cpp FALSE TRUE)
    foreach(test_name IN ITEMS qhost_identity_delay qhost_foreign_recall qhost_recall_order qhost_reset_tail)
        add_test(NAME vst3.quadrature_${test_name} COMMAND rhodes_vst3_quadrature_contract_tests ${test_name})
    endforeach()
    add_test(NAME vst3.benchmark_help COMMAND rhodes_vst3_benchmark --help)
    foreach(sample_format IN ITEMS 32 64)
        add_test(NAME vst3.benchmark_smoke_${sample_format} COMMAND rhodes_vst3_benchmark
            --blocks 5 --warmup-blocks 2 --block-size 64 --sample-rate 48000
            --active-keys 73 --workload pedal --sample-format ${sample_format}
            --machine ci-smoke)
    endforeach()
    add_test(NAME vst3.benchmark_json COMMAND ${CMAKE_COMMAND}
        "-DBENCHMARK=$<TARGET_FILE:rhodes_vst3_benchmark>"
        -DEXPECT_FILTERED=OFF
        -P "${PROJECT_SOURCE_DIR}/tests/cpp/vst3_benchmark_json.cmake")
    add_test(NAME vst3.filtered_benchmark_help COMMAND rhodes_vst3_filtered_benchmark --help)
    add_test(NAME vst3.filtered_benchmark_json COMMAND ${CMAKE_COMMAND}
        "-DBENCHMARK=$<TARGET_FILE:rhodes_vst3_filtered_benchmark>"
        -DEXPECT_FILTERED=ON
        -P "${PROJECT_SOURCE_DIR}/tests/cpp/vst3_benchmark_json.cmake")
    add_test(NAME vst3.quadrature_benchmark_help COMMAND rhodes_vst3_quadrature_benchmark --help)
    add_test(NAME vst3.quadrature_benchmark_json COMMAND ${CMAKE_COMMAND}
        "-DBENCHMARK=$<TARGET_FILE:rhodes_vst3_quadrature_benchmark>"
        -DEXPECT_AUDIO_PATH=quadrature
        -P "${PROJECT_SOURCE_DIR}/tests/cpp/vst3_benchmark_json.cmake")
    rhodes_add_vst3_test(rhodes_vst3_host_tests tests/cpp/vst3_host_test.cpp)
    rhodes_add_vst3_test(rhodes_vst3_channel_commands_tests tests/cpp/vst3_channel_commands_test.cpp)
    rhodes_add_vst3_test(rhodes_vst3_filtered_channel_commands_tests tests/cpp/vst3_channel_commands_test.cpp TRUE)
    rhodes_add_vst3_test(rhodes_vst3_quadrature_channel_commands_tests tests/cpp/vst3_channel_commands_test.cpp FALSE TRUE)
    add_test(NAME vst3.channel_commands COMMAND rhodes_vst3_channel_commands_tests)
    add_test(NAME vst3.filtered_channel_commands COMMAND rhodes_vst3_filtered_channel_commands_tests)
    add_test(NAME vst3.quadrature_channel_commands COMMAND rhodes_vst3_quadrature_channel_commands_tests)
    rhodes_add_vst3_test(rhodes_vst3_command_partition_tests tests/cpp/vst3_command_partition_test.cpp)
    rhodes_add_vst3_test(rhodes_vst3_filtered_command_partition_tests tests/cpp/vst3_command_partition_test.cpp TRUE)
    rhodes_add_vst3_test(rhodes_vst3_quadrature_command_partition_tests tests/cpp/vst3_command_partition_test.cpp FALSE TRUE)
    add_test(NAME vst3.command_partition COMMAND rhodes_vst3_command_partition_tests)
    add_test(NAME vst3.filtered_command_partition COMMAND rhodes_vst3_filtered_command_partition_tests)
    add_test(NAME vst3.quadrature_command_partition COMMAND rhodes_vst3_quadrature_command_partition_tests)
    rhodes_add_vst3_test(rhodes_vst3_pressure_tests tests/cpp/vst3_pressure_test.cpp)
    rhodes_add_vst3_test(rhodes_vst3_filtered_pressure_tests tests/cpp/vst3_pressure_test.cpp TRUE)
    rhodes_add_vst3_test(rhodes_vst3_quadrature_pressure_tests tests/cpp/vst3_pressure_test.cpp FALSE TRUE)
    add_test(NAME vst3.pressure COMMAND rhodes_vst3_pressure_tests)
    add_test(NAME vst3.filtered_pressure COMMAND rhodes_vst3_filtered_pressure_tests)
    add_test(NAME vst3.quadrature_pressure COMMAND rhodes_vst3_quadrature_pressure_tests)
    rhodes_add_vst3_test(rhodes_vst3_command_bounds_tests tests/cpp/vst3_command_bounds_test.cpp)
    rhodes_add_vst3_test(rhodes_vst3_filtered_command_bounds_tests tests/cpp/vst3_command_bounds_test.cpp TRUE)
    rhodes_add_vst3_test(rhodes_vst3_quadrature_command_bounds_tests tests/cpp/vst3_command_bounds_test.cpp FALSE TRUE)
    add_test(NAME vst3.command_bounds COMMAND rhodes_vst3_command_bounds_tests)
    add_test(NAME vst3.filtered_command_bounds COMMAND rhodes_vst3_filtered_command_bounds_tests)
    add_test(NAME vst3.quadrature_command_bounds COMMAND rhodes_vst3_quadrature_command_bounds_tests)
    rhodes_add_vst3_test(rhodes_vst3_control_text_tests tests/cpp/vst3_control_text_test.cpp FALSE FALSE TRUE)
    rhodes_add_vst3_test(rhodes_vst3_filtered_control_text_tests tests/cpp/vst3_control_text_test.cpp TRUE FALSE TRUE)
    rhodes_add_vst3_test(rhodes_vst3_quadrature_control_text_tests tests/cpp/vst3_control_text_test.cpp FALSE TRUE TRUE)
    add_test(NAME vst3.control_text COMMAND rhodes_vst3_control_text_tests)
    add_test(NAME vst3.filtered_control_text COMMAND rhodes_vst3_filtered_control_text_tests)
    add_test(NAME vst3.quadrature_control_text COMMAND rhodes_vst3_quadrature_control_text_tests)
    rhodes_add_vst3_test(rhodes_vst3_command_atomicity_tests tests/cpp/vst3_command_atomicity_test.cpp)
    rhodes_add_vst3_test(rhodes_vst3_filtered_command_atomicity_tests tests/cpp/vst3_command_atomicity_test.cpp TRUE)
    rhodes_add_vst3_test(rhodes_vst3_quadrature_command_atomicity_tests tests/cpp/vst3_command_atomicity_test.cpp FALSE TRUE)
    add_test(NAME vst3.command_atomicity COMMAND rhodes_vst3_command_atomicity_tests)
    add_test(NAME vst3.filtered_command_atomicity COMMAND rhodes_vst3_filtered_command_atomicity_tests)
    add_test(NAME vst3.quadrature_command_atomicity COMMAND rhodes_vst3_quadrature_command_atomicity_tests)
    rhodes_add_vst3_test(rhodes_vst3_filtered_regression_tests tests/cpp/vst3_host_test.cpp TRUE)
    rhodes_add_vst3_test(rhodes_vst3_quadrature_regression_tests tests/cpp/vst3_host_test.cpp FALSE TRUE)
    foreach(test_name IN ITEMS scalar_output overlapping_notes linear_automation
            dense_automation invalid_atomicity parameter_flush lifecycle deferred_state
            recall_abort stream_errors controller_mapping allocation_guard)
        add_test(NAME vst3.${test_name} COMMAND rhodes_vst3_host_tests ${test_name})
        add_test(NAME vst3.filtered_regression_${test_name}
            COMMAND rhodes_vst3_filtered_regression_tests ${test_name})
        add_test(NAME vst3.quadrature_regression_${test_name}
            COMMAND rhodes_vst3_quadrature_regression_tests ${test_name})
    endforeach()
    rhodes_add_vst3_test(rhodes_vst3_fp_tests tests/cpp/vst3_fp_environment_test.cpp)
    rhodes_add_vst3_test(rhodes_vst3_filtered_fp_tests tests/cpp/vst3_fp_environment_test.cpp TRUE)
    rhodes_add_vst3_test(rhodes_vst3_quadrature_fp_tests tests/cpp/vst3_fp_environment_test.cpp FALSE TRUE)
    foreach(test_name IN ITEMS baseline ftz daz ftz_daz)
        add_test(NAME vst3.fp_${test_name} COMMAND rhodes_vst3_fp_tests ${test_name})
        set_tests_properties(vst3.fp_${test_name} PROPERTIES SKIP_RETURN_CODE 77)
        add_test(NAME vst3.filtered_fp_${test_name} COMMAND rhodes_vst3_filtered_fp_tests ${test_name})
        set_tests_properties(vst3.filtered_fp_${test_name} PROPERTIES SKIP_RETURN_CODE 77)
        add_test(NAME vst3.quadrature_fp_${test_name} COMMAND rhodes_vst3_quadrature_fp_tests ${test_name})
        set_tests_properties(vst3.quadrature_fp_${test_name} PROPERTIES SKIP_RETURN_CODE 77)
    endforeach()
    rhodes_add_vst3_test(rhodes_vst3_buffer_tests tests/cpp/vst3_buffer_contract_test.cpp)
    rhodes_add_vst3_test(rhodes_vst3_filtered_buffer_tests tests/cpp/vst3_buffer_contract_test.cpp TRUE)
    rhodes_add_vst3_test(rhodes_vst3_quadrature_buffer_tests tests/cpp/vst3_buffer_contract_test.cpp FALSE TRUE)
    foreach(test_name IN ITEMS inactive_buffers flush_matrix rejected_buffers setup_rollback)
        add_test(NAME vst3.${test_name} COMMAND rhodes_vst3_buffer_tests ${test_name})
        add_test(NAME vst3.filtered_buffer_${test_name} COMMAND rhodes_vst3_filtered_buffer_tests ${test_name})
        add_test(NAME vst3.quadrature_buffer_${test_name} COMMAND rhodes_vst3_quadrature_buffer_tests ${test_name})
    endforeach()
    rhodes_add_vst3_test(rhodes_vst3_stream_tests tests/cpp/vst3_stream_contract_test.cpp)
    rhodes_add_vst3_test(rhodes_vst3_filtered_stream_tests tests/cpp/vst3_stream_contract_test.cpp TRUE)
    rhodes_add_vst3_test(rhodes_vst3_quadrature_stream_tests tests/cpp/vst3_stream_contract_test.cpp FALSE TRUE)
    foreach(test_name IN ITEMS partial_roundtrip read_failures write_failures malformed_state)
        add_test(NAME vst3.${test_name} COMMAND rhodes_vst3_stream_tests ${test_name})
        add_test(NAME vst3.filtered_stream_${test_name} COMMAND rhodes_vst3_filtered_stream_tests ${test_name})
        add_test(NAME vst3.quadrature_stream_${test_name} COMMAND rhodes_vst3_quadrature_stream_tests ${test_name})
    endforeach()
    rhodes_add_vst3_test(rhodes_vst3_automation_tests tests/cpp/vst3_automation_contract_test.cpp)
    rhodes_add_vst3_test(rhodes_vst3_filtered_automation_tests tests/cpp/vst3_automation_contract_test.cpp TRUE)
    rhodes_add_vst3_test(rhodes_vst3_quadrature_automation_tests tests/cpp/vst3_automation_contract_test.cpp FALSE TRUE)
    foreach(test_name IN ITEMS automation_model automation_ties automation_flush automation_rejection)
        add_test(NAME vst3.${test_name} COMMAND rhodes_vst3_automation_tests ${test_name})
        add_test(NAME vst3.filtered_automation_${test_name} COMMAND rhodes_vst3_filtered_automation_tests ${test_name})
        add_test(NAME vst3.quadrature_automation_${test_name} COMMAND rhodes_vst3_quadrature_automation_tests ${test_name})
    endforeach()
    rhodes_add_vst3_test(rhodes_vst3_note_model_tests tests/cpp/vst3_note_model_test.cpp)
    rhodes_add_vst3_test(rhodes_vst3_filtered_note_model_tests tests/cpp/vst3_note_model_test.cpp TRUE)
    rhodes_add_vst3_test(rhodes_vst3_quadrature_note_model_tests tests/cpp/vst3_note_model_test.cpp FALSE TRUE)
    foreach(test_name IN ITEMS identity_model capacity_model recall_model random_note_model)
        add_test(NAME vst3.${test_name} COMMAND rhodes_vst3_note_model_tests ${test_name})
        add_test(NAME vst3.filtered_note_${test_name} COMMAND rhodes_vst3_filtered_note_model_tests ${test_name})
        add_test(NAME vst3.quadrature_note_${test_name} COMMAND rhodes_vst3_quadrature_note_model_tests ${test_name})
    endforeach()
    rhodes_add_vst3_test(rhodes_vst3_state_concurrency_tests tests/cpp/vst3_state_concurrency_test.cpp)
    rhodes_add_vst3_test(rhodes_vst3_filtered_state_concurrency_tests tests/cpp/vst3_state_concurrency_test.cpp TRUE)
    rhodes_add_vst3_test(rhodes_vst3_quadrature_state_concurrency_tests tests/cpp/vst3_state_concurrency_test.cpp FALSE TRUE)
    target_link_libraries(rhodes_vst3_state_concurrency_tests PRIVATE Threads::Threads)
    target_link_libraries(rhodes_vst3_filtered_state_concurrency_tests PRIVATE Threads::Threads)
    target_link_libraries(rhodes_vst3_quadrature_state_concurrency_tests PRIVATE Threads::Threads)
    foreach(test_name IN ITEMS state_preflight_race state_callback_stress)
        add_test(NAME vst3.${test_name} COMMAND rhodes_vst3_state_concurrency_tests ${test_name})
        add_test(NAME vst3.filtered_state_${test_name} COMMAND rhodes_vst3_filtered_state_concurrency_tests ${test_name})
        add_test(NAME vst3.quadrature_state_${test_name} COMMAND rhodes_vst3_quadrature_state_concurrency_tests ${test_name})
    endforeach()
    rhodes_add_vst3_test(rhodes_vst3_expression_concurrency_tests tests/cpp/vst3_expression_concurrency_test.cpp)
    rhodes_add_vst3_test(rhodes_vst3_filtered_expression_concurrency_tests tests/cpp/vst3_expression_concurrency_test.cpp TRUE)
    rhodes_add_vst3_test(rhodes_vst3_quadrature_expression_concurrency_tests tests/cpp/vst3_expression_concurrency_test.cpp FALSE TRUE)
    target_link_libraries(rhodes_vst3_expression_concurrency_tests PRIVATE Threads::Threads)
    target_link_libraries(rhodes_vst3_filtered_expression_concurrency_tests PRIVATE Threads::Threads)
    target_link_libraries(rhodes_vst3_quadrature_expression_concurrency_tests PRIVATE Threads::Threads)
    foreach(test_name IN ITEMS mailbox_ordered host_preflight adaptive_snapshots live_anchors callback_stress)
        add_test(NAME vst3.expression_concurrency_${test_name} COMMAND rhodes_vst3_expression_concurrency_tests ${test_name})
        add_test(NAME vst3.filtered_expression_concurrency_${test_name} COMMAND rhodes_vst3_filtered_expression_concurrency_tests ${test_name})
        add_test(NAME vst3.quadrature_expression_concurrency_${test_name} COMMAND rhodes_vst3_quadrature_expression_concurrency_tests ${test_name})
        set_tests_properties(vst3.expression_concurrency_${test_name}
            vst3.filtered_expression_concurrency_${test_name}
            vst3.quadrature_expression_concurrency_${test_name} PROPERTIES TIMEOUT 60)
    endforeach()
    # Fault injection belongs only to these test executables. Keep the shipped
    # processor/core unmodified and require the 64-bit ELF ABI used by the
    # explicitly named GNU-compatible linker symbols below.
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SIZEOF_VOID_P EQUAL 8
            AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        include(CheckLinkerFlag)
        include(CheckCXXSourceCompiles)
        check_linker_flag(CXX "LINKER:--wrap=rhodes_test_linker_probe" RHODES_HAS_LINKER_WRAP)
        check_cxx_source_compiles("#include <cstddef>
#ifndef __GLIBCXX__
#error The fault wrapper symbols require libstdc++
#endif
int main() { return 0; }" RHODES_FAULT_LIBSTDCXX)
        if(RHODES_HAS_LINKER_WRAP AND RHODES_FAULT_LIBSTDCXX)
            rhodes_add_vst3_test(rhodes_vst3_expression_fault_tests tests/cpp/vst3_expression_fault_test.cpp)
            rhodes_add_vst3_test(rhodes_vst3_filtered_expression_fault_tests tests/cpp/vst3_expression_fault_test.cpp TRUE)
            rhodes_add_vst3_test(rhodes_vst3_quadrature_expression_fault_tests tests/cpp/vst3_expression_fault_test.cpp FALSE TRUE)
            foreach(target IN ITEMS rhodes_vst3_expression_fault_tests
                    rhodes_vst3_filtered_expression_fault_tests rhodes_vst3_quadrature_expression_fault_tests)
                target_link_options(${target} PRIVATE
                    "LINKER:--wrap=_ZN6rhodes14ResearchEngine7processESt4spanIdLm18446744073709551615EE"
                    "LINKER:--wrap=_ZNK6rhodes14ResearchEngine12observe_cellERKNS_25FractionalObservationPlanE")
            endforeach()
            foreach(test_name IN ITEMS interception engine_failure monitor_fault float_conversion fault_state reset_recovery)
                add_test(NAME vst3.expression_fault_${test_name} COMMAND rhodes_vst3_expression_fault_tests ${test_name})
                add_test(NAME vst3.filtered_expression_fault_${test_name} COMMAND rhodes_vst3_filtered_expression_fault_tests ${test_name})
                add_test(NAME vst3.quadrature_expression_fault_${test_name} COMMAND rhodes_vst3_quadrature_expression_fault_tests ${test_name})
                set_tests_properties(vst3.expression_fault_${test_name}
                    vst3.filtered_expression_fault_${test_name}
                    vst3.quadrature_expression_fault_${test_name}
                    PROPERTIES TIMEOUT 60 LABELS "host-fault;linux-linker-wrap")
            endforeach()
        else()
            message(STATUS "Expression host-fault tests omitted: libstdc++ and linker --wrap are required")
        endif()
    endif()
    add_dependencies(rhodes_vst3 validator)
    add_dependencies(rhodes_vst3_filtered validator)
    add_dependencies(rhodes_vst3_quadrature validator)
    get_target_property(rhodes_plugin_bundle rhodes_vst3 SMTG_PLUGIN_PACKAGE_PATH)
    add_test(NAME vst3.sdk_validator
        COMMAND $<TARGET_FILE:validator> "${rhodes_plugin_bundle}")
    set_tests_properties(vst3.sdk_validator PROPERTIES TIMEOUT 180)
    get_target_property(rhodes_filtered_bundle rhodes_vst3_filtered SMTG_PLUGIN_PACKAGE_PATH)
    get_target_property(rhodes_quadrature_bundle rhodes_vst3_quadrature SMTG_PLUGIN_PACKAGE_PATH)
    if(SMTG_LINUX OR SMTG_WIN OR SMTG_MAC)
        # The host exercises independently loaded production modules. Keep SDK
        # loader sources under SDK options and strict warnings on our test.
        # Match the pinned validator/editorhost platform sources and ARC setup.
        if(SMTG_MAC)
            set(rhodes_module_loader "${RHODES_VST3_SDK_ROOT}/public.sdk/source/vst/hosting/module_mac.mm")
            set_source_files_properties("${rhodes_module_loader}" PROPERTIES
                COMPILE_FLAGS "-fobjc-arc")
        elseif(SMTG_WIN)
            set(rhodes_module_loader "${RHODES_VST3_SDK_ROOT}/public.sdk/source/vst/hosting/module_win32.cpp")
        else()
            set(rhodes_module_loader "${RHODES_VST3_SDK_ROOT}/public.sdk/source/vst/hosting/module_linux.cpp")
        endif()
        add_library(rhodes_vst3_loader_support STATIC
            "${rhodes_module_loader}"
            "${RHODES_VST3_SDK_ROOT}/public.sdk/source/common/memorystream.cpp")
        target_compile_features(rhodes_vst3_loader_support PRIVATE cxx_std_20)
        target_link_libraries(rhodes_vst3_loader_support PUBLIC sdk_hosting)
        if(SMTG_WIN OR SMTG_MAC)
            # SDK directory options do not propagate to targets created here.
            # Reuse its host target's own options only for these SDK sources.
            target_compile_options(rhodes_vst3_loader_support PRIVATE
                "$<TARGET_PROPERTY:sdk_hosting,COMPILE_OPTIONS>")
            target_compile_definitions(rhodes_vst3_loader_support PRIVATE
                "$<TARGET_PROPERTY:sdk_hosting,COMPILE_DEFINITIONS>")
        endif()
        if(SMTG_MAC)
            # Cocoa is used by module_mac.mm; the static archive must carry the
            # framework to each host executable, including non-Xcode builds.
            find_library(RHODES_COCOA_FRAMEWORK Cocoa REQUIRED)
            target_link_libraries(rhodes_vst3_loader_support PUBLIC "${RHODES_COCOA_FRAMEWORK}")
            smtg_target_setup_universal_binary(rhodes_vst3_loader_support)
        endif()
        add_executable(rhodes_vst3_dual_module_tests
            "${PROJECT_SOURCE_DIR}/tests/cpp/vst3_dual_module_test.cpp")
        target_compile_features(rhodes_vst3_dual_module_tests PRIVATE cxx_std_20)
        target_include_directories(rhodes_vst3_dual_module_tests SYSTEM PRIVATE "${RHODES_VST3_SDK_ROOT}")
        target_link_libraries(rhodes_vst3_dual_module_tests PRIVATE rhodes_vst3_loader_support)
        rhodes_target_options(rhodes_vst3_dual_module_tests)
        add_dependencies(rhodes_vst3_dual_module_tests rhodes_vst3 rhodes_vst3_filtered)
        add_dependencies(rhodes_vst3_checks rhodes_vst3_dual_module_tests)
        add_test(NAME vst3.dual_module_coexist COMMAND rhodes_vst3_dual_module_tests
            coexist "${rhodes_plugin_bundle}" "${rhodes_filtered_bundle}")
        add_test(NAME vst3.dual_module_duplicate_guard COMMAND rhodes_vst3_dual_module_tests
            duplicate_guard "${rhodes_plugin_bundle}")
        set_tests_properties(vst3.dual_module_coexist vst3.dual_module_duplicate_guard PROPERTIES TIMEOUT 30)
        add_executable(rhodes_vst3_triple_module_tests
            "${PROJECT_SOURCE_DIR}/tests/cpp/vst3_triple_module_test.cpp")
        target_compile_features(rhodes_vst3_triple_module_tests PRIVATE cxx_std_20)
        target_include_directories(rhodes_vst3_triple_module_tests SYSTEM PRIVATE "${RHODES_VST3_SDK_ROOT}")
        target_link_libraries(rhodes_vst3_triple_module_tests PRIVATE rhodes_vst3_loader_support)
        rhodes_target_options(rhodes_vst3_triple_module_tests)
        add_dependencies(rhodes_vst3_triple_module_tests rhodes_vst3 rhodes_vst3_filtered rhodes_vst3_quadrature)
        add_dependencies(rhodes_vst3_checks rhodes_vst3_triple_module_tests)
        foreach(test_name IN ITEMS coexist duplicate_guard)
            add_test(NAME vst3.triple_module_${test_name} COMMAND rhodes_vst3_triple_module_tests
                ${test_name} "${rhodes_plugin_bundle}" "${rhodes_filtered_bundle}" "${rhodes_quadrature_bundle}")
            set_tests_properties(vst3.triple_module_${test_name} PROPERTIES TIMEOUT 60)
        endforeach()
        add_executable(rhodes_vst3_loaded_midi_tests
            "${PROJECT_SOURCE_DIR}/tests/cpp/vst3_loaded_midi_test.cpp")
        target_compile_features(rhodes_vst3_loaded_midi_tests PRIVATE cxx_std_20)
        target_include_directories(rhodes_vst3_loaded_midi_tests SYSTEM PRIVATE "${RHODES_VST3_SDK_ROOT}")
        target_link_libraries(rhodes_vst3_loaded_midi_tests PRIVATE rhodes_vst3_loader_support)
        rhodes_target_options(rhodes_vst3_loaded_midi_tests)
        add_dependencies(rhodes_vst3_loaded_midi_tests rhodes_vst3 rhodes_vst3_filtered rhodes_vst3_quadrature)
        add_dependencies(rhodes_vst3_checks rhodes_vst3_loaded_midi_tests)
        add_test(NAME vst3.loaded_midi COMMAND rhodes_vst3_loaded_midi_tests
            "${rhodes_plugin_bundle}" "${rhodes_filtered_bundle}" "${rhodes_quadrature_bundle}")
        set_tests_properties(vst3.loaded_midi PROPERTIES TIMEOUT 60)
        add_executable(rhodes_vst3_loaded_panel_audio_tests
            "${PROJECT_SOURCE_DIR}/tests/cpp/vst3_loaded_panel_audio_test.cpp")
        target_compile_features(rhodes_vst3_loaded_panel_audio_tests PRIVATE cxx_std_20)
        target_include_directories(rhodes_vst3_loaded_panel_audio_tests SYSTEM PRIVATE
            "${RHODES_VST3_SDK_ROOT}")
        target_link_libraries(rhodes_vst3_loaded_panel_audio_tests PRIVATE rhodes_vst3_loader_support)
        rhodes_target_options(rhodes_vst3_loaded_panel_audio_tests)
        add_dependencies(rhodes_vst3_loaded_panel_audio_tests
            rhodes_vst3 rhodes_vst3_filtered rhodes_vst3_quadrature)
        add_dependencies(rhodes_vst3_checks rhodes_vst3_loaded_panel_audio_tests)
        add_test(NAME vst3.loaded_panel_audio COMMAND rhodes_vst3_loaded_panel_audio_tests
            "${rhodes_plugin_bundle}" "${rhodes_filtered_bundle}" "${rhodes_quadrature_bundle}")
        set_tests_properties(vst3.loaded_panel_audio PROPERTIES TIMEOUT 120)
        if(RHODES_BUILD_VST3_EDITOR)
            add_executable(rhodes_vst3_photo_editor_tests
                "${PROJECT_SOURCE_DIR}/tests/cpp/vst3_photo_editor_test.cpp")
            target_compile_features(rhodes_vst3_photo_editor_tests PRIVATE cxx_std_20)
            target_include_directories(rhodes_vst3_photo_editor_tests SYSTEM PRIVATE "${RHODES_VST3_SDK_ROOT}")
            target_link_libraries(rhodes_vst3_photo_editor_tests PRIVATE rhodes_vst3_loader_support)
            rhodes_target_options(rhodes_vst3_photo_editor_tests)
            add_dependencies(rhodes_vst3_photo_editor_tests rhodes_vst3 rhodes_vst3_filtered rhodes_vst3_quadrature)
            add_dependencies(rhodes_vst3_checks rhodes_vst3_photo_editor_tests)
            add_test(NAME vst3.photo_editor_contract COMMAND rhodes_vst3_photo_editor_tests
                "${rhodes_plugin_bundle}" "${rhodes_filtered_bundle}" "${rhodes_quadrature_bundle}")
            set_tests_properties(vst3.photo_editor_contract PROPERTIES TIMEOUT 60)
            if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
                # Drive the public loaded editor through real native X11
                # dispatch, without linking its implementation into the host.
                add_executable(rhodes_vst3_photo_lifecycle_tests
                    "${PROJECT_SOURCE_DIR}/tests/cpp/vst3_photo_lifecycle_test.cpp")
                target_compile_features(rhodes_vst3_photo_lifecycle_tests PRIVATE cxx_std_20)
                target_include_directories(rhodes_vst3_photo_lifecycle_tests SYSTEM PRIVATE
                    "${RHODES_VST3_SDK_ROOT}")
                target_link_libraries(rhodes_vst3_photo_lifecycle_tests PRIVATE
                    rhodes_vst3_loader_support PkgConfig::RHODES_EDITOR_XCB)
                rhodes_target_options(rhodes_vst3_photo_lifecycle_tests)
                add_dependencies(rhodes_vst3_photo_lifecycle_tests rhodes_vst3)
                add_dependencies(rhodes_vst3_checks rhodes_vst3_photo_lifecycle_tests)
                foreach(test_name IN ITEMS normal_close remove_begin remove_perform remove_end
                        nested_pump timer_refused repeated_remove reopen parent_destroy_after_remove
                        release_host_reference_after_remove focus_remove reopen_cycles sibling_survives)
                    add_test(NAME vst3.photo_lifecycle.${test_name}
                        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/run_xvfb_test.py"
                            --xvfb "${RHODES_XVFB_EXECUTABLE}" --xauth "${RHODES_XAUTH_EXECUTABLE}"
                            --xdpyinfo "${RHODES_XDPYINFO_EXECUTABLE}"
                            --startup-timeout 10 --test-timeout 30 --
                            $<TARGET_FILE:rhodes_vst3_photo_lifecycle_tests>
                            "${rhodes_plugin_bundle}" ${test_name})
                    set_tests_properties(vst3.photo_lifecycle.${test_name} PROPERTIES
                        TIMEOUT 60 LABELS "editor;linux-xcb;lifecycle")
                endforeach()
            endif()
            if(SMTG_MAC)
                smtg_target_setup_universal_binary(rhodes_vst3_photo_editor_tests)
                smtg_target_codesign(rhodes_vst3_photo_editor_tests)
            endif()
        endif()
        add_executable(rhodes_vst3_loaded_expression_tests
            "${PROJECT_SOURCE_DIR}/tests/cpp/vst3_loaded_expression_test.cpp")
        target_compile_features(rhodes_vst3_loaded_expression_tests PRIVATE cxx_std_20)
        target_include_directories(rhodes_vst3_loaded_expression_tests SYSTEM PRIVATE "${RHODES_VST3_SDK_ROOT}")
        target_link_libraries(rhodes_vst3_loaded_expression_tests PRIVATE rhodes_vst3_loader_support)
        rhodes_target_options(rhodes_vst3_loaded_expression_tests)
        add_dependencies(rhodes_vst3_loaded_expression_tests rhodes_vst3 rhodes_vst3_filtered rhodes_vst3_quadrature)
        add_dependencies(rhodes_vst3_checks rhodes_vst3_loaded_expression_tests)
        foreach(test_name IN ITEMS controls_state audio_layout coexist)
            add_test(NAME vst3.loaded_expression_${test_name} COMMAND rhodes_vst3_loaded_expression_tests
                ${test_name} "${rhodes_plugin_bundle}" "${rhodes_filtered_bundle}" "${rhodes_quadrature_bundle}")
            set_tests_properties(vst3.loaded_expression_${test_name} PROPERTIES TIMEOUT 60)
        endforeach()
        add_executable(rhodes_vst3_legacy_capture
            "${PROJECT_SOURCE_DIR}/tests/cpp/vst3_legacy_capture.cpp")
        target_compile_features(rhodes_vst3_legacy_capture PRIVATE cxx_std_20)
        target_include_directories(rhodes_vst3_legacy_capture SYSTEM PRIVATE "${RHODES_VST3_SDK_ROOT}")
        target_link_libraries(rhodes_vst3_legacy_capture PRIVATE rhodes_vst3_loader_support)
        rhodes_target_options(rhodes_vst3_legacy_capture)
        add_dependencies(rhodes_vst3_legacy_capture rhodes_vst3 rhodes_vst3_filtered rhodes_vst3_quadrature)
        add_dependencies(rhodes_vst3_checks rhodes_vst3_legacy_capture)
        set(RHODES_VST3_LEGACY_BUNDLE_ROOT "" CACHE PATH
            "Optional three-bundle baseline directory with a verified source/module manifest")
        if(RHODES_VST3_LEGACY_BUNDLE_ROOT)
            if(NOT IS_DIRECTORY "${RHODES_VST3_LEGACY_BUNDLE_ROOT}" OR
                    NOT EXISTS "${RHODES_VST3_LEGACY_BUNDLE_ROOT}/manifest.json")
                message(FATAL_ERROR "RHODES_VST3_LEGACY_BUNDLE_ROOT requires baseline bundles and manifest.json")
            endif()
            find_package(Python3 COMPONENTS Interpreter REQUIRED)
            get_filename_component(rhodes_current_bundle_root "${rhodes_plugin_bundle}" DIRECTORY)
            add_test(NAME vst3.legacy_unity_comparison
                COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/compare_vst3_legacy.py"
                    --capture $<TARGET_FILE:rhodes_vst3_legacy_capture>
                    --baseline-root "${RHODES_VST3_LEGACY_BUNDLE_ROOT}"
                    --current-root "${rhodes_current_bundle_root}"
                    --output-dir "${PROJECT_BINARY_DIR}/legacy-comparison/$<CONFIG>"
                    --sdk-root "${RHODES_VST3_SDK_ROOT}")
            set_tests_properties(vst3.legacy_unity_comparison PROPERTIES TIMEOUT 300)
        endif()
        if(SMTG_MAC)
            foreach(target IN ITEMS rhodes_vst3_dual_module_tests
                    rhodes_vst3_triple_module_tests rhodes_vst3_loaded_midi_tests
                    rhodes_vst3_loaded_expression_tests rhodes_vst3_loaded_panel_audio_tests
                    rhodes_vst3_legacy_capture)
                smtg_target_setup_universal_binary(${target})
                smtg_target_codesign(${target})
            endforeach()
        endif()
    endif()
    add_test(NAME vst3.filtered_sdk_validator
        COMMAND $<TARGET_FILE:validator> "${rhodes_filtered_bundle}")
    set_tests_properties(vst3.filtered_sdk_validator PROPERTIES TIMEOUT 180)
    add_test(NAME vst3.quadrature_sdk_validator
        COMMAND $<TARGET_FILE:validator> "${rhodes_quadrature_bundle}")
    set_tests_properties(vst3.quadrature_sdk_validator PROPERTIES TIMEOUT 180)
    # Validate the generated metadata against each actual loadable factory.
    # The audio validator and resource-presence test cover different contracts.
    add_dependencies(rhodes_vst3_checks moduleinfotool)
    add_test(NAME vst3.moduleinfo_validator
        COMMAND $<TARGET_FILE:moduleinfotool> -validate -path "${rhodes_plugin_bundle}")
    add_test(NAME vst3.filtered_moduleinfo_validator
        COMMAND $<TARGET_FILE:moduleinfotool> -validate -path "${rhodes_filtered_bundle}")
    add_test(NAME vst3.quadrature_moduleinfo_validator
        COMMAND $<TARGET_FILE:moduleinfotool> -validate -path "${rhodes_quadrature_bundle}")
    set_tests_properties(vst3.moduleinfo_validator vst3.filtered_moduleinfo_validator
        vst3.quadrature_moduleinfo_validator PROPERTIES TIMEOUT 60)
    add_test(NAME vst3.bundle_resources COMMAND ${CMAKE_COMMAND}
        "-DDIRECT_BUNDLE=${rhodes_plugin_bundle}"
        "-DFILTERED_BUNDLE=${rhodes_filtered_bundle}"
        "-DQUADRATURE_BUNDLE=${rhodes_quadrature_bundle}"
        "-DSDK_LICENSE=${RHODES_VST3_SDK_ROOT}/LICENSE.txt"
        "-DTHIRD_PARTY_NOTICES=${PROJECT_SOURCE_DIR}/plugins/vst3/THIRD_PARTY_NOTICES.md"
        "-DEDITOR_ENABLED=${RHODES_BUILD_VST3_EDITOR}"
        "-DEDITOR_RESOURCE_ROOT=${PROJECT_SOURCE_DIR}/plugins/vst3/resources"
        "-DVSTGUI_ROOT=${RHODES_VST3_SDK_ROOT}/vstgui4"
        -P "${PROJECT_SOURCE_DIR}/tests/cpp/vst3_bundle_resources.cmake")
endif()
