// Retained visual-anchor and coordinate-transform contract for the supplied photo.
// Anchors are visual engineering estimates, not physical instrument measurements.
#include "plugins/vst3/photo_geometry.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
namespace photo = rhodes::vst3::photo;
std::size_t checks = 0;

void require(bool condition, const char* message) {
    ++checks;
    if (!condition) { throw std::runtime_error(message); }
}

bool close(double actual, double expected) {
    return std::abs(actual - expected) < 1.0e-8;
}

photo::Point mean(const photo::Polygon& polygon) {
    photo::Point result{};
    for (std::size_t i = 0; i < polygon.size; ++i) {
        result.x += polygon.points[i].x / static_cast<double>(polygon.size);
        result.y += polygon.points[i].y / static_cast<double>(polygon.size);
    }
    return result;
}

void keyboard() {
    constexpr std::array<int, 30> black_notes{{
        30, 32, 34, 37, 39, 42, 44, 46, 49, 51,
        54, 56, 58, 61, 63, 66, 68, 70, 73, 75,
        78, 80, 82, 85, 87, 90, 92, 94, 97, 99,
    }};
    constexpr std::array<double, 30> measured_black_x{{
        183, 217, 252, 305, 342, 395, 430, 464, 517, 555,
        608, 642, 677, 730, 767, 821, 856, 891, 945, 982,
        1036, 1071, 1106, 1159, 1197, 1252, 1287, 1322, 1377, 1415,
    }};
    constexpr std::array<double, 44> measured_white_x{{
        113, 143, 174, 205, 236, 267, 298, 329, 360, 391, 422,
        453, 484, 515, 546, 577, 608, 639, 670, 701, 732, 763,
        794, 825, 856, 887, 919, 950, 981, 1013, 1044, 1076,
        1107, 1139, 1170, 1202, 1234, 1266, 1297, 1329, 1361,
        1393, 1425, 1457,
    }};
    std::set<int> reached;
    std::size_t white_index = 0;
    std::size_t black_index = 0;
    for (int index = 0; index < 73; ++index) {
        const auto& polygon = photo::key_polygon(index);
        const auto center = mean(polygon);
        require(polygon.contains(center), "polygon center is inside itself");
        const int hit = photo::hit_key(center.x, center.y);
        require(hit == index, "all 73 polygon centers reach their own key");
        reached.insert(hit);
        const bool expected_black = black_index < black_notes.size() &&
            black_notes[black_index] == 28 + index;
        require(photo::is_black_key(index) == expected_black, "E1-E7 pitch classification");
        if (expected_black) {
            require(polygon.size == 4, "black polygon is a trapezoid");
            const auto measured = photo::Point{measured_black_x[black_index], 560.0};
            require(photo::hit_key(measured.x, measured.y) == index,
                    "measured black center preserves MIDI ordering");
            bool underlying_white = false;
            for (int other = 0; other < 73; ++other) {
                if (!photo::is_black_key(other) && photo::key_polygon(other).contains(center)) {
                    underlying_white = true;
                }
            }
            require(underlying_white, "black test point overlaps a white polygon");
            for (std::size_t vertex = 0; vertex < polygon.size; ++vertex) {
                const auto point = polygon.points[vertex];
                require(photo::hit_key(point.x, point.y) == index, "black edges have precedence");
            }
            ++black_index;
        } else {
            require(polygon.size == 6, "white surface includes front face");
            const double x = (measured_white_x[white_index] +
                              measured_white_x[white_index + 1]) * 0.5;
            require(photo::hit_key(x, 610.0) == index, "all measured white front centers map");
            ++white_index;
        }
    }
    require(reached.size() == 73 && white_index == 43 && black_index == 30,
            "73 unique keys, 43 white and 30 black");
    require(photo::hit_key(143.0, 615.0) == 0, "shared white boundary chooses lower index");
    require(photo::hit_key(113.0, 615.0) == 0, "left outer corner is playable");
    require(photo::hit_key(1457.0, 615.0) == 72, "right outer corner is playable");
    for (const auto point : std::array<photo::Point, 8>{{
             {0, 0}, {150, 500}, {110, 600}, {1459, 600},
             {780, 616}, {-1, 550}, {1537, 550}, {1536, 1152}}}) {
        require(photo::hit_key(point.x, point.y) == -1, "outside keyboard rejects hit");
    }

}

void controls() {
    constexpr std::array<std::uint32_t, 7> control_ids{{0, 1, 3, 4, 5, 6, 7}};
    for (const auto id : control_ids) {
        const auto* control = photo::control_for_parameter(id);
        require(control != nullptr, "known parameter has a control");
        const auto center = control->bounds.center();
        require(photo::hit_control(center.x, center.y) == static_cast<int>(id),
                "control center uniquely hits correct parameter");
        require(photo::hit_control(control->bounds.left, control->bounds.top) ==
                    static_cast<int>(id), "control boundary is included");
    }
    require(photo::hit_control(420, 450) == 4, "Treble remains distinct from Bass");
    require(photo::hit_control(420, 479) == 3, "Bass remains distinct from Treble");
    require(photo::hit_control(560, 440) == -1, "red light is not the switch-only target");
    require(photo::hit_control(777, 932) == 1, "pedal maps to sustain");
    require(photo::control_for_parameter(2) == nullptr, "Expression is not a photo control");
    require(photo::control_for_parameter(999) == nullptr, "unknown control rejects lookup");
    require(photo::kControls[6].priority > photo::kControls[0].priority,
            "switch priority exceeds padded knobs");

}

void transforms() {
    for (const auto size : std::array<photo::Point, 7>{{
             {1536, 1152}, {1024, 768}, {800, 800}, {1400, 700},
             {400, 1200}, {0.01, 0.01}, {1.0e6, 2.0e6}}}) {
        const auto transform = photo::AspectFit::fit(size.x, size.y);
        require(transform.valid(), "positive viewport has valid fit");
        const auto bounds = transform.view_bounds();
        require(bounds.left >= 0 && bounds.top >= 0 &&
                    bounds.right <= size.x + 1.0e-8 && bounds.bottom <= size.y + 1.0e-8,
                "fit preserves whole photo inside viewport");
        for (int index = 0; index < 73; ++index) {
            const auto source = mean(photo::key_polygon(index));
            const auto view = transform.source_to_view(source);
            require(transform.contains_view(view), "mapped key remains in fitted image");
            const auto returned = transform.view_to_source(view);
            require(close(source.x, returned.x) && close(source.y, returned.y),
                    "source/view transform roundtrip");
            require(photo::hit_key(returned.x, returned.y) == index,
                    "arbitrary aspect ratios preserve every key hit");
        }
        const auto outside = transform.view_to_source({bounds.left - 0.1, bounds.top});
        require(!transform.contains_view({bounds.left - 0.1, bounds.top}),
                "letterbox/outside point is excluded");
        require(photo::hit_key(outside.x, outside.y) == -1, "inverse does not clamp letterbox");
    }
    const auto landscape = photo::AspectFit::fit(1400, 700);
    require(close(landscape.offset_x, (1400.0 - 700.0 * 4.0 / 3.0) * 0.5) &&
                close(landscape.offset_y, 0), "wide view centers horizontal letterbox");
    const auto portrait = photo::AspectFit::fit(800, 800);
    require(close(portrait.offset_x, 0) && close(portrait.offset_y, 100),
            "square view centers vertical letterbox");

}

void invalid_inputs() {
    const auto landscape = photo::AspectFit::fit(1400, 700);
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    for (const double invalid : std::array<double, 3>{{nan, inf, -inf}}) {
        require(photo::hit_key(invalid, 550) == -1 && photo::hit_key(780, invalid) == -1,
                "nonfinite key coordinates reject");
        require(photo::hit_control(invalid, 458) == -1 &&
                    photo::hit_control(319, invalid) == -1, "nonfinite control coordinates reject");
        require(!photo::AspectFit::fit(invalid, 768).valid() &&
                    !photo::AspectFit::fit(1024, invalid).valid(), "nonfinite viewports reject");
        require(!landscape.source_to_view({invalid, 550}).finite() &&
                    !landscape.view_to_source({780, invalid}).finite(),
                "nonfinite transforms return invalid points");
    }
    for (const auto size : std::array<photo::Point, 5>{{
             {0, 768}, {1024, 0}, {-1, 768}, {1024, -1},
             {std::numeric_limits<double>::denorm_min(), 768}}}) {
        const auto transform = photo::AspectFit::fit(size.x, size.y);
        require(!transform.valid() && !transform.source_to_view({319, 458}).finite() &&
                    !transform.contains_view({0, 0}), "zero/negative/underflow fits reject");
    }
    require(photo::key_polygon(-1).size == 0 && photo::key_polygon(73).size == 0 &&
                photo::key_polygon(std::numeric_limits<int>::min()).size == 0 &&
                photo::key_polygon(std::numeric_limits<int>::max()).size == 0,
            "invalid key indices return empty polygons");
    photo::Polygon malformed{};
    malformed.size = 7;
    require(!malformed.contains({0, 0}), "oversized polygon rejects without indexing");
    malformed = photo::key_polygon(0);
    malformed.points[2].x = nan;
    require(!malformed.contains({130, 590}), "nonfinite polygon vertices reject");
    require(!photo::Rect{0, 0, inf, 1}.contains({0, 0}), "nonfinite rectangle rejects");
    require(!photo::Rect{1, 1, 0, 0}.contains({0.5, 0.5}), "inverted rectangle rejects");
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc > 2) { throw std::runtime_error("expected at most one fixture name"); }
        const std::string_view selected = argc > 1 ? argv[1] : "all";
        bool found = false;
        const auto run = [&](std::string_view name, auto test) {
            if (selected == "all" || selected == name) {
                test();
                found = true;
                std::cout << "PASS " << name << '\n';
            }
        };
        run("keyboard", keyboard);
        run("controls", controls);
        run("transforms", transforms);
        run("invalid_inputs", invalid_inputs);
        if (!found) { throw std::runtime_error("unknown fixture: " + std::string{selected}); }
        std::cout << "PASS total checks=" << checks << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
