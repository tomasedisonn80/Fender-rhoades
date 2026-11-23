# Exercise explicit engine selection and retain the renderer's publication
# contract. This checks synthetic diagnostic output, not instrument fidelity.
cmake_minimum_required(VERSION 3.24)
if(NOT DEFINED PROGRAM OR NOT DEFINED TEST_ROOT)
    message(FATAL_ERROR "PROGRAM and TEST_ROOT are required")
endif()
string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef suffix)
set(work "${TEST_ROOT}/filtered-render-${suffix}")
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
    if("${expected}" STREQUAL "2" AND
            (NOT diagnostic MATCHES "rhodes_render:" OR output MATCHES "Wrote "))
        message(FATAL_ERROR "Expected an error without publication: ${output}${diagnostic}")
    endif()
    set(render_stdout "${output}" PARENT_SCOPE)
    set(render_diagnostic "${diagnostic}" PARENT_SCOPE)
endfunction()

function(assert_wav path)
    # Exactly 480 binary32 samples at 48000 Hz, with the existing 58-byte
    # IEEE-float WAV container. Neither path may trim or extend the duration.
    file(SIZE "${path}" size)
    file(READ "${path}" header OFFSET 0 LIMIT 58 HEX)
    set(expected_header
        "52494646b207000057415645666d7420120000000300010080bb000000ee02000400200000006661637404000000e00100006461746180070000")
    if(NOT size EQUAL 1978 OR NOT header STREQUAL expected_header)
        message(FATAL_ERROR "Unexpected WAV duration or container: ${path}; ${size}; ${header}")
    endif()
endfunction()

function(run_rejected name expected_diagnostic)
    set(destination "${work}/rejected-${name}.wav")
    run_render(2 "${destination}" ${ARGN})
    if(EXISTS "${destination}" OR IS_SYMLINK "${destination}" OR
            NOT render_diagnostic MATCHES "${expected_diagnostic}")
        message(FATAL_ERROR "Incorrect rejection for ${name}: ${render_diagnostic}")
    endif()
endfunction()

execute_process(COMMAND "${PROGRAM}" --help RESULT_VARIABLE help_result
    OUTPUT_VARIABLE help_output ERROR_VARIABLE help_error TIMEOUT 30)
if(NOT "${help_result}" STREQUAL "0" OR
        NOT help_output MATCHES "--audio-path direct\\|filtered" OR
        NOT help_output MATCHES "defaults to direct" OR
        NOT help_output MATCHES "48-frame FIR delay")
    message(FATAL_ERROR "Missing audio path help: ${help_output}${help_error}")
endif()

foreach(audio_path IN ITEMS direct filtered)
    set(destination "${work}/${audio_path} note.wav")
    run_render(0 "${destination}" --audio-path "${audio_path}")
    assert_wav("${destination}")
    if(audio_path STREQUAL "filtered")
        set(expected_metadata "audio-path=filtered, internal rate=192000 Hz, latency=48 frames")
    else()
        set(expected_metadata "audio-path=direct, internal rate=48000 Hz, latency=0 frames")
    endif()
    if(NOT render_stdout MATCHES "${expected_metadata}" OR
            NOT render_stdout MATCHES "Synthetic parameters; no hardware fidelity result")
        message(FATAL_ERROR "Incorrect success metadata: ${render_stdout}")
    endif()
    file(READ "${destination}" payload_${audio_path} OFFSET 58 HEX)
    if(payload_${audio_path} MATCHES "^(00000000|00000080)+$")
        message(FATAL_ERROR "${audio_path} path unexpectedly produced only zero samples")
    endif()
    file(SHA256 "${destination}" hash_${audio_path})
endforeach()
if(payload_direct STREQUAL payload_filtered)
    message(FATAL_ERROR "Explicit direct and filtered paths produced identical payloads")
endif()

set(default_output "${work}/default.wav")
run_render(0 "${default_output}")
assert_wav("${default_output}")
file(SHA256 "${default_output}" default_hash)
if(NOT default_hash STREQUAL hash_direct OR
        NOT render_stdout MATCHES "audio-path=direct, internal rate=48000 Hz, latency=0 frames")
    message(FATAL_ERROR "Default audio path no longer matches explicit direct rendering")
endif()

run_rejected(unknown_path "Invalid value for --audio-path" --audio-path unknown)
run_rejected(missing_path "Missing value for --audio-path" --audio-path)
foreach(audio_path IN ITEMS direct filtered)
    run_rejected("${audio_path}-unknown-option" "Unknown option"
        --audio-path "${audio_path}" --unknown)
    run_rejected("${audio_path}-bad-number" "Invalid value for --gain"
        --audio-path "${audio_path}" --gain invalid)
    run_rejected("${audio_path}-nan-seconds" "outside the documented ranges"
        --audio-path "${audio_path}" --seconds nan)
    run_rejected("${audio_path}-infinite-velocity" "outside the documented ranges"
        --audio-path "${audio_path}" --velocity inf)
    run_rejected("${audio_path}-infinite-gain" "outside the documented ranges"
        --audio-path "${audio_path}" --gain -inf)
    run_rejected("${audio_path}-rate" "outside the documented ranges"
        --audio-path "${audio_path}" --sample-rate 31999)
    run_rejected("${audio_path}-key" "outside the documented ranges"
        --audio-path "${audio_path}" --key 73)

    set(destination "${work}/${audio_path} note.wav")
    run_render(2 "${destination}" --audio-path "${audio_path}" --gain 0)
    file(SHA256 "${destination}" refused_hash)
    if(NOT refused_hash STREQUAL hash_${audio_path})
        message(FATAL_ERROR "${audio_path} path changed an existing file without --force")
    endif()
    run_render(2 "${destination}" --audio-path "${audio_path}" --gain nan --force)
    file(SHA256 "${destination}" invalid_hash)
    if(NOT invalid_hash STREQUAL hash_${audio_path})
        message(FATAL_ERROR "${audio_path} path changed an existing file after invalid input")
    endif()
    run_render(0 "${destination}" --audio-path "${audio_path}" --gain 0 --force)
    assert_wav("${destination}")
    file(READ "${destination}" silent_payload OFFSET 58 HEX)
    if(NOT silent_payload MATCHES "^(00000000|00000080)+$")
        message(FATAL_ERROR "${audio_path} path did not publish the complete forced replacement")
    endif()

    set(missing_parent "${work}/${audio_path}-missing-parent")
    run_render(2 "${missing_parent}/note.wav" --audio-path "${audio_path}")
    if(EXISTS "${missing_parent}")
        message(FATAL_ERROR "Renderer unexpectedly created the missing output parent")
    endif()
endforeach()

set(marker "Preserve this existing entry and its contents.\n")
set(directory "${work}/directory.wav")
set(file_parent "${work}/regular-file-parent")
file(MAKE_DIRECTORY "${directory}")
file(WRITE "${directory}/marker" "${marker}")
file(WRITE "${file_parent}" "${marker}")
foreach(audio_path IN ITEMS direct filtered)
    run_render(2 "${directory}" --audio-path "${audio_path}")
    run_render(2 "${directory}" --audio-path "${audio_path}" --force)
    run_render(2 "${file_parent}/note.wav" --audio-path "${audio_path}")
endforeach()
file(READ "${directory}/marker" directory_marker)
file(READ "${file_parent}" parent_marker)
if(NOT directory_marker STREQUAL marker OR NOT parent_marker STREQUAL marker)
    message(FATAL_ERROR "Output path rejection changed existing entries")
endif()

foreach(kind IN ITEMS live dangling)
    set(target "${work}/${kind}-target.wav")
    set(link "${work}/${kind}-link.wav")
    if(kind STREQUAL "live")
        file(WRITE "${target}" "${marker}")
    endif()
    file(CREATE_LINK "${target}" "${link}" SYMBOLIC RESULT link_result)
    if(NOT "${link_result}" STREQUAL "0")
        message(STATUS "SKIP ${kind} symlink rejection: ${link_result}")
        continue()
    endif()
    # Compare the host's actual link representation, which may use native
    # separators, rather than the spelling passed to CREATE_LINK.
    file(READ_SYMLINK "${link}" original_target)
    foreach(audio_path IN ITEMS direct filtered)
        run_render(2 "${link}" --audio-path "${audio_path}")
        run_render(2 "${link}" --audio-path "${audio_path}" --force)
    endforeach()
    if(NOT IS_SYMLINK "${link}")
        message(FATAL_ERROR "Renderer replaced the ${kind} symlink")
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
endforeach()

# Check directory permissions only when the current host/account enforces them.
# A privileged account may write despite these mode bits; a read-only file in a
# writable directory may still be atomically replaced, which is valid behavior.
set(read_only "${work}/read-only")
file(MAKE_DIRECTORY "${read_only}")
file(WRITE "${read_only}/marker" "${marker}")
file(CHMOD "${read_only}" PERMISSIONS OWNER_READ OWNER_EXECUTE
    GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
execute_process(COMMAND "${CMAKE_COMMAND}" -E touch "${read_only}/permission-probe"
    RESULT_VARIABLE probe_result OUTPUT_QUIET ERROR_QUIET TIMEOUT 30)
if("${probe_result}" STREQUAL "0")
    message(STATUS "SKIP read-only-directory rejection: current account can write there")
else()
    foreach(audio_path IN ITEMS direct filtered)
        run_render(2 "${read_only}/${audio_path}.wav" --audio-path "${audio_path}")
        if(EXISTS "${read_only}/${audio_path}.wav")
            message(FATAL_ERROR "Renderer published into the read-only output directory")
        endif()
    endforeach()
    message(STATUS "PASS read-only-directory rejection for both audio paths")
endif()
file(CHMOD "${read_only}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
    GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
file(READ "${read_only}/marker" read_only_marker)
if(NOT read_only_marker STREQUAL marker)
    message(FATAL_ERROR "Read-only output rejection changed existing contents")
endif()

file(GLOB_RECURSE leftovers LIST_DIRECTORIES true "${work}/*")
list(FILTER leftovers INCLUDE REGEX "/[.]rhodes-render-")
if(leftovers)
    message(FATAL_ERROR "Temporary renderer outputs were not cleaned up: ${leftovers}")
endif()
file(REMOVE_RECURSE "${work}")
message(STATUS "PASS direct/default/filtered WAVs, timing metadata, invalid inputs, and publication safety")
