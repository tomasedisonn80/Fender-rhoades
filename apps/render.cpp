#include "rhodes/audio_engine.hpp"
#include "rhodes/quadrature_engine.hpp"
#include "rhodes/research_engine.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
enum class AudioPath { direct, filtered, quadrature };

struct Options {
    std::filesystem::path output = "research-note.wav";
    double seconds = 3.0;
    std::uint32_t sample_rate = 48000;
    std::size_t key = 40;
    double velocity = 0.7;
    double gain = 8.0;
    AudioPath audio_path = AudioPath::direct;
    bool force = false;
};

void usage() {
    std::cout <<
        "rhodes_render: synthetic physical-model research fixture, not a validated Rhodes\n"
        "Usage: rhodes_render [--output PATH] [--seconds 0.01..60]\n"
        "                     [--sample-rate 32000..192000] [--key 0..72]\n"
        "                     [--velocity 0..1] [--gain 0..100] [--force]\n"
        "                     [--audio-path direct|filtered|quadrature]\n"
        "Writes mono 32-bit IEEE-float WAV. Key 0 maps to synthetic MIDI 28.\n"
        "Existing entries are refused; --force replaces regular files only, never symlinks.\n"
        "The note releases after min(1 second, 65% of duration).\n"
        "Audio path defaults to direct (0 latency frames). Filtered retains its causal\n"
        "48-frame FIR delay within the requested duration; no trim or tail extension.\n"
        "Quadrature uses 6 fractional observations per host cell and retains 48 frames of delay.\n"
        "One output unit represents one modeled volt times the explicit monitoring gain.\n"
        "No normalization, clipping, measured parameters, or recorded samples are used.\n";
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

bool replace_existing_output(const std::filesystem::path& path, bool force) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (status.type() == std::filesystem::file_type::not_found &&
        (!error || error == std::errc::no_such_file_or_directory)) {
        return false;
    }
    if (error) {
        throw std::filesystem::filesystem_error("Unable to inspect output path", path, error);
    }
    if (std::filesystem::is_symlink(status)) {
        throw std::runtime_error("Output path is a symlink; choose a regular file path");
    }
    if (!force) {
        throw std::runtime_error("Output already exists; choose another path or pass --force");
    }
    if (!std::filesystem::is_regular_file(status)) {
        throw std::runtime_error("--force can replace only an existing regular file");
    }
    return true;
}

class StagedOutput {
public:
    explicit StagedOutput(const std::filesystem::path& destination) {
        const auto parent = destination.has_parent_path() ? destination.parent_path()
                                                        : std::filesystem::path{"."};
        std::random_device random;
        const auto prefix = ".rhodes-render-" + std::to_string(random()) + "-" +
                            std::to_string(random()) + "-";
        for (unsigned int attempt = 0; attempt < 128; ++attempt) {
            auto directory = parent / (prefix + std::to_string(attempt) + ".tmp");
            auto file = directory / "output.wav";
            std::error_code error;
            // C++20 has no exclusive ofstream open. Atomically reserve a sibling
            // directory, then write only inside that newly created location.
            if (std::filesystem::create_directory(directory, error)) {
                directory_ = std::move(directory);
                file_ = std::move(file);
                return;
            }
            if (error && error != std::errc::file_exists) {
                throw std::filesystem::filesystem_error(
                    "Unable to reserve temporary output directory", directory, error);
            }
        }
        throw std::runtime_error("Unable to reserve an unused temporary output directory");
    }

    StagedOutput(const StagedOutput&) = delete;
    StagedOutput& operator=(const StagedOutput&) = delete;

    ~StagedOutput() noexcept {
        std::error_code file_error;
        std::error_code directory_error;
        std::filesystem::remove(file_, file_error);
        std::filesystem::remove(directory_, directory_error);
        if (file_error || directory_error) {
            // Cleanup is best effort, including after publication. A complete
            // published output must not be reported as a failed render.
            try {
                std::cerr << "rhodes_render: warning: temporary output cleanup failed at "
                          << directory_.string() << ": "
                          << (file_error ? file_error : directory_error).message();
                if (published_) {
                    std::cerr << "; completed output was published";
                }
                std::cerr << '\n';
            } catch (...) {
                // Destruction must not throw while unwinding a rendering error.
            }
        }
    }

    const std::filesystem::path& path() const noexcept { return file_; }

    void publish(const std::filesystem::path& destination, bool force) {
        // Parent directories must be trusted and quiescent. Recheck the final
        // entry, but do not claim protection against hostile ancestor/entry swaps.
        const bool replace = replace_existing_output(destination, force);
        std::error_code error;
        if (replace) {
#ifdef _WIN32
            // Same-volume replacement only: never request a copy/delete fallback.
            if (!MoveFileExW(file_.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING)) {
                error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
            }
#else
            std::filesystem::rename(file_, destination, error);
#endif
        } else {
            // Linking publishes the closed file atomically and refuses every
            // existing entry, including one created after the preceding check.
            // If hard links are unsupported, fail safely without a copy fallback.
            std::filesystem::create_hard_link(file_, destination, error);
        }
        if (error) {
            throw std::filesystem::filesystem_error(
                "Unable to publish completed output", file_, destination, error);
        }
        published_ = true;
    }

private:
    std::filesystem::path directory_;
    std::filesystem::path file_;
    bool published_ = false;
};

void u16(std::ostream& output, std::uint16_t value) {
    const std::array<char, 2> bytes{static_cast<char>(value & 0xffU),
                                  static_cast<char>((value >> 8U) & 0xffU)};
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void u32(std::ostream& output, std::uint32_t value) {
    const std::array<char, 4> bytes{
        static_cast<char>(value & 0xffU), static_cast<char>((value >> 8U) & 0xffU),
        static_cast<char>((value >> 16U) & 0xffU), static_cast<char>((value >> 24U) & 0xffU)};
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void header(std::ostream& output, std::uint32_t rate, std::uint32_t samples) {
    output.write("RIFF", 4);
    u32(output, 50U + samples * 4U);
    output.write("WAVEfmt ", 8);
    u32(output, 18U); // WAVEFORMATEX, including cbSize = 0.
    u16(output, 3U);  // WAVE_FORMAT_IEEE_FLOAT.
    u16(output, 1U);
    u32(output, rate);
    u32(output, rate * 4U);
    u16(output, 4U);
    u16(output, 32U);
    u16(output, 0U);
    output.write("fact", 4);
    u32(output, 4U);
    u32(output, samples);
    output.write("data", 4);
    u32(output, samples * 4U);
}

struct AudioPathInfo {
    std::string_view name;
    double internal_rate_hz;
    std::size_t latency_frames;
    std::size_t quadrature_nodes = 0;
};

AudioPathInfo audio_path_info(const rhodes::ResearchEngine& engine) noexcept {
    return {"direct", engine.sample_rate_hz(), 0};
}

AudioPathInfo audio_path_info(const rhodes::AudioEngine& engine) noexcept {
    return {"filtered", engine.internal_sample_rate_hz(), engine.latency_frames()};
}

AudioPathInfo audio_path_info(const rhodes::QuadratureEngine& engine) noexcept {
    // There is no uniformly sampled internal stream in the quadrature path.
    return {"quadrature", 0.0, engine.latency_frames(), engine.node_count()};
}

template<typename Engine>
void render(const Options& options) {
    const auto output_name = options.output.string();
    Engine engine;
    if (!engine.prepare(static_cast<double>(options.sample_rate)) ||
        !engine.note_on(options.key, options.velocity)) {
        throw std::runtime_error("Audio engine rejected the requested configuration");
    }
    const auto path_info = audio_path_info(engine);
    const auto frames = static_cast<std::uint32_t>(
        std::llround(options.seconds * static_cast<double>(options.sample_rate)));
    const auto release_frame = static_cast<std::uint32_t>(
        std::llround(std::min(1.0, options.seconds * 0.65) *
                     static_cast<double>(options.sample_rate)));
    StagedOutput staged_output(options.output);
    std::ofstream output(staged_output.path(), std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("Unable to open temporary output file");
    }
    // Render exactly the requested host frames from the initial causal state.
    // In particular, do not skip the filtered delay or append its remaining tail.
    header(output, options.sample_rate, frames);
    double peak = 0.0;
    std::array<double, 256> block{};
    std::uint32_t written = 0;
    while (written < frames) {
        if (written == release_frame && !engine.note_off(options.key)) {
            throw std::runtime_error("Note release failed");
        }
        auto count = std::min(static_cast<std::uint32_t>(block.size()), frames - written);
        if (written < release_frame) {
            count = std::min(count, release_frame - written);
        }
        if (!engine.process(std::span<double>{block.data(), count})) {
            throw std::runtime_error("Numerical failure in the audio engine");
        }
        for (std::size_t j = 0; j < count; ++j) {
            const double value = block[j] * options.gain;
            if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max()) {
                throw std::runtime_error("Output exceeds the float-WAV numerical range");
            }
            peak = std::max(peak, std::abs(value));
            u32(output, std::bit_cast<std::uint32_t>(static_cast<float>(value)));
        }
        written += count;
    }
    output.flush();
    if (!output) {
        throw std::runtime_error("Temporary output write failed; destination was not changed");
    }
    output.close();
    if (!output) {
        throw std::runtime_error("Temporary output close failed; destination was not changed");
    }
    staged_output.publish(options.output, options.force);
    std::cout << "Wrote " << output_name << ": " << frames
              << " frames at " << options.sample_rate << " Hz, peak=" << peak
              << ", explicit monitoring gain=" << options.gain
              << ", audio-path=" << path_info.name;
    if (path_info.quadrature_nodes != 0) {
        std::cout << ", quadrature nodes=" << path_info.quadrature_nodes
                  << " per host cell, host rate=" << options.sample_rate << " Hz";
    } else {
        std::cout << ", internal rate=" << path_info.internal_rate_hz << " Hz";
    }
    std::cout << ", latency=" << path_info.latency_frames
              << " frames. Synthetic parameters; no hardware fidelity result.\n";
}
}

int main(int argc, char** argv) {
    static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559,
                  "WAV renderer requires IEEE-754 binary32 floats");
    try {
        Options options;
        for (int i = 1; i < argc; ++i) {
            const std::string_view flag{argv[i]};
            if (flag == "--help") {
                usage();
                return 0;
            }
            if (flag == "--force") {
                options.force = true;
                continue;
            }
            if (flag != "--output" && flag != "--seconds" && flag != "--sample-rate" &&
                flag != "--key" && flag != "--velocity" && flag != "--gain" &&
                flag != "--audio-path") {
                throw std::runtime_error("Unknown option: " + std::string(flag));
            }
            if (++i >= argc) {
                throw std::runtime_error("Missing value for " + std::string(flag));
            }
            const std::string_view value{argv[i]};
            if (flag == "--output") { options.output = std::filesystem::path{value}; }
            else if (flag == "--seconds") { options.seconds = number<double>(value, flag); }
            else if (flag == "--sample-rate") { options.sample_rate = number<std::uint32_t>(value, flag); }
            else if (flag == "--key") { options.key = number<std::size_t>(value, flag); }
            else if (flag == "--velocity") { options.velocity = number<double>(value, flag); }
            else if (flag == "--gain") { options.gain = number<double>(value, flag); }
            else if (flag == "--audio-path") {
                if (value == "direct") { options.audio_path = AudioPath::direct; }
                else if (value == "filtered") { options.audio_path = AudioPath::filtered; }
                else if (value == "quadrature") { options.audio_path = AudioPath::quadrature; }
                else { throw std::runtime_error("Invalid value for --audio-path; expected direct, filtered or quadrature"); }
            }
        }
        if (!std::isfinite(options.seconds) || options.seconds < 0.01 || options.seconds > 60.0 ||
            options.sample_rate < rhodes::minimum_sample_rate_hz ||
            options.sample_rate > rhodes::maximum_sample_rate_hz ||
            options.key >= rhodes::key_count || !std::isfinite(options.velocity) ||
            options.velocity < 0.0 || options.velocity > 1.0 ||
            !std::isfinite(options.gain) || options.gain < 0.0 || options.gain > 100.0 ||
            options.output.empty()) {
            throw std::runtime_error("Arguments are outside the documented ranges; see --help");
        }
        replace_existing_output(options.output, options.force);
        if (options.audio_path == AudioPath::quadrature) {
            render<rhodes::QuadratureEngine>(options);
        } else if (options.audio_path == AudioPath::filtered) {
            render<rhodes::AudioEngine>(options);
        } else {
            render<rhodes::ResearchEngine>(options);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "rhodes_render: " << error.what() << '\n';
        return 2;
    }
}
