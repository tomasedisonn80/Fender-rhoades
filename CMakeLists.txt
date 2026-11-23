cmake_minimum_required(VERSION 3.24)
project(RhodesResearch VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
option(RHODES_WARNINGS_AS_ERRORS "Treat project compiler warnings as errors" ON)
option(RHODES_ENABLE_SANITIZERS "Enable address and undefined-behavior sanitizers" OFF)

if(RHODES_ENABLE_SANITIZERS)
    if(MSVC OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"
            OR NOT CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$")
        message(FATAL_ERROR
            "RHODES_ENABLE_SANITIZERS requires GCC or Clang with a GNU-style "
            "driver; MSVC, clang-cl, and other compiler families are unsupported.")
    endif()
endif()

add_library(rhodes_core src/research_engine.cpp)
target_include_directories(rhodes_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_features(rhodes_core PUBLIC cxx_std_20)
set_target_properties(rhodes_core PROPERTIES CXX_EXTENSIONS OFF)
set_target_properties(rhodes_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

function(rhodes_target_options target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /fp:precise)
        if(RHODES_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
        if(RHODES_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
        if(RHODES_ENABLE_SANITIZERS)
            target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(${target} PRIVATE -fsanitize=address,undefined)
        endif()
    endif()
endfunction()
rhodes_target_options(rhodes_core)

add_executable(rhodes_render apps/render.cpp)
target_link_libraries(rhodes_render PRIVATE rhodes_core)
rhodes_target_options(rhodes_render)

add_executable(rhodes_benchmark apps/benchmark.cpp)
target_link_libraries(rhodes_benchmark PRIVATE rhodes_core)
target_compile_definitions(rhodes_benchmark PRIVATE RHODES_BUILD_CONFIGURATION="$<CONFIG>")
rhodes_target_options(rhodes_benchmark)

if(BUILD_TESTING)
    add_executable(rhodes_tests tests/cpp/research_engine_test.cpp)
    target_link_libraries(rhodes_tests PRIVATE rhodes_core)
    rhodes_target_options(rhodes_tests)
    foreach(test_name IN ITEMS exact_solution energy_loss energy_conservation
            retrigger pickup_derivative pickup_intermodulation network_loading
            invalid_input silence block_partition pedal_release supported_rates)
        add_test(NAME core.${test_name} COMMAND rhodes_tests ${test_name})
    endforeach()
    add_executable(rhodes_review_tests tests/cpp/review_regression.cpp)
    target_link_libraries(rhodes_review_tests PRIVATE rhodes_core)
    rhodes_target_options(rhodes_review_tests)
    foreach(test_name IN ITEMS modal_boundaries rejected_updates engine_controls engine_combined_pickup
            network_extreme_divider)
        add_test(NAME review.${test_name} COMMAND rhodes_review_tests ${test_name})
    endforeach()
    add_executable(rhodes_event_tests tests/cpp/event_processor_test.cpp)
    target_link_libraries(rhodes_event_tests PRIVATE rhodes_core)
    rhodes_target_options(rhodes_event_tests)
    foreach(test_name IN ITEMS scalar_partitions equal_offset_order preflight_atomicity
            invalid_events block_limits sample_fault_reset event_fault_reset)
        add_test(NAME runtime.${test_name} COMMAND rhodes_event_tests ${test_name})
    endforeach()
    add_executable(rhodes_note_tests tests/cpp/note_tracker_test.cpp)
    target_link_libraries(rhodes_note_tests PRIVATE rhodes_core)
    rhodes_target_options(rhodes_note_tests)
    foreach(test_name IN ITEMS range_and_validation identified_overlap anonymous_and_zero_velocity
            conflict_atomicity capacity_and_reuse channel_release_and_reset)
        add_test(NAME midi.${test_name} COMMAND rhodes_note_tests ${test_name})
    endforeach()
    add_executable(rhodes_state_tests tests/cpp/instrument_state_test.cpp)
    target_link_libraries(rhodes_state_tests PRIVATE rhodes_core)
    rhodes_target_options(rhodes_state_tests)
    foreach(test_name IN ITEMS codec_golden codec_rejection mailbox_recall mailbox_races mailbox_abort)
        add_test(NAME state.${test_name} COMMAND rhodes_state_tests ${test_name})
    endforeach()
    add_test(NAME cli.help COMMAND rhodes_render --help)
    add_test(NAME benchmark.help COMMAND rhodes_benchmark --help)
    add_test(NAME benchmark.smoke COMMAND rhodes_benchmark --blocks 5 --block-size 64
        --sample-rate 48000 --active-keys 73 --machine ci-smoke)
    add_test(NAME benchmark.json COMMAND ${CMAKE_COMMAND}
        "-DBENCHMARK=$<TARGET_FILE:rhodes_benchmark>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/cpp/benchmark_json.cmake")
    add_test(NAME cli.render COMMAND rhodes_render --output ${CMAKE_CURRENT_BINARY_DIR}/smoke.wav
        --seconds 0.05 --sample-rate 48000 --force)
    add_test(NAME cli.render_output COMMAND ${CMAKE_COMMAND}
        -DPROGRAM=$<TARGET_FILE:rhodes_render> -DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cpp/render_output.cmake)
    add_test(NAME cli.reject_nan COMMAND ${CMAKE_COMMAND}
        -DPROGRAM=$<TARGET_FILE:rhodes_render> -DARG1=--seconds -DARG2=nan
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cpp/expect_cli_failure.cmake)
    add_test(NAME cli.reject_unknown COMMAND ${CMAKE_COMMAND}
        -DPROGRAM=$<TARGET_FILE:rhodes_render> -DARG1=--unknown
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cpp/expect_cli_failure.cmake)
endif()
