cmake_minimum_required(VERSION 3.24)

if(NOT DEFINED BENCHMARK OR NOT IS_ABSOLUTE "${BENCHMARK}"
        OR NOT EXISTS "${BENCHMARK}" OR IS_DIRECTORY "${BENCHMARK}")
    message(FATAL_ERROR "BENCHMARK must name an existing absolute executable path")
endif()

# Exercise JSON escaping without assuming Windows UTF-8 argv support. NUL
# cannot be passed in an argv element; the other characters remain ASCII.
string(ASCII 31 control_character)
set(machine_label "cmake \"quote\" \\backslash\ttab\nnewline${control_character}control")
execute_process(
    COMMAND "${BENCHMARK}" --blocks 1 --block-size 64 --sample-rate 32000
        --active-keys 0 --machine "${machine_label}"
    RESULT_VARIABLE result OUTPUT_VARIABLE benchmark_json ERROR_VARIABLE diagnostic
    TIMEOUT 30)
if(NOT "${result}" STREQUAL "0" OR NOT "${diagnostic}" STREQUAL "")
    message(FATAL_ERROR "Expected successful benchmark; received ${result}: ${diagnostic}")
endif()

# Every parse checks ERROR_VARIABLE: missing fields, wrong containers, and
# malformed JSON must fail the test rather than becoming comparison operands.
function(json_read output expected_type)
    string(JSON actual_type ERROR_VARIABLE json_error TYPE "${benchmark_json}" ${ARGN})
    if(NOT "${json_error}" STREQUAL "NOTFOUND")
        message(FATAL_ERROR "Invalid JSON at [${ARGN}]: ${json_error}")
    endif()
    if(NOT "${actual_type}" STREQUAL "${expected_type}")
        message(FATAL_ERROR
            "JSON [${ARGN}] must be ${expected_type}; received ${actual_type}")
    endif()
    string(JSON value ERROR_VARIABLE json_error GET "${benchmark_json}" ${ARGN})
    if(NOT "${json_error}" STREQUAL "NOTFOUND")
        message(FATAL_ERROR "Cannot read JSON [${ARGN}]: ${json_error}")
    endif()
    set("${output}" "${value}" PARENT_SCOPE)
endfunction()

function(json_expect expected_type expected_value)
    json_read(actual "${expected_type}" ${ARGN})
    if("${expected_type}" STREQUAL "NUMBER")
        if(NOT actual EQUAL expected_value)
            message(FATAL_ERROR
                "JSON [${ARGN}] must equal ${expected_value}; received ${actual}")
        endif()
    elseif(NOT "${actual}" STREQUAL "${expected_value}")
        message(FATAL_ERROR "JSON [${ARGN}] does not match its expected value")
    endif()
endfunction()

function(json_length output)
    string(JSON length ERROR_VARIABLE json_error LENGTH "${benchmark_json}" ${ARGN})
    if(NOT "${json_error}" STREQUAL "NOTFOUND")
        message(FATAL_ERROR "Cannot read JSON length [${ARGN}]: ${json_error}")
    endif()
    set("${output}" "${length}" PARENT_SCOPE)
endfunction()

json_read(root OBJECT)
json_expect(NUMBER 1 schema_version)
json_expect(STRING "synthetic_research_kernel" benchmark)
json_expect(STRING "${machine_label}" machine)
json_read(platform STRING platform)
if("${platform}" STREQUAL "")
    message(FATAL_ERROR "Platform label must not be empty")
endif()
json_read(build OBJECT build)
json_read(configuration STRING build configuration)
foreach(field IN ITEMS compiler compiler_version)
    json_read(label STRING build "${field}")
    if("${label}" STREQUAL "")
        message(FATAL_ERROR "Build ${field} label must not be empty")
    endif()
endforeach()
json_read(cplusplus NUMBER build cplusplus)
if(cplusplus LESS 202002)
    message(FATAL_ERROR "Benchmark must report C++20 or later")
endif()
json_read(ndebug BOOLEAN build ndebug)

json_expect(NUMBER 32000 sample_rate_hz)
json_expect(NUMBER 64 block_size)
json_expect(NUMBER 1 blocks)
json_expect(NUMBER 32 warmup_blocks)
json_read(workload OBJECT workload)
json_expect(NUMBER 0 workload active_keys)
json_expect(NUMBER 73 workload fixed_key_slots)
json_expect(NUMBER 3 workload modes_per_key)
json_read(key_indices ARRAY workload key_indices)
json_length(key_count workload key_indices)
if(NOT key_count EQUAL 0)
    message(FATAL_ERROR "Zero active keys must produce an empty key_indices array")
endif()
json_expect(NUMBER 0.7 workload impulse_intensity)
json_expect(NUMBER 250 workload retrigger_interval_blocks)
json_expect(BOOLEAN OFF workload event_handling_timed)
json_expect(STRING
    "held synthetic impulses, retriggered approximately every 0.5 seconds"
    workload excitation)

json_read(timing OBJECT timing)
json_expect(STRING "std::chrono::steady_clock" timing clock)
json_expect(STRING "nearest rank" timing percentile_method)
json_read(mean NUMBER timing mean_callback_us)
if(mean LESS 0)
    message(FATAL_ERROR "Callback duration must not be negative")
endif()
foreach(field IN ITEMS p50_callback_us p99_callback_us max_callback_us)
    json_expect(NUMBER "${mean}" timing "${field}")
endforeach()
# 64 samples at 32000 Hz gives an exact 2000 us deadline. This checks the
# accounting, never whether the machine meets that deadline.
json_expect(NUMBER 2000 timing deadline_us)
if(mean GREATER 2000)
    set(expected_misses 1)
else()
    set(expected_misses 0)
endif()
json_expect(NUMBER "${expected_misses}" timing deadline_misses)
json_expect(NUMBER 0 checksum_sum_squares)
json_expect(NUMBER 0 output_peak_abs_synthetic_volts)
json_read(limits ARRAY limits)
json_length(limit_count limits)
if(limit_count LESS 1)
    message(FATAL_ERROR "Benchmark must include its measurement limitations")
endif()
math(EXPR final_limit "${limit_count} - 1")
foreach(index RANGE 0 ${final_limit})
    json_read(limit STRING limits "${index}")
    if("${limit}" STREQUAL "")
        message(FATAL_ERROR "Benchmark limitation ${index} must not be empty")
    endif()
endforeach()

execute_process(COMMAND "${BENCHMARK}" --blocks 0
    RESULT_VARIABLE rejected_result OUTPUT_VARIABLE rejected_output
    ERROR_VARIABLE rejected_diagnostic TIMEOUT 30)
if(NOT "${rejected_result}" STREQUAL "2"
        OR NOT "${rejected_output}" STREQUAL ""
        OR NOT rejected_diagnostic MATCHES "rhodes_benchmark:")
    message(FATAL_ERROR
        "Zero blocks must exit 2 with empty stdout and a benchmark diagnostic; "
        "received ${rejected_result}: ${rejected_diagnostic}")
endif()

message(STATUS "Benchmark JSON and zero-block rejection checks passed")
