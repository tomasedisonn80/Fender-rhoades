# Exercise file publication and the exact diagnostic WAV container without a
# Python runtime or an external audio reader. Keep failed fixtures for inspection.
if(NOT DEFINED PROGRAM OR NOT DEFINED TEST_ROOT)
    message(FATAL_ERROR "PROGRAM and TEST_ROOT are required")
endif()
string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef suffix)
set(work "${TEST_ROOT}/render-output-${suffix}")
file(MAKE_DIRECTORY "${work}")

function(run_render expected destination)
    execute_process(COMMAND "${PROGRAM}" --output "${destination}"
            --seconds 0.01 --sample-rate 48000 ${ARGN}
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE diagnostic
        TIMEOUT 30)
    if(NOT "${result}" STREQUAL "${expected}")
        message(FATAL_ERROR
            "Expected renderer exit ${expected}; received ${result}\n"
            "Destination: ${destination}\n${output}${diagnostic}")
    endif()
    if("${expected}" STREQUAL "2" AND NOT diagnostic MATCHES "rhodes_render:")
        message(FATAL_ERROR "Expected a renderer validation diagnostic: ${diagnostic}")
    endif()
endfunction()

function(assert_wav path)
    # 480 binary32 samples plus 58 bytes: RIFF, WAVEFORMATEX (tag 3,
    # mono/48000 Hz/192000 bytes per second/4-byte alignment/32 bits/cbSize=0),
    # fact=480, and data=1920. All multibyte fields must be little-endian.
    file(SIZE "${path}" size)
    if(NOT size EQUAL 1978)
        message(FATAL_ERROR "Unexpected WAV length ${size}; expected 1978: ${path}")
    endif()
    file(READ "${path}" header OFFSET 0 LIMIT 58 HEX)
    set(expected_header
        "52494646b207000057415645666d7420120000000300010080bb000000ee02000400200000006661637404000000e00100006461746180070000")
    if(NOT header STREQUAL expected_header)
        message(FATAL_ERROR "Unexpected IEEE-float WAV header: ${header}")
    endif()
endfunction()

set(output "${work}/ordinary note.wav")
run_render(0 "${output}")
assert_wav("${output}")
file(SHA256 "${output}" original_hash)

# A refused render must retain every original byte. A forced regular-file
# replacement must contain a complete new render, with zero samples at gain=0.
run_render(2 "${output}" --gain 0)
file(SHA256 "${output}" refused_hash)
if(NOT refused_hash STREQUAL original_hash)
    message(FATAL_ERROR "Refused overwrite changed the existing WAV")
endif()
run_render(0 "${output}" --gain 0 --force)
assert_wav("${output}")
file(SHA256 "${output}" replaced_hash)
file(READ "${output}" replaced_samples OFFSET 58 HEX)
if(replaced_hash STREQUAL original_hash OR
        NOT replaced_samples MATCHES "^(00000000|00000080)+$")
    message(FATAL_ERROR "Forced replacement did not publish the complete new render")
endif()

set(missing_parent "${work}/missing-parent")
run_render(2 "${missing_parent}/note.wav")
if(EXISTS "${missing_parent}")
    message(FATAL_ERROR "Renderer unexpectedly created the missing parent")
endif()

set(directory "${work}/directory.wav")
set(marker "Keep this directory and its contents unchanged.\n")
file(MAKE_DIRECTORY "${directory}")
file(WRITE "${directory}/marker" "${marker}")
run_render(2 "${directory}")
run_render(2 "${directory}" --force)
file(READ "${directory}/marker" observed_marker)
if(NOT IS_DIRECTORY "${directory}" OR NOT observed_marker STREQUAL marker)
    message(FATAL_ERROR "Directory rejection changed the existing directory")
endif()

foreach(kind IN ITEMS dangling live)
    set(target "${work}/${kind}-target.wav")
    set(link "${work}/${kind}-link.wav")
    if(kind STREQUAL "live")
        file(WRITE "${target}" "${marker}")
    endif()
    file(CREATE_LINK "${target}" "${link}" SYMBOLIC RESULT link_result)
    if(NOT "${link_result}" STREQUAL "0")
        message(STATUS "SKIP ${kind} final-symlink checks: host cannot create symlink (${link_result})")
        continue()
    endif()
    # Compare the host's actual link representation, which may use native
    # separators, rather than the spelling passed to CREATE_LINK.
    file(READ_SYMLINK "${link}" original_target)
    run_render(2 "${link}")
    run_render(2 "${link}" --force)
    if(NOT IS_SYMLINK "${link}")
        message(FATAL_ERROR "Renderer replaced the ${kind} final symlink")
    endif()
    file(READ_SYMLINK "${link}" observed_target)
    if(NOT observed_target STREQUAL original_target)
        message(FATAL_ERROR "Renderer changed the ${kind} symlink destination")
    endif()
    if(kind STREQUAL "live")
        file(READ "${target}" observed_marker)
        if(NOT observed_marker STREQUAL marker)
            message(FATAL_ERROR "Renderer changed the live symlink target")
        endif()
    elseif(EXISTS "${target}" OR IS_SYMLINK "${target}")
        message(FATAL_ERROR "Renderer created the dangling symlink target")
    endif()
    message(STATUS "PASS ${kind} final-symlink rejection, with and without --force")
endforeach()

file(GLOB leftovers LIST_DIRECTORIES true "${work}/.rhodes-render-*")
if(leftovers)
    message(FATAL_ERROR "Temporary renderer outputs were not cleaned up: ${leftovers}")
endif()
file(REMOVE_RECURSE "${work}")
message(STATUS "PASS WAV format/size, preserved bytes, regular replacement, invalid paths, and cleanup")
