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
        message(FATAL_ERROR "VST3 SDK ${component}: missing Git checkout. Initialize the four documented submodules; see docs/VST3.md")
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

set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "Headless instrument" FORCE)
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

function(rhodes_add_plugin target package filtered bundle_id)
    if(filtered)
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
    if(filtered)
        target_compile_definitions(${target} PRIVATE RHODES_FILTERED_VST3=1)
    endif()
    set_target_properties(${target} PROPERTIES
        CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
    target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}")
    target_include_directories(${target} SYSTEM PRIVATE "${RHODES_VST3_SDK_ROOT}")
    target_link_libraries(${target} PRIVATE rhodes_core sdk)
    rhodes_target_options(${target})
    if(SMTG_MAC)
        smtg_target_set_bundle(${target} BUNDLE_IDENTIFIER "${bundle_id}"
            COMPANY_NAME "Independent Rhodes Research")
    endif()
    # The pinned SDK associates copy commands with MAIN_DEPENDENCY source
    # paths. Reusing one source for two bundles in a directory collides, so each
    # target gets distinct configure-tracked inputs with the original basenames.
    set(resource_inputs "${CMAKE_CURRENT_BINARY_DIR}/rhodes-resources/${target}")
    file(MAKE_DIRECTORY "${resource_inputs}")
    configure_file("${RHODES_VST3_SDK_ROOT}/LICENSE.txt"
        "${resource_inputs}/LICENSE.txt" COPYONLY)
    configure_file("${PROJECT_SOURCE_DIR}/plugins/vst3/THIRD_PARTY_NOTICES.md"
        "${resource_inputs}/THIRD_PARTY_NOTICES.md" COPYONLY)
    smtg_target_add_plugin_resources(${target} RESOURCES
        "${resource_inputs}/LICENSE.txt" "${resource_inputs}/THIRD_PARTY_NOTICES.md")
endfunction()
rhodes_add_plugin(rhodes_vst3 "RhodesResearch" FALSE "org.rhodesresearch.experimental")
rhodes_add_plugin(rhodes_vst3_filtered "RhodesResearchFiltered" TRUE "org.rhodesresearch.filtered")

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

if(BUILD_TESTING)
    # One explicit build target keeps CI and documented test builds complete.
    add_custom_target(rhodes_vst3_checks)
    add_dependencies(rhodes_vst3_checks rhodes_vst3 rhodes_vst3_benchmark
        rhodes_vst3_filtered rhodes_vst3_filtered_benchmark)
    function(rhodes_add_vst3_test target source)
        add_executable(${target}
            "${PROJECT_SOURCE_DIR}/${source}"
            "${PROJECT_SOURCE_DIR}/plugins/vst3/processor.cpp"
            "${PROJECT_SOURCE_DIR}/plugins/vst3/controller.cpp"
            "${PROJECT_SOURCE_DIR}/plugins/vst3/state_stream.cpp")
        target_compile_features(${target} PRIVATE cxx_std_20)
        target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}")
        target_include_directories(${target} SYSTEM PRIVATE "${RHODES_VST3_SDK_ROOT}")
        target_link_libraries(${target} PRIVATE rhodes_core sdk)
        if(ARGC GREATER 2 AND ARGV2)
            target_compile_definitions(${target} PRIVATE RHODES_FILTERED_VST3=1)
        endif()
        rhodes_target_options(${target})
        add_dependencies(rhodes_vst3_checks ${target})
    endfunction()
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
    foreach(sample_format IN ITEMS 32 64)
        add_test(NAME vst3.filtered_benchmark_smoke_${sample_format}
            COMMAND rhodes_vst3_filtered_benchmark --blocks 5 --warmup-blocks 2
                --block-size 64 --sample-rate 48000 --active-keys 73 --workload pedal
                --sample-format ${sample_format} --machine ci-smoke)
    endforeach()
    rhodes_add_vst3_test(rhodes_vst3_filtered_tests tests/cpp/vst3_filtered_test.cpp TRUE)
    foreach(test_name IN ITEMS scalar_output rate_contract state_contract reset_and_flush rejected_calls)
        add_test(NAME vst3.filtered_${test_name} COMMAND rhodes_vst3_filtered_tests ${test_name})
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
        -P "${PROJECT_SOURCE_DIR}/tests/cpp/vst3_benchmark_json.cmake")
    rhodes_add_vst3_test(rhodes_vst3_host_tests tests/cpp/vst3_host_test.cpp)
    rhodes_add_vst3_test(rhodes_vst3_filtered_regression_tests tests/cpp/vst3_host_test.cpp TRUE)
    foreach(test_name IN ITEMS scalar_output overlapping_notes linear_automation
            dense_automation invalid_atomicity parameter_flush lifecycle deferred_state
            recall_abort stream_errors controller_mapping allocation_guard)
        add_test(NAME vst3.${test_name} COMMAND rhodes_vst3_host_tests ${test_name})
        add_test(NAME vst3.filtered_regression_${test_name}
            COMMAND rhodes_vst3_filtered_regression_tests ${test_name})
    endforeach()
    rhodes_add_vst3_test(rhodes_vst3_fp_tests tests/cpp/vst3_fp_environment_test.cpp)
    rhodes_add_vst3_test(rhodes_vst3_filtered_fp_tests tests/cpp/vst3_fp_environment_test.cpp TRUE)
    foreach(test_name IN ITEMS baseline ftz daz ftz_daz)
        add_test(NAME vst3.fp_${test_name} COMMAND rhodes_vst3_fp_tests ${test_name})
        set_tests_properties(vst3.fp_${test_name} PROPERTIES SKIP_RETURN_CODE 77)
        add_test(NAME vst3.filtered_fp_${test_name} COMMAND rhodes_vst3_filtered_fp_tests ${test_name})
        set_tests_properties(vst3.filtered_fp_${test_name} PROPERTIES SKIP_RETURN_CODE 77)
    endforeach()
    rhodes_add_vst3_test(rhodes_vst3_buffer_tests tests/cpp/vst3_buffer_contract_test.cpp)
    foreach(test_name IN ITEMS inactive_buffers flush_matrix rejected_buffers setup_rollback)
        add_test(NAME vst3.${test_name} COMMAND rhodes_vst3_buffer_tests ${test_name})
    endforeach()
    rhodes_add_vst3_test(rhodes_vst3_stream_tests tests/cpp/vst3_stream_contract_test.cpp)
    rhodes_add_vst3_test(rhodes_vst3_filtered_stream_tests tests/cpp/vst3_stream_contract_test.cpp TRUE)
    foreach(test_name IN ITEMS partial_roundtrip read_failures write_failures malformed_state)
        add_test(NAME vst3.${test_name} COMMAND rhodes_vst3_stream_tests ${test_name})
        add_test(NAME vst3.filtered_stream_${test_name} COMMAND rhodes_vst3_filtered_stream_tests ${test_name})
    endforeach()
    rhodes_add_vst3_test(rhodes_vst3_automation_tests tests/cpp/vst3_automation_contract_test.cpp)
    foreach(test_name IN ITEMS automation_model automation_ties automation_flush automation_rejection)
        add_test(NAME vst3.${test_name} COMMAND rhodes_vst3_automation_tests ${test_name})
    endforeach()
    rhodes_add_vst3_test(rhodes_vst3_note_model_tests tests/cpp/vst3_note_model_test.cpp)
    foreach(test_name IN ITEMS identity_model capacity_model recall_model random_note_model)
        add_test(NAME vst3.${test_name} COMMAND rhodes_vst3_note_model_tests ${test_name})
    endforeach()
    rhodes_add_vst3_test(rhodes_vst3_state_concurrency_tests tests/cpp/vst3_state_concurrency_test.cpp)
    rhodes_add_vst3_test(rhodes_vst3_filtered_state_concurrency_tests tests/cpp/vst3_state_concurrency_test.cpp TRUE)
    target_link_libraries(rhodes_vst3_state_concurrency_tests PRIVATE Threads::Threads)
    target_link_libraries(rhodes_vst3_filtered_state_concurrency_tests PRIVATE Threads::Threads)
    foreach(test_name IN ITEMS state_preflight_race state_callback_stress)
        add_test(NAME vst3.${test_name} COMMAND rhodes_vst3_state_concurrency_tests ${test_name})
        add_test(NAME vst3.filtered_state_${test_name} COMMAND rhodes_vst3_filtered_state_concurrency_tests ${test_name})
    endforeach()
    add_dependencies(rhodes_vst3 validator)
    add_dependencies(rhodes_vst3_filtered validator)
    get_target_property(rhodes_plugin_bundle rhodes_vst3 SMTG_PLUGIN_PACKAGE_PATH)
    add_test(NAME vst3.sdk_validator
        COMMAND $<TARGET_FILE:validator> "${rhodes_plugin_bundle}")
    set_tests_properties(vst3.sdk_validator PROPERTIES TIMEOUT 180)
    get_target_property(rhodes_filtered_bundle rhodes_vst3_filtered SMTG_PLUGIN_PACKAGE_PATH)
    add_test(NAME vst3.filtered_sdk_validator
        COMMAND $<TARGET_FILE:validator> "${rhodes_filtered_bundle}")
    set_tests_properties(vst3.filtered_sdk_validator PROPERTIES TIMEOUT 180)
    add_test(NAME vst3.bundle_resources COMMAND ${CMAKE_COMMAND}
        "-DDIRECT_BUNDLE=${rhodes_plugin_bundle}"
        "-DFILTERED_BUNDLE=${rhodes_filtered_bundle}"
        "-DSDK_LICENSE=${RHODES_VST3_SDK_ROOT}/LICENSE.txt"
        "-DTHIRD_PARTY_NOTICES=${PROJECT_SOURCE_DIR}/plugins/vst3/THIRD_PARTY_NOTICES.md"
        -P "${PROJECT_SOURCE_DIR}/tests/cpp/vst3_bundle_resources.cmake")
endif()
