#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>

namespace rhodes::vst3::photo {

// Original-image pixels, before any editor/device scaling. These approximate
// visual anchors describe the supplied photograph, not instrument measurements.
// Reference WebP SHA-256:
// 782b4c83a2bf827ed0d363bc567cdc2211485da6e650db4ae99448e78b7da24b
inline constexpr double kSourceWidth = 1536.0;
inline constexpr double kSourceHeight = 1152.0;
inline constexpr int kFirstMidiNote = 28;
inline constexpr int kLastMidiNote = 100;
inline constexpr int kKeyCount = kLastMidiNote - kFirstMidiNote + 1;

struct Point {
    double x{};
    double y{};

    [[nodiscard]] bool finite() const noexcept {
        return std::isfinite(x) && std::isfinite(y);
    }
};

struct Rect {
    double left{};
    double top{};
    double right{};
    double bottom{};

    [[nodiscard]] bool valid() const noexcept {
        return std::isfinite(left) && std::isfinite(top) &&
               std::isfinite(right) && std::isfinite(bottom) &&
               left < right && top < bottom;
    }

    // Edges are included. Callers resolve adjoining regions deterministically.
    [[nodiscard]] bool contains(Point point) const noexcept {
        return valid() && point.finite() && point.x >= left && point.x <= right &&
               point.y >= top && point.y <= bottom;
    }

    [[nodiscard]] constexpr Point center() const noexcept {
        return {left * 0.5 + right * 0.5, top * 0.5 + bottom * 0.5};
    }
};

struct Polygon {
    std::array<Point, 6> points{};
    std::size_t size{};

    [[nodiscard]] Rect bounds() const noexcept {
        if (size < 3 || size > points.size() || !points[0].finite()) {
            return {};
        }
        Rect result{points[0].x, points[0].y, points[0].x, points[0].y};
        for (std::size_t i = 1; i < size; ++i) {
            const auto point = points[i];
            if (!point.finite()) {
                return {};
            }
            result.left = point.x < result.left ? point.x : result.left;
            result.right = point.x > result.right ? point.x : result.right;
            result.top = point.y < result.top ? point.y : result.top;
            result.bottom = point.y > result.bottom ? point.y : result.bottom;
        }
        return result;
    }

    [[nodiscard]] bool contains(Point point) const noexcept {
        if (!bounds().contains(point)) {
            return false;
        }
        bool inside = false;
        for (std::size_t i = 0, j = size - 1; i < size; j = i++) {
            const auto a = points[j];
            const auto b = points[i];
            const long double dx = static_cast<long double>(b.x) - a.x;
            const long double dy = static_cast<long double>(b.y) - a.y;
            const long double px = static_cast<long double>(point.x) - a.x;
            const long double py = static_cast<long double>(point.y) - a.y;
            const long double cross = px * dy - py * dx;
            const long double tolerance = 1.0e-9L * (std::abs(dx) + std::abs(dy));
            if (!std::isfinite(cross) || !std::isfinite(tolerance)) {
                return false;
            }
            if (std::abs(cross) <= tolerance &&
                point.x >= (a.x < b.x ? a.x : b.x) &&
                point.x <= (a.x > b.x ? a.x : b.x) &&
                point.y >= (a.y < b.y ? a.y : b.y) &&
                point.y <= (a.y > b.y ? a.y : b.y)) {
                return true;
            }
            if ((a.y > point.y) != (b.y > point.y)) {
                const long double crossing = a.x + dx * (py / dy);
                if (!std::isfinite(crossing)) {
                    return false;
                }
                if (static_cast<long double>(point.x) < crossing) {
                    inside = !inside;
                }
            }
        }
        return inside;
    }
};

struct Control {
    std::uint32_t parameter_id{};
    Rect bounds{};
    int priority{};
};

// Larger priority wins; equal priorities retain this array's order. Narrow
// sliders/switches outrank padded knobs if future target tuning creates overlap.
inline constexpr std::array<Control, 7> kControls{{
    {0, {294.0, 433.0, 344.0, 483.0}, 10}, // Volume / monitoring gain.
    {1, {753.0, 887.0, 800.0, 977.0}, 10}, // Pedal / sustain.
    {3, {371.0, 468.0, 474.0, 489.0}, 20}, // Bass.
    {4, {370.0, 439.0, 473.0, 459.0}, 20}, // Treble.
    {5, {494.0, 433.0, 544.0, 483.0}, 10}, // Intensity.
    {6, {578.0, 433.0, 628.0, 483.0}, 10}, // Speed.
    {7, {548.0, 450.0, 573.0, 480.0}, 30}, // Vibrato/tremolo switch.
}};

[[nodiscard]] inline constexpr const Control* control_for_parameter(
    std::uint32_t parameter_id) noexcept {
    for (const auto& control : kControls) {
        if (control.parameter_id == parameter_id) {
            return &control;
        }
    }
    return nullptr;
}

[[nodiscard]] inline int hit_control(double x, double y) noexcept {
    const Control* selected = nullptr;
    for (const auto& control : kControls) {
        if (control.bounds.contains({x, y}) &&
            (selected == nullptr || control.priority > selected->priority)) {
            selected = &control;
        }
    }
    return selected == nullptr ? -1 : static_cast<int>(selected->parameter_id);
}

[[nodiscard]] inline constexpr bool is_black_key(int index) noexcept {
    if (index < 0 || index >= kKeyCount) {
        return false;
    }
    const int pitch_class = (kFirstMidiNote + index) % 12;
    return pitch_class == 1 || pitch_class == 3 || pitch_class == 6 ||
           pitch_class == 8 || pitch_class == 10;
}

namespace detail {

// Visually sampled near y=610 and y=560 respectively; keep the nonuniform
// spacing. The 44 white boundaries describe 43 white keys, not 44 keys.
inline constexpr std::array<double, 44> kWhiteFrontBoundaries{{
    113, 143, 174, 205, 236, 267, 298, 329, 360, 391, 422,
    453, 484, 515, 546, 577, 608, 639, 670, 701, 732, 763,
    794, 825, 856, 887, 919, 950, 981, 1013, 1044, 1076,
    1107, 1139, 1170, 1202, 1234, 1266, 1297, 1329, 1361,
    1393, 1425, 1457,
}};
inline constexpr std::array<double, 30> kBlackFrontCenters{{
    183, 217, 252, 305, 342, 395, 430, 464, 517, 555,
    608, 642, 677, 730, 767, 821, 856, 891, 945, 982,
    1036, 1071, 1106, 1159, 1197, 1252, 1287, 1322,
    1377, 1415,
}};

[[nodiscard]] constexpr Point white_rear(double front_x) noexcept {
    const double fraction = (front_x - 113.0) / 1344.0;
    return {147.0 + 1266.0 * fraction, 516.0 - fraction};
}

[[nodiscard]] constexpr Point white_surface_front(double front_x) noexcept {
    const double fraction = (front_x - 113.0) / 1344.0;
    return {112.0 + 1346.0 * fraction, 593.0 + fraction};
}

[[nodiscard]] constexpr std::array<Polygon, kKeyCount> make_key_polygons() noexcept {
    std::array<Polygon, kKeyCount> result{};
    std::size_t white = 0;
    std::size_t black = 0;
    for (int index = 0; index < kKeyCount; ++index) {
        auto& polygon = result[static_cast<std::size_t>(index)];
        if (is_black_key(index)) {
            const double front = kBlackFrontCenters[black++];
            // Approximate inward rear shift: 18-19 pixels at the outer keys.
            // Widths and rear interpolation are visual engineering estimates.
            const double rear = 780.0 + (front - 780.0) * 0.97;
            polygon.points = {{{rear - 8.5, 507.0}, {rear + 8.5, 507.0},
                               {front + 9.5, 565.0}, {front - 9.5, 565.0},
                               {}, {}}};
            polygon.size = 4;
        } else {
            const double left = kWhiteFrontBoundaries[white];
            const double right = kWhiteFrontBoundaries[++white];
            // Include the photographed front face without turning the sloped
            // top surface into a vertical strip. Adjacent keys share edges.
            polygon.points = {{white_rear(left), white_rear(right),
                               white_surface_front(right), {right, 615.0},
                               {left, 615.0}, white_surface_front(left)}};
            polygon.size = 6;
        }
    }
    return result;
}

inline constexpr auto kKeyPolygons = make_key_polygons();
inline constexpr Polygon kEmptyPolygon{};

[[nodiscard]] inline Point invalid_point() noexcept {
    const double invalid = std::numeric_limits<double>::quiet_NaN();
    return {invalid, invalid};
}

} // namespace detail

// Index 0..72 maps to MIDI 28..100 (E1..E7); invalid indices return an empty
// polygon. Geometry is immutable and needs no SDK, allocation, or GUI state.
[[nodiscard]] inline constexpr const Polygon& key_polygon(int index) noexcept {
    return index >= 0 && index < kKeyCount ?
        detail::kKeyPolygons[static_cast<std::size_t>(index)] : detail::kEmptyPolygon;
}

// Black surfaces override underlying white surfaces, including shared edges.
// Equal-colour shared edges choose the lower key index. Outside returns -1.
[[nodiscard]] inline int hit_key(double x, double y) noexcept {
    const Point point{x, y};
    if (!point.finite() || x < 0.0 || x > kSourceWidth ||
        y < 0.0 || y > kSourceHeight) {
        return -1;
    }
    for (const bool black : {true, false}) {
        for (int index = 0; index < kKeyCount; ++index) {
            if (is_black_key(index) == black && key_polygon(index).contains(point)) {
                return index;
            }
        }
    }
    return -1;
}

// Use this same transform for drawing and input. Width/height and view points
// must share a coordinate system (logical pixels or device pixels, not both).
struct AspectFit {
    double scale{};
    double offset_x{};
    double offset_y{};

    [[nodiscard]] bool valid() const noexcept {
        return std::isfinite(scale) && scale > 0.0 &&
               std::isfinite(offset_x) && offset_x >= 0.0 &&
               std::isfinite(offset_y) && offset_y >= 0.0 &&
               std::isfinite(offset_x + kSourceWidth * scale) &&
               std::isfinite(offset_y + kSourceHeight * scale);
    }

    [[nodiscard]] static AspectFit fit(double width, double height) noexcept {
        if (!std::isfinite(width) || !std::isfinite(height) ||
            width <= 0.0 || height <= 0.0) {
            return {};
        }
        const double sx = width / kSourceWidth;
        const double sy = height / kSourceHeight;
        const double scale = sx < sy ? sx : sy;
        const double x = (width - kSourceWidth * scale) * 0.5;
        const double y = (height - kSourceHeight * scale) * 0.5;
        const AspectFit result{scale, x > 0.0 ? x : 0.0, y > 0.0 ? y : 0.0};
        return result.valid() ? result : AspectFit{};
    }

    [[nodiscard]] Rect view_bounds() const noexcept {
        return valid() ? Rect{offset_x, offset_y, offset_x + kSourceWidth * scale,
                             offset_y + kSourceHeight * scale} : Rect{};
    }

    [[nodiscard]] bool contains_view(Point point) const noexcept {
        return view_bounds().contains(point);
    }

    // These conversions do not clamp: points in letterboxing stay outside the
    // source, so hit tests reject them. Invalid transforms/inputs return NaNs.
    [[nodiscard]] Point source_to_view(Point point) const noexcept {
        if (!valid() || !point.finite()) {
            return detail::invalid_point();
        }
        const Point result{offset_x + point.x * scale, offset_y + point.y * scale};
        return result.finite() ? result : detail::invalid_point();
    }

    [[nodiscard]] Point view_to_source(Point point) const noexcept {
        if (!valid() || !point.finite()) {
            return detail::invalid_point();
        }
        const Point result{(point.x - offset_x) / scale, (point.y - offset_y) / scale};
        return result.finite() ? result : detail::invalid_point();
    }
};

} // namespace rhodes::vst3::photo
