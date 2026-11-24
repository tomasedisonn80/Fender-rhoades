cmake_minimum_required(VERSION 3.24)
project(RhodesResearch VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
option(RHODES_WARNINGS_AS_ERRORS "Treat project compiler warnings as errors" ON)
option(RHODES_ENABLE_SANITIZERS "Enable address and undefined-behavior sanitizers" OFF)
option(RHODES_BUILD_VST3 "Build the experimental instrument using an external pinned VST3 SDK" OFF)
option(RHODES_BUILD_VST3_EDITOR "Build the native photo editor (requires the pinned VSTGUI submodule)" ON)
set(RHODES_VST3_SDK_ROOT "" CACHE PATH "Path to the pinned official VST3 SDK checkout")

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
# Keep implementation symbols private when this archive is linked into a plug-in.
# A separately requested shared core keeps its existing public-symbol behavior.
get_target_property(rhodes_core_type rhodes_core TYPE)
if(rhodes_core_type STREQUAL "STATIC_LIBRARY")
    set_target_properties(rhodes_core PROPERTIES
        CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
endif()

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

# Offline contact reference stays separate from the bounded audio core.
add_library(rhodes_contact src/contact_reference.cpp)
target_include_directories(rhodes_contact PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(rhodes_contact PUBLIC cxx_std_20)
set_target_properties(rhodes_contact PROPERTIES CXX_EXTENSIONS OFF)
rhodes_target_options(rhodes_contact)
add_executable(rhodes_contact_reference apps/contact_reference.cpp)
target_link_libraries(rhodes_contact_reference PRIVATE rhodes_contact)
rhodes_target_options(rhodes_contact_reference)

add_executable(rhodes_render apps/render.cpp)
target_link_libraries(rhodes_render PRIVATE rhodes_core)
rhodes_target_options(rhodes_render)

# Offline stationary pickup diagnostic; no new realtime engine/model variant.
add_executable(rhodes_alias_primitive_probe apps/alias_primitive_probe.cpp)
target_link_libraries(rhodes_alias_primitive_probe PRIVATE rhodes_core)
rhodes_target_options(rhodes_alias_primitive_probe)

add_executable(rhodes_benchmark apps/benchmark.cpp)
target_link_libraries(rhodes_benchmark PRIVATE rhodes_core)
target_compile_definitions(rhodes_benchmark PRIVATE RHODES_BUILD_CONFIGURATION="$<CONFIG>")
rhodes_target_options(rhodes_benchmark)

add_executable(rhodes_runtime_benchmark apps/runtime_benchmark.cpp)
target_link_libraries(rhodes_runtime_benchmark PRIVATE rhodes_core)
target_compile_definitions(rhodes_runtime_benchmark PRIVATE RHODES_BUILD_CONFIGURATION="$<CONFIG>")
rhodes_target_options(rhodes_runtime_benchmark)

if(BUILD_TESTING)
    find_package(Threads REQUIRED)
    add_executable(rhodes_plugin_state_tests tests/cpp/plugin_state_test.cpp)
    target_link_libraries(rhodes_plugin_state_tests PRIVATE rhodes_core Threads::Threads)
    target_include_directories(rhodes_plugin_state_tests PRIVATE "${PROJECT_SOURCE_DIR}")
    rhodes_target_options(rhodes_plugin_state_tests)
    foreach(test_name IN ITEMS codec floating_modes transactions exhaustion concurrency)
        add_test(NAME plugin_state.${test_name} COMMAND rhodes_plugin_state_tests ${test_name})
        set_tests_properties(plugin_state.${test_name} PROPERTIES TIMEOUT 60)
    endforeach()
    add_executable(rhodes_photo_geometry_tests tests/cpp/photo_geometry_test.cpp)
    target_link_libraries(rhodes_photo_geometry_tests PRIVATE rhodes_core)
    target_include_directories(rhodes_photo_geometry_tests PRIVATE "${PROJECT_SOURCE_DIR}")
    rhodes_target_options(rhodes_photo_geometry_tests)
    foreach(test_name IN ITEMS keyboard controls transforms invalid_inputs)
        add_test(NAME photo_geometry.${test_name} COMMAND rhodes_photo_geometry_tests ${test_name})
    endforeach()
    add_executable(rhodes_photo_key_highlight_tests tests/cpp/photo_key_highlight_test.cpp)
    target_link_libraries(rhodes_photo_key_highlight_tests PRIVATE rhodes_core)
    target_include_directories(rhodes_photo_key_highlight_tests PRIVATE "${PROJECT_SOURCE_DIR}")
    rhodes_target_options(rhodes_photo_key_highlight_tests)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        # The 73-key contour table is evaluated at compile time. A default-budget
        # MSVC 19.52 build reported C1001; give this test a bounded larger budget
        # while retaining every static assertion and the production flags.
        target_compile_options(rhodes_photo_key_highlight_tests PRIVATE /constexpr:steps1000000)
    endif()
    foreach(test_name IN ITEMS structure black_interiors dense_grid boundaries invalid_indices)
        add_test(NAME photo_key_highlight.${test_name}
            COMMAND rhodes_photo_key_highlight_tests ${test_name})
    endforeach()
    add_executable(rhodes_editor_audition_tests tests/cpp/editor_audition_test.cpp)
    target_link_libraries(rhodes_editor_audition_tests PRIVATE rhodes_core Threads::Threads)
    rhodes_target_options(rhodes_editor_audition_tests)
    foreach(test_name IN ITEMS fifo_and_ring_wrap invalid_commands_and_float_bits
            overflow_releases_final_key_up aborted_transaction_and_late_release
            snapshot_rejection_and_discard counter_exhaustion ordered_producer_consumer
            concurrent_producer_consumer)
        add_test(NAME editor_audition.${test_name} COMMAND rhodes_editor_audition_tests ${test_name})
        set_tests_properties(editor_audition.${test_name} PROPERTIES TIMEOUT 60)
    endforeach()

    add_executable(rhodes_front_panel_effects_tests tests/cpp/front_panel_effects_test.cpp)
    target_link_libraries(rhodes_front_panel_effects_tests PRIVATE rhodes_core)
    rhodes_target_options(rhodes_front_panel_effects_tests)
    foreach(test_name IN ITEMS identity frequency_response tremolo transitions
            prepare_atomicity reset_replay faults allocation_free)
        add_test(NAME front_panel.${test_name} COMMAND rhodes_front_panel_effects_tests ${test_name})
    endforeach()

    add_executable(rhodes_expression_state_tests tests/cpp/expression_state_test.cpp)
    target_link_libraries(rhodes_expression_state_tests PRIVATE rhodes_core)
    rhodes_target_options(rhodes_expression_state_tests)
    foreach(test_name IN ITEMS defaults_and_goldens codec_domains malformed_packets
            model_isolation mailbox_words mailbox_transactions floating_point_modes)
        add_test(NAME expression_state.${test_name}
            COMMAND rhodes_expression_state_tests ${test_name})
    endforeach()

    add_executable(rhodes_control_text_tests tests/cpp/control_text_test.cpp)
    target_link_libraries(rhodes_control_text_tests PRIVATE rhodes_core)
    rhodes_target_options(rhodes_control_text_tests)
    foreach(test_name IN ITEMS oracle bounds formatting roundtrip environments locale)
        add_test(NAME control_text.${test_name} COMMAND rhodes_control_text_tests ${test_name})
    endforeach()

    add_executable(rhodes_event_release_port_tests tests/cpp/event_release_port_test.cpp)
    target_link_libraries(rhodes_event_release_port_tests PRIVATE rhodes_core)
    rhodes_target_options(rhodes_event_release_port_tests)
    add_test(NAME events.release_port COMMAND rhodes_event_release_port_tests)
    add_executable(rhodes_fractional_observation_tests tests/cpp/fractional_observation_test.cpp)
    target_link_libraries(rhodes_fractional_observation_tests PRIVATE rhodes_core)
    rhodes_target_options(rhodes_fractional_observation_tests)
    foreach(test_name IN ITEMS analytic_nodes plan_domains observation_nonmutation damping_regimes)
        add_test(NAME quadrature.${test_name} COMMAND rhodes_fractional_observation_tests ${test_name})
    endforeach()
    add_executable(rhodes_silent_observation_tests tests/cpp/silent_observation_test.cpp)
    target_link_libraries(rhodes_silent_observation_tests PRIVATE rhodes_core)
    rhodes_target_options(rhodes_silent_observation_tests)
    foreach(test_name IN ITEMS plan_validation network lifecycle transactions fp_environment)
        add_test(NAME quadrature.silent_${test_name} COMMAND rhodes_silent_observation_tests ${test_name})
    endforeach()
    set_tests_properties(quadrature.silent_fp_environment PROPERTIES SKIP_RETURN_CODE 77)
    add_executable(rhodes_quadrature_fir_tests tests/cpp/quadrature_fir_test.cpp)
    target_link_libraries(rhodes_quadrature_fir_tests PRIVATE rhodes_core)
    rhodes_target_options(rhodes_quadrature_fir_tests)
    foreach(test_name IN ITEMS qfir_convolution qfir_impulses qfir_delay qfir_rejection qfir_overflow)
        add_test(NAME quadrature.${test_name} COMMAND rhodes_quadrature_fir_tests ${test_name})
    endforeach()
    add_executable(rhodes_quadrature_fir_order_tests tests/cpp/quadrature_fir_order_test.cpp)
    target_link_libraries(rhodes_quadrature_fir_order_tests PRIVATE rhodes_core)
    rhodes_target_options(rhodes_quadrature_fir_order_tests)
    # This oracle specifies separate binary64 product/add roundings. Keep its
    # arithmetic contract local; production compatibility is checked by replay.
    if(CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$"
            AND NOT CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        target_compile_options(rhodes_quadrature_fir_order_tests PRIVATE -ffp-contract=off)
    endif()
    foreach(test_name IN ITEMS qfir_ordered_history qfir_prefix_overflow)
        add_test(NAME quadrature.${test_name} COMMAND rhodes_quadrature_fir_order_tests ${test_name})
    endforeach()
    add_executable(rhodes_quadrature_engine_tests tests/cpp/quadrature_engine_test.cpp)
    target_link_libraries(rhodes_quadrature_engine_tests PRIVATE rhodes_core)
    rhodes_target_options(rhodes_quadrature_engine_tests)
    foreach(test_name IN ITEMS qengine_events qengine_partitions qengine_prepare qengine_faults qengine_allocations)
        add_test(NAME quadrature.${test_name} COMMAND rhodes_quadrature_engine_tests ${test_name})
    endforeach()
    add_executable(rhodes_audio_fir_tests tests/cpp/audio_fir_test.cpp)
    target_link_libraries(rhodes_audio_fir_tests PRIVATE rhodes_core)
    rhodes_target_options(rhodes_audio_fir_tests)
    foreach(test_name IN ITEMS fir_coefficients_dc fir_impulse_phases
            fir_direct_convolution fir_group_delay fir_transactional_rejection fir_overflow_reset)
        add_test(NAME audio.${test_name} COMMAND rhodes_audio_fir_tests ${test_name})
    endforeach()
    add_executable(rhodes_model_state_tests tests/cpp/instrument_model_state_test.cpp)
    target_link_libraries(rhodes_model_state_tests PRIVATE rhodes_core)
    rhodes_target_options(rhodes_model_state_tests)
    foreach(test_name IN ITEMS model1_golden model2_roundtrip model3_roundtrip
            cross_model_rejection invalid_model malformed_models)
        add_test(NAME model_state.${test_name} COMMAND rhodes_model_state_tests ${test_name})
    endforeach()
    add_executable(rhodes_audio_engine_tests tests/cpp/audio_engine_test.cpp)
    target_link_libraries(rhodes_audio_engine_tests PRIVATE rhodes_core)
    rhodes_target_options(rhodes_audio_engine_tests)
    foreach(test_name IN ITEMS high_rate_oracle rate_boundaries persistent_events
            partition_invariance prepare_atomicity reset_flush event_timing fault_substeps)
        add_test(NAME audio.${test_name} COMMAND rhodes_audio_engine_tests ${test_name})
    endforeach()
    add_executable(rhodes_audio_probe apps/audio_probe.cpp)
    target_link_libraries(rhodes_audio_probe PRIVATE rhodes_core)
    rhodes_target_options(rhodes_audio_probe)
    add_executable(rhodes_quadrature_probe apps/audio_probe.cpp)
    target_link_libraries(rhodes_quadrature_probe PRIVATE rhodes_core)
    target_compile_definitions(rhodes_quadrature_probe PRIVATE RHODES_QUADRATURE_PROBE=1)
    rhodes_target_options(rhodes_quadrature_probe)
    add_test(NAME audio.probe_cli COMMAND ${CMAKE_COMMAND}
        "-DPROGRAM=$<TARGET_FILE:rhodes_audio_probe>"
        "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/cpp/audio_probe_cli.cmake")
    add_test(NAME quadrature.cli COMMAND ${CMAKE_COMMAND}
        "-DPROBE=$<TARGET_FILE:rhodes_quadrature_probe>"
        "-DRENDERER=$<TARGET_FILE:rhodes_render>"
        "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/cpp/quadrature_cli.cmake")
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND EXISTS "/dev/full")
        add_test(NAME audio.probe_stdout COMMAND ${CMAKE_COMMAND}
            "-DPROGRAM=$<TARGET_FILE:rhodes_audio_probe>" -DKIND=probe
            "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/cpp/diagnostic_stdout.cmake")
        add_test(NAME quadrature.probe_stdout COMMAND ${CMAKE_COMMAND}
            "-DPROGRAM=$<TARGET_FILE:rhodes_quadrature_probe>" -DKIND=quadrature_probe
            "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/cpp/diagnostic_stdout.cmake")
        add_test(NAME contact.stdout COMMAND ${CMAKE_COMMAND}
            "-DPROGRAM=$<TARGET_FILE:rhodes_contact_reference>" -DKIND=contact
            "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/cpp/diagnostic_stdout.cmake")
        set_tests_properties(audio.probe_stdout quadrature.probe_stdout contact.stdout PROPERTIES TIMEOUT 90)
    endif()
    add_executable(rhodes_contact_tests tests/cpp/contact_reference_test.cpp)
    target_link_libraries(rhodes_contact_tests PRIVATE rhodes_contact)
    rhodes_target_options(rhodes_contact_tests)
    foreach(test_name IN ITEMS conservative_impact damped_impact forced_clamp
            separated_oscillator preload moving_damper relaunch event_order scaling
            invalid_inputs cap_exhaustion grazing_miss grazing_tangent grazing_contact)
        add_test(NAME contact.${test_name} COMMAND rhodes_contact_tests ${test_name})
    endforeach()
    add_test(NAME contact.cli_help COMMAND rhodes_contact_reference --help)
    add_test(NAME contact.cli_json COMMAND ${CMAKE_COMMAND}
        "-DPROGRAM=$<TARGET_FILE:rhodes_contact_reference>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/cpp/contact_cli.cmake")
    add_executable(rhodes_contact_peak_tests tests/cpp/contact_peak_test.cpp)
    target_link_libraries(rhodes_contact_peak_tests PRIVATE rhodes_contact)
    rhodes_target_options(rhodes_contact_peak_tests)
    foreach(test_name IN ITEMS peak_defaults peak_conservative peak_damped
            peak_damper_face peak_actions peak_rollback)
        add_test(NAME contact.${test_name} COMMAND rhodes_contact_peak_tests ${test_name})
    endforeach()
    add_test(NAME contact.peak_cli COMMAND ${CMAKE_COMMAND}
        "-DPROGRAM=$<TARGET_FILE:rhodes_contact_reference>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/cpp/contact_peak_cli.cmake")
    add_executable(rhodes_contact_event_split_tests tests/cpp/contact_event_split_test.cpp)
    target_link_libraries(rhodes_contact_event_split_tests PRIVATE rhodes_contact)
    rhodes_target_options(rhodes_contact_event_split_tests)
    add_test(NAME contact.event_split COMMAND rhodes_contact_event_split_tests)
    add_executable(rhodes_contact_split_peak_tests tests/cpp/contact_split_peak_test.cpp)
    target_link_libraries(rhodes_contact_split_peak_tests PRIVATE rhodes_contact)
    rhodes_target_options(rhodes_contact_split_peak_tests)
    foreach(test_name IN ITEMS split_simultaneous split_close_roots split_peak_rollback)
        add_test(NAME contact.${test_name} COMMAND rhodes_contact_split_peak_tests ${test_name})
    endforeach()
    add_executable(rhodes_contact_grazing_tests tests/cpp/contact_grazing_test.cpp)
    target_link_libraries(rhodes_contact_grazing_tests PRIVATE rhodes_contact)
    rhodes_target_options(rhodes_contact_grazing_tests)
    foreach(test_name IN ITEMS generalized_harmonics envelope_sign partitions relaunch_invalidation)
        add_test(NAME grazing.${test_name} COMMAND rhodes_contact_grazing_tests ${test_name})
    endforeach()
    add_executable(rhodes_contact_grazing_anchor_tests tests/cpp/contact_grazing_anchor_test.cpp)
    target_link_libraries(rhodes_contact_grazing_anchor_tests PRIVATE rhodes_contact)
    rhodes_target_options(rhodes_contact_grazing_anchor_tests)
    foreach(test_name IN ITEMS anchor_steps anchor_geometry anchor_partitions anchor_boundaries anchor_rollback)
        add_test(NAME grazing.${test_name} COMMAND rhodes_contact_grazing_anchor_tests ${test_name})
    endforeach()
    add_executable(rhodes_contact_domain_tests tests/cpp/contact_domain_test.cpp)
    target_link_libraries(rhodes_contact_domain_tests PRIVATE rhodes_contact)
    rhodes_target_options(rhodes_contact_domain_tests)
    foreach(test_name IN ITEMS configuration_bounds settings_bounds state_actions trajectory_guards)
        add_test(NAME contact.${test_name} COMMAND rhodes_contact_domain_tests ${test_name})
    endforeach()
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
    add_executable(rhodes_transition_cache_tests tests/cpp/modal_transition_cache_test.cpp)
    target_link_libraries(rhodes_transition_cache_tests PRIVATE rhodes_core)
    rhodes_target_options(rhodes_transition_cache_tests)
    foreach(test_name IN ITEMS cached_vs_fresh invalid_cache_updates reconfigure_cache redundant_pedal)
        add_test(NAME transition_cache.${test_name} COMMAND rhodes_transition_cache_tests ${test_name})
    endforeach()
    add_executable(rhodes_event_tests tests/cpp/event_processor_test.cpp)
    target_link_libraries(rhodes_event_tests PRIVATE rhodes_core)
    rhodes_target_options(rhodes_event_tests)
    foreach(test_name IN ITEMS scalar_partitions equal_offset_order preflight_atomicity
            invalid_events block_limits sample_fault_reset event_fault_reset)
        add_test(NAME runtime.${test_name} COMMAND rhodes_event_tests ${test_name})
    endforeach()
    add_executable(rhodes_event_model_tests tests/cpp/event_model_test.cpp)
    target_link_libraries(rhodes_event_model_tests PRIVATE rhodes_core)
    rhodes_target_options(rhodes_event_model_tests)
    foreach(test_name IN ITEMS control_order random_partitions transaction_rejection capacity_and_cursor)
        add_test(NAME runtime.${test_name} COMMAND rhodes_event_model_tests ${test_name})
    endforeach()
    add_executable(rhodes_note_tests tests/cpp/note_tracker_test.cpp)
    target_link_libraries(rhodes_note_tests PRIVATE rhodes_core)
    rhodes_target_options(rhodes_note_tests)
    foreach(test_name IN ITEMS range_and_validation identified_overlap anonymous_and_zero_velocity
            conflict_atomicity capacity_and_reuse channel_release_and_reset)
        add_test(NAME midi.${test_name} COMMAND rhodes_note_tests ${test_name})
    endforeach()
    add_executable(rhodes_note_model_tests tests/cpp/note_model_test.cpp)
    target_link_libraries(rhodes_note_model_tests PRIVATE rhodes_core)
    rhodes_target_options(rhodes_note_model_tests)
    foreach(test_name IN ITEMS boundary_model capacity_model random_model)
        add_test(NAME midi.${test_name} COMMAND rhodes_note_model_tests ${test_name})
    endforeach()
    add_executable(rhodes_state_tests tests/cpp/instrument_state_test.cpp)
    target_link_libraries(rhodes_state_tests PRIVATE rhodes_core)
    rhodes_target_options(rhodes_state_tests)
    foreach(test_name IN ITEMS codec_golden codec_rejection mailbox_recall mailbox_races mailbox_abort)
        add_test(NAME state.${test_name} COMMAND rhodes_state_tests ${test_name})
    endforeach()
    add_executable(rhodes_state_property_tests tests/cpp/state_property_test.cpp)
    target_link_libraries(rhodes_state_property_tests PRIVATE rhodes_core)
    rhodes_target_options(rhodes_state_property_tests)
    foreach(test_name IN ITEMS gain_bit_domain pedal_bit_domain header_lengths invalid_recall)
        add_test(NAME state.${test_name} COMMAND rhodes_state_property_tests ${test_name})
    endforeach()
    add_executable(rhodes_state_fp_tests tests/cpp/state_fp_environment_test.cpp)
    target_link_libraries(rhodes_state_fp_tests PRIVATE rhodes_core)
    rhodes_target_options(rhodes_state_fp_tests)
    foreach(test_name IN ITEMS baseline ftz daz ftz_daz)
        add_test(NAME state_fp.${test_name} COMMAND rhodes_state_fp_tests ${test_name})
        set_tests_properties(state_fp.${test_name} PROPERTIES SKIP_RETURN_CODE 77)
    endforeach()
    add_executable(rhodes_control_value_tests tests/cpp/control_value_test.cpp)
    target_link_libraries(rhodes_control_value_tests PRIVATE rhodes_core)
    rhodes_target_options(rhodes_control_value_tests)
    foreach(test_name IN ITEMS domains quantize widen rounding_modes)
        add_test(NAME control.${test_name} COMMAND rhodes_control_value_tests ${test_name})
    endforeach()
    add_executable(rhodes_control_fp_tests tests/cpp/control_fp_environment_test.cpp)
    target_link_libraries(rhodes_control_fp_tests PRIVATE rhodes_core)
    rhodes_target_options(rhodes_control_fp_tests)
    foreach(test_name IN ITEMS baseline ftz daz ftz_daz)
        add_test(NAME control_fp.${test_name} COMMAND rhodes_control_fp_tests ${test_name})
        set_tests_properties(control_fp.${test_name} PROPERTIES SKIP_RETURN_CODE 77)
    endforeach()
    find_package(Threads REQUIRED)
    add_executable(rhodes_state_concurrency_tests tests/cpp/state_concurrency_test.cpp)
    target_link_libraries(rhodes_state_concurrency_tests PRIVATE rhodes_core Threads::Threads)
    rhodes_target_options(rhodes_state_concurrency_tests)
    foreach(test_name IN ITEMS ordered_races concurrent_stress)
        add_test(NAME state.${test_name} COMMAND rhodes_state_concurrency_tests ${test_name})
    endforeach()
    add_test(NAME cli.help COMMAND rhodes_render --help)
    add_test(NAME alias.primitive_help COMMAND rhodes_alias_primitive_probe --help)
    add_test(NAME benchmark.help COMMAND rhodes_benchmark --help)
    add_test(NAME runtime_benchmark.help COMMAND rhodes_runtime_benchmark --help)
    add_test(NAME runtime_benchmark.smoke COMMAND rhodes_runtime_benchmark
        --blocks 5 --warmup-blocks 2 --block-size 64 --sample-rate 48000
        --active-keys 73 --workload retrigger256 --machine ci-smoke)
    add_test(NAME runtime_benchmark.json COMMAND ${CMAKE_COMMAND}
        "-DBENCHMARK=$<TARGET_FILE:rhodes_runtime_benchmark>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/cpp/runtime_benchmark_json.cmake")
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
    add_test(NAME cli.filtered_render COMMAND ${CMAKE_COMMAND}
        -DPROGRAM=$<TARGET_FILE:rhodes_render> -DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cpp/filtered_render.cmake)
    add_test(NAME cli.reject_nan COMMAND ${CMAKE_COMMAND}
        -DPROGRAM=$<TARGET_FILE:rhodes_render> -DARG1=--seconds -DARG2=nan
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cpp/expect_cli_failure.cmake)
    add_test(NAME cli.reject_unknown COMMAND ${CMAKE_COMMAND}
        -DPROGRAM=$<TARGET_FILE:rhodes_render> -DARG1=--unknown
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cpp/expect_cli_failure.cmake)
endif()

if(RHODES_BUILD_VST3)
    include(cmake/Vst3.cmake)
endif()
