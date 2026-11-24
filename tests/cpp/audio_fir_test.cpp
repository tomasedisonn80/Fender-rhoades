#include "rhodes/fir_decimator.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
constexpr std::array<std::size_t, 3> factors{1, 2, 4};

void require(bool condition, std::string_view message) {
    if (!condition) { throw std::runtime_error(std::string{message}); }
}

double output(const rhodes::AudioFir& fir) {
    const auto value = fir.output();
    require(value.has_value(), "ordinary finite FIR stream faulted");
    return *value;
}

// Independent reference: ordinary chronological input storage, every tap once,
// no circular indexing, symmetric pairing, or implementation phase bookkeeping.
long double direct(std::span<const double> samples, std::span<const double> h,
                   std::size_t time) {
    long double sum = 0.0L;
    const std::size_t count = std::min(h.size(), time + 1);
    for (std::size_t k = 0; k < count; ++k) {
        sum += static_cast<long double>(h[k]) * samples[time - k];
    }
    return sum;
}

void fir_coefficients_dc() {
    for (const auto factor : factors) {
        rhodes::AudioFir fir;
        require(fir.prepare(factor), "supported FIR factor rejected");
        const auto h = fir.coefficients();
        require(h.size() == 96 * factor + 1, "wrong FIR tap count");
        long double sum = 0.0L;
        for (std::size_t k = 0; k < h.size(); ++k) {
            require(std::isfinite(h[k]), "nonfinite FIR coefficient");
            require(std::abs(h[k] - h[h.size() - 1 - k]) <= 5e-15,
                    "FIR coefficients are not symmetric");
            sum += h[k];
        }
        require(std::abs(sum - 1.0L) <= 5e-15L, "FIR DC normalization differs");
        require(output(fir) == 0.0, "prepared FIR history is not zero");
        for (std::size_t n = 0; n < 4 * h.size(); ++n) {
            require(fir.push(1.0), "finite DC sample rejected");
            if (n + 1 >= h.size()) {
                require(std::abs(output(fir) - 1.0) <= 5e-15,
                        "settled FIR DC output differs from unity");
            }
        }
    }
}

void fir_impulse_phases() {
    for (const auto factor : factors) {
        rhodes::AudioFir fir;
        require(fir.prepare(factor), "supported FIR factor rejected");
        const auto h = fir.coefficients();
        // N is coprime to L. Times 0..N*L-1 cover every combination of
        // circular insertion position and absolute host-decimation residue.
        std::vector<bool> covered(h.size() * factor, false);
        for (std::size_t onset = 0; onset < h.size() * factor; ++onset) {
            fir.reset();
            for (std::size_t n = 0; n < onset; ++n) {
                require(fir.push(0.0), "zero warmup rejected");
            }
            covered[(onset % h.size()) * factor + onset % factor] = true;
            for (std::size_t age = 0; age < h.size() + 2 * factor; ++age) {
                require(fir.push(age == 0 ? 1.0 : 0.0), "impulse sample rejected");
                const double expected = age < h.size() ? h[age] : 0.0;
                const double actual = output(fir);
                require(std::abs(actual - expected) <= 5e-15,
                        "impulse tap, ring phase, or final tail differs");
                if (age >= h.size()) {
                    require(actual == 0.0, "impulse survived its finite support");
                }
                // Explicitly exercise every host output phase as well as all
                // internal samples; the FIR itself does not own decimation.
                if ((onset + age) % factor == 0) {
                    require(std::abs(actual - expected) <= 5e-15,
                            "decimated impulse phase differs");
                }
            }
        }
        require(std::all_of(covered.begin(), covered.end(), [](bool x) { return x; }),
                "impulse fixture missed a ring-position/phase combination");
    }
}

void fir_direct_convolution() {
    long double largest_error = 0.0L;
    for (const auto factor : factors) {
        rhodes::AudioFir fir;
        require(fir.prepare(factor), "supported FIR factor rejected");
        const auto h = fir.coefficients();
        const std::size_t active = 67 * h.size() + 17;
        std::vector<double> samples(active + 2 * h.size(), 0.0);
        std::uint32_t seed = 0x46d38a15U;
        for (std::size_t n = 0; n < active; ++n) {
            seed = seed * 1664525U + 1013904223U;
            const double noise = static_cast<double>(seed >> 8U) / 16777216.0 - 0.5;
            samples[n] = 0.7 * noise + 0.2 * std::sin(0.017 * static_cast<double>(n))
                         + (n % 137 == 0 ? -0.4 : 0.0);
        }
        for (std::size_t n = 0; n < samples.size(); ++n) {
            require(fir.push(samples[n]), "finite reference stream sample rejected");
            const long double expected = direct(samples, h, n);
            const long double error = std::abs(static_cast<long double>(output(fir)) - expected);
            largest_error = std::max(largest_error, error);
            require(error <= 2e-14L, "FIR differs from full direct convolution");
            if (n >= active + h.size() - 1) {
                require(output(fir) == 0.0, "finite input tail did not expire");
            }
        }
    }
    std::cout << "maximum direct-convolution absolute error="
              << static_cast<double>(largest_error) << '\n';
}

void fir_group_delay() {
    for (const auto factor : factors) {
        rhodes::AudioFir fir;
        require(fir.prepare(factor), "supported FIR factor rejected");
        const auto h = fir.coefficients();
        long double mass = 0.0L;
        long double first_moment = 0.0L;
        for (std::size_t k = 0; k < h.size(); ++k) {
            mass += h[k];
            first_moment += static_cast<long double>(k) * h[k];
        }
        require(std::abs(first_moment / mass / static_cast<long double>(factor) - 48.0L) <= 2e-12L,
                "coefficient group delay is not 48 host frames");
        std::vector<double> host;
        for (std::size_t t = 0; t < h.size() + 3 * factor; ++t) {
            require(fir.push(t == 0 ? 1.0 : 0.0), "delay impulse rejected");
            // Frozen timing: return index L*n, never L*n+(L-1).
            if (t % factor == 0) { host.push_back(output(fir)); }
        }
        const auto peak = std::max_element(host.begin(), host.end());
        require(static_cast<std::size_t>(peak - host.begin()) == 48,
                "impulse peak is not at host frame 48");
        for (std::size_t n = 0; n < host.size(); ++n) {
            const double expected = n * factor < h.size() ? h[n * factor] : 0.0;
            require(std::abs(host[n] - expected) <= 5e-15,
                    "host impulse uses wrong internal output phase");
        }
        for (std::size_t d = 0; d <= 48; ++d) {
            require(std::abs(host[48 - d] - host[48 + d]) <= 5e-15,
                    "host impulse lacks symmetry about frame 48");
        }
    }
}

void fir_transactional_rejection() {
    for (const auto factor : factors) {
        rhodes::AudioFir fir;
        require(fir.prepare(factor), "supported FIR factor rejected");
        for (std::size_t n = 0; n < 3 * fir.coefficients().size() + 7; ++n) {
            require(fir.push(std::sin(static_cast<double>(n))), "history setup failed");
        }
        auto unchanged = fir;
        const auto same = [&] {
            require(fir.coefficients().data() == unchanged.coefficients().data() &&
                    fir.coefficients().size() == unchanged.coefficients().size(),
                    "invalid prepare changed selected coefficients");
            require(fir.output() == unchanged.output(), "rejected operation changed FIR output");
        };
        for (const std::size_t bad : {std::size_t{0}, std::size_t{3}, std::size_t{5},
                                     std::numeric_limits<std::size_t>::max()}) {
            require(!fir.prepare(bad), "invalid FIR factor accepted");
            same();
        }
        for (const double bad : {std::numeric_limits<double>::quiet_NaN(),
                                 std::numeric_limits<double>::infinity(),
                                 -std::numeric_limits<double>::infinity()}) {
            require(!fir.push(bad), "nonfinite FIR sample accepted");
            same();
        }
        // Immediate output equality alone cannot expose a moved insertion cursor.
        for (std::size_t n = 0; n < 3 * fir.coefficients().size(); ++n) {
            const double value = std::cos(0.31 * static_cast<double>(n));
            require(fir.push(value) && unchanged.push(value), "continuation sample rejected");
            same();
        }
        for (const auto replacement : factors) {
            require(fir.prepare(replacement), "valid replacement factor rejected");
            require(fir.coefficients().size() == 96 * replacement + 1 && output(fir) == 0.0,
                    "valid prepare did not replace coefficients and clear history");
        }
    }
}

void fir_overflow_reset() {
    for (const auto factor : factors) {
        rhodes::AudioFir fir;
        require(fir.prepare(factor), "supported FIR factor rejected");
        const auto h = fir.coefficients();
        std::vector<double> samples(h.size());
        const double large = 0.8 * std::numeric_limits<double>::max();
        for (std::size_t n = 0; n < h.size(); ++n) {
            samples[n] = std::copysign(large, h[h.size() - 1 - n]);
            require(fir.push(samples[n]), "finite extreme sample incorrectly rejected");
        }
        require(direct(samples, h, samples.size() - 1) > std::numeric_limits<double>::max(),
                "overflow fixture does not exceed finite output range");
        require(!fir.output().has_value(), "arithmetic overflow returned a finite output");
        require(!fir.output().has_value(), "reading overflow output mutated history");
        require(!fir.push(std::numeric_limits<double>::quiet_NaN()), "NaN accepted after fault");
        require(!fir.output().has_value(), "invalid push erased arithmetic fault history");
        fir.reset();
        require(output(fir) == 0.0, "explicit reset failed to clear fault history");
        require(fir.push(1.0), "FIR did not recover after reset");
        require(std::abs(output(fir) - h.front()) <= 5e-15,
                "reset did not restore impulse timing");
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        const std::string_view selected = argc > 1 ? argv[1] : "all";
        bool found = false;
        const auto run = [&](std::string_view name, auto test) {
            if (selected == "all" || selected == name) {
                test(); found = true; std::cout << "PASS " << name << '\n';
            }
        };
        run("fir_coefficients_dc", fir_coefficients_dc);
        run("fir_impulse_phases", fir_impulse_phases);
        run("fir_direct_convolution", fir_direct_convolution);
        run("fir_group_delay", fir_group_delay);
        run("fir_transactional_rejection", fir_transactional_rejection);
        run("fir_overflow_reset", fir_overflow_reset);
        require(found, "unknown FIR fixture");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
