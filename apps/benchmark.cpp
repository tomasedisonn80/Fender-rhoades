#include "rhodes/research_engine.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <locale>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifndef RHODES_BUILD_CONFIGURATION
#define RHODES_BUILD_CONFIGURATION "unspecified"
#endif

namespace {
constexpr std::size_t warmup_blocks = 32;
constexpr double impulse_intensity = 0.7;

struct Options {
    std::size_t blocks = 1000;
    std::size_t block_size = 128;
    std::uint32_t sample_rate = 48000;
    std::size_t active_keys = rhodes::key_count;
    std::string machine = "unknown";
};

void usage() {
    std::cout <<
        "rhodes_benchmark: synthetic research-kernel callback timings\n"
        "Usage: rhodes_benchmark [--blocks 1..100000] [--block-size 64|128|256|512]\n"
        "                        [--sample-rate 32000..192000] [--active-keys 0..73]\n"
        "                        [--machine NONEMPTY_UTF8_LABEL]\n"
        "Defaults: 1000 blocks, 128 samples, 48000 Hz, 73 active keys, unknown machine.\n"
        "Prints JSON. Measures process() only after 32 warmup blocks.\n"
        "All 73 key slots run even with zero active keys. Synthetic constants only;\n"
        "no hardware fidelity, CPU utilization, or real-time scheduling guarantee.\n";
}

template<typename T>
T number(std::string_view text, std::string_view flag) {
    T result{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
        throw std::runtime_error("Invalid value for " + std::string(flag));
    }
    return result;
}

bool valid_utf8(std::string_view text) {
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto byte = static_cast<unsigned char>(text[i]);
        if (byte < 0x80U) {
            continue;
        }
        std::size_t remaining = 0;
        std::uint32_t codepoint = 0;
        std::uint32_t minimum = 0;
        if ((byte & 0xe0U) == 0xc0U) {
            remaining = 1;
            codepoint = byte & 0x1fU;
            minimum = 0x80U;
        } else if ((byte & 0xf0U) == 0xe0U) {
            remaining = 2;
            codepoint = byte & 0x0fU;
            minimum = 0x800U;
        } else if ((byte & 0xf8U) == 0xf0U) {
            remaining = 3;
            codepoint = byte & 0x07U;
            minimum = 0x10000U;
        } else {
            return false;
        }
        if (remaining > text.size() - i - 1) {
            return false;
        }
        for (std::size_t j = 0; j < remaining; ++j) {
            const auto next = static_cast<unsigned char>(text[++i]);
            if ((next & 0xc0U) != 0x80U) {
                return false;
            }
            codepoint = (codepoint << 6U) | (next & 0x3fU);
        }
        if (codepoint < minimum || codepoint > 0x10ffffU ||
            (codepoint >= 0xd800U && codepoint <= 0xdfffU)) {
            return false;
        }
    }
    return true;
}

void json_string(std::ostream& output, std::string_view text) {
    constexpr std::string_view hex = "0123456789abcdef";
    output << '"';
    for (const char character : text) {
        const auto byte = static_cast<unsigned char>(character);
        if (character == '"' || character == '\\') {
            output << '\\' << character;
        } else if (byte < 0x20U) {
            output << "\\u00" << hex[byte >> 4U] << hex[byte & 0x0fU];
        } else {
            output << character;
        }
    }
    output << '"';
}

std::string_view compiler_name() {
#if defined(__clang__)
    return "Clang";
#elif defined(_MSC_VER)
    return "MSVC";
#elif defined(__GNUC__)
    return "GCC";
#else
    return "unknown";
#endif
}

std::string compiler_version() {
#if defined(__clang__)
    return __clang_version__;
#elif defined(_MSC_VER)
    return std::to_string(_MSC_FULL_VER);
#elif defined(__GNUC__)
    return __VERSION__;
#else
    return "unknown";
#endif
}

std::string_view platform_name() {
#if defined(_WIN32)
    return "Windows";
#elif defined(__APPLE__)
    return "Apple";
#elif defined(__linux__)
    return "Linux";
#else
    return "unknown";
#endif
}

void strike(rhodes::ResearchEngine& engine, std::span<const std::size_t> keys) {
    for (const auto key : keys) {
        if (!engine.note_on(key, impulse_intensity)) {
            throw std::runtime_error("Research engine rejected a synthetic strike");
        }
    }
}

double nearest_rank(const std::vector<double>& sorted, std::size_t percentile) {
    return sorted[(sorted.size() * percentile + 99) / 100 - 1];
}
} // namespace

int main(int argc, char** argv) {
    try {
        Options options;
        for (int i = 1; i < argc; ++i) {
            const std::string_view flag{argv[i]};
            if (flag == "--help") {
                usage();
                return 0;
            }
            if (flag != "--blocks" && flag != "--block-size" &&
                flag != "--sample-rate" && flag != "--active-keys" &&
                flag != "--machine") {
                throw std::runtime_error("Unknown option: " + std::string(flag));
            }
            if (++i >= argc) {
                throw std::runtime_error("Missing value for " + std::string(flag));
            }
            const std::string_view value{argv[i]};
            if (flag == "--blocks") { options.blocks = number<std::size_t>(value, flag); }
            else if (flag == "--block-size") { options.block_size = number<std::size_t>(value, flag); }
            else if (flag == "--sample-rate") { options.sample_rate = number<std::uint32_t>(value, flag); }
            else if (flag == "--active-keys") { options.active_keys = number<std::size_t>(value, flag); }
            else if (flag == "--machine") { options.machine = value; }
        }
        if (options.blocks == 0 || options.blocks > 100000 ||
            (options.block_size != 64 && options.block_size != 128 &&
             options.block_size != 256 && options.block_size != 512) ||
            options.sample_rate < rhodes::minimum_sample_rate_hz ||
            options.sample_rate > rhodes::maximum_sample_rate_hz ||
            options.active_keys > rhodes::key_count || options.machine.empty() ||
            !valid_utf8(options.machine)) {
            throw std::runtime_error("Arguments are outside the documented ranges; see --help");
        }

        // Allocate all sample and timing storage before warmup and measurements.
        std::vector<double> output(options.block_size);
        std::vector<double> timings_us(options.blocks);
        std::array<std::size_t, rhodes::key_count> key_indices{};
        for (std::size_t i = 0; i < options.active_keys; ++i) {
            key_indices[i] = options.active_keys == 1 ? rhodes::key_count / 2 :
                i * (rhodes::key_count - 1) / (options.active_keys - 1);
        }
        const std::span<const std::size_t> keys{key_indices.data(), options.active_keys};
        const auto retrigger_interval =
            (static_cast<std::size_t>(options.sample_rate) + 2 * options.block_size - 1) /
            (2 * options.block_size); // Round half a second up to a block boundary.
        rhodes::ResearchEngine engine;
        if (!engine.prepare(static_cast<double>(options.sample_rate))) {
            throw std::runtime_error("Research engine rejected the requested sample rate");
        }
        for (std::size_t i = 0; i < warmup_blocks; ++i) {
            if (i % retrigger_interval == 0) {
                strike(engine, keys);
            }
            if (!engine.process(output)) {
                throw std::runtime_error("Numerical failure during benchmark warmup");
            }
        }
        // Begin each recorded run from the same deterministic mechanical state.
        engine.reset();
        const double deadline_us = 1.0e6 * static_cast<double>(options.block_size) /
            static_cast<double>(options.sample_rate);
        double checksum = 0.0;
        double output_peak = 0.0;
        double total_us = 0.0;
        std::size_t deadline_misses = 0;
        for (std::size_t i = 0; i < options.blocks; ++i) {
            if (i % retrigger_interval == 0) {
                strike(engine, keys);
            }
            const auto start = std::chrono::steady_clock::now();
            const bool success = engine.process(output);
            const auto end = std::chrono::steady_clock::now();
            if (!success) {
                throw std::runtime_error("Numerical failure during benchmark measurement");
            }
            const double elapsed_us =
                std::chrono::duration<double, std::micro>(end - start).count();
            timings_us[i] = elapsed_us;
            total_us += elapsed_us;
            if (elapsed_us > deadline_us) {
                ++deadline_misses;
            }
            // Observable output consumption prevents dead-code elimination.
            // Its cost, like strike scheduling, is outside the timed callback.
            for (const double sample : output) {
                checksum += sample * sample;
                output_peak = std::max(output_peak, std::abs(sample));
            }
        }
        if (!std::isfinite(checksum) || !std::isfinite(total_us) ||
            (options.active_keys != 0 && output_peak == 0.0)) {
            throw std::runtime_error("Nonfinite result or unexpectedly silent active workload");
        }
        std::sort(timings_us.begin(), timings_us.end());

        std::cout.imbue(std::locale::classic());
        std::cout << std::setprecision(17) << "{\n  \"schema_version\": 1,\n  \"benchmark\": "
            "\"synthetic_research_kernel\",\n  \"machine\": ";
        json_string(std::cout, options.machine);
        std::cout << ",\n  \"platform\": ";
        json_string(std::cout, platform_name());
        std::cout << ",\n  \"build\": {\"configuration\": ";
        json_string(std::cout, RHODES_BUILD_CONFIGURATION);
        std::cout << ", \"compiler\": ";
        json_string(std::cout, compiler_name());
        std::cout << ", \"compiler_version\": ";
        json_string(std::cout, compiler_version());
#if defined(_MSVC_LANG)
        std::cout << ", \"cplusplus\": " << _MSVC_LANG;
#else
        std::cout << ", \"cplusplus\": " << __cplusplus;
#endif
#if defined(NDEBUG)
        std::cout << ", \"ndebug\": true},\n";
#else
        std::cout << ", \"ndebug\": false},\n";
#endif
        std::cout << "  \"sample_rate_hz\": " << options.sample_rate
            << ",\n  \"block_size\": " << options.block_size
            << ",\n  \"blocks\": " << options.blocks
            << ",\n  \"warmup_blocks\": " << warmup_blocks
            << ",\n  \"workload\": {\n    \"active_keys\": " << options.active_keys
            << ",\n    \"fixed_key_slots\": " << rhodes::key_count
            << ",\n    \"modes_per_key\": " << rhodes::modes_per_key
            << ",\n    \"key_indices\": [";
        for (std::size_t i = 0; i < keys.size(); ++i) {
            std::cout << (i == 0 ? "" : ", ") << keys[i];
        }
        std::cout << "],\n    \"impulse_intensity\": " << impulse_intensity
            << ",\n    \"retrigger_interval_blocks\": " << retrigger_interval
            << ",\n    \"event_handling_timed\": false,\n"
            "    \"excitation\": \"held synthetic impulses, retriggered approximately every 0.5 seconds\"\n"
            "  },\n  \"timing\": {\n"
            "    \"clock\": \"std::chrono::steady_clock\",\n"
            "    \"percentile_method\": \"nearest rank\",\n"
            "    \"mean_callback_us\": " << total_us / static_cast<double>(options.blocks)
            << ",\n    \"p50_callback_us\": " << nearest_rank(timings_us, 50)
            << ",\n    \"p99_callback_us\": " << nearest_rank(timings_us, 99)
            << ",\n    \"max_callback_us\": " << timings_us.back()
            << ",\n    \"deadline_us\": " << deadline_us
            << ",\n    \"deadline_misses\": " << deadline_misses
            << "\n  },\n  \"checksum_sum_squares\": " << checksum
            << ",\n  \"output_peak_abs_synthetic_volts\": " << output_peak
            << ",\n  \"limits\": [\n"
            "    \"Synthetic development constants; no specimen measurements or fidelity result.\",\n"
            "    \"All 73 key slots and pickup branches run even when active_keys is zero.\",\n"
            "    \"Only process() is timed; preparation, strikes, checksums, sorting and JSON are excluded.\",\n"
            "    \"Wall-clock observations include scheduling and clock overhead; no overhead subtraction.\",\n"
            "    \"No host, audio device, thread-priority or affinity control; no CPU utilization claim.\",\n"
            "    \"Observed maxima and deadline misses are not worst-case bounds or real-time guarantees.\",\n"
            "    \"No contact, coupled structure, complex pickup circuit, antialiasing or plugin-host workload.\"\n"
            "  ]\n}\n";
        if (!std::cout) {
            throw std::runtime_error("Unable to write benchmark JSON");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "rhodes_benchmark: " << error.what() << '\n';
        return 2;
    }
}
