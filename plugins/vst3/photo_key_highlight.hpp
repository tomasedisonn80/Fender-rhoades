#pragma once

#include "photo_geometry.hpp"

#include <array>
#include <cstddef>

namespace rhodes::vst3::photo {

// A convex quadrilateral intersection has at most eight vertices. These paths
// use the original photograph's pixel coordinates and visual estimates; they
// do not change the playable key polygons or imply measured key dimensions.
struct HighlightCutout {
    std::array<Point, 8> points{};
    std::size_t size{};
};

struct KeyHighlight {
    Polygon outline{};
    std::array<HighlightCutout, 2> cutouts{};
    std::size_t cutout_count{};
    bool stroke_outline{};
};

namespace highlight_detail {

[[nodiscard]] constexpr double edge_side(Point a, Point b, Point point) noexcept {
    return (b.x - a.x) * (point.y - a.y) -
           (b.y - a.y) * (point.x - a.x);
}

[[nodiscard]] constexpr bool same_point(Point a, Point b) noexcept {
    constexpr double tolerance = 1.0e-10;
    return a.x - b.x <= tolerance && b.x - a.x <= tolerance &&
           a.y - b.y <= tolerance && b.y - a.y <= tolerance;
}

[[nodiscard]] constexpr bool append(HighlightCutout& polygon, Point point) noexcept {
    if (polygon.size > 0 && same_point(polygon.points[polygon.size - 1], point)) {
        return true;
    }
    if (polygon.size == polygon.points.size()) {
        return false;
    }
    polygon.points[polygon.size++] = point;
    return true;
}

// Sutherland-Hodgman clipping against one directed edge. All inputs here are
// the immutable convex photo geometry, with their interiors on the left of
// each edge in numeric x/y coordinates. Bounded storage never allocates.
[[nodiscard]] constexpr HighlightCutout clip_edge(
    const HighlightCutout& input, Point a, Point b) noexcept {
    HighlightCutout output{};
    if (input.size < 3 || input.size > input.points.size()) {
        return output;
    }
    Point previous = input.points[input.size - 1];
    double previous_side = edge_side(a, b, previous);
    for (std::size_t index = 0; index < input.size; ++index) {
        const auto current = input.points[index];
        const double current_side = edge_side(a, b, current);
        const bool previous_inside = previous_side >= 0.0;
        const bool current_inside = current_side >= 0.0;
        if (previous_inside != current_inside) {
            // Opposite signs guarantee a nonzero denominator.
            const double fraction = previous_side / (previous_side - current_side);
            const Point crossing{previous.x + fraction * (current.x - previous.x),
                                 previous.y + fraction * (current.y - previous.y)};
            if (!append(output, crossing)) {
                return {};
            }
        }
        if (current_inside && !append(output, current)) {
            return {};
        }
        previous = current;
        previous_side = current_side;
    }
    if (output.size > 1 && same_point(output.points[0], output.points[output.size - 1])) {
        --output.size;
    }
    return output;
}

[[nodiscard]] constexpr HighlightCutout black_overlap(
    const Polygon& white, const Polygon& black) noexcept {
    if (white.size != 6 || black.size != 4) {
        return {};
    }
    HighlightCutout intersection{};
    // Clip only the convex top surface: the photographed white front face is
    // below all black keys. The six-point whole white outline is not convex.
    intersection.points[0] = white.points[0];
    intersection.points[1] = white.points[1];
    intersection.points[2] = white.points[2];
    intersection.points[3] = white.points[5];
    intersection.size = 4;
    for (std::size_t edge = 0; edge < black.size; ++edge) {
        intersection = clip_edge(intersection, black.points[edge],
                                 black.points[(edge + 1) % black.size]);
    }
    if (intersection.size < 3) {
        return {};
    }
    // Triangulate about one vertex to avoid subtracting large source-coordinate
    // products for the very narrow end-register intersections.
    double twice_area = 0.0;
    for (std::size_t index = 1; index + 1 < intersection.size; ++index) {
        twice_area += edge_side(intersection.points[0], intersection.points[index],
                                intersection.points[index + 1]);
    }
    return twice_area > 1.0e-10 ? intersection : HighlightCutout{};
}

[[nodiscard]] constexpr std::array<KeyHighlight, kKeyCount> make_key_highlights() noexcept {
    std::array<KeyHighlight, kKeyCount> highlights{};
    for (int key = 0; key < kKeyCount; ++key) {
        auto& highlight = highlights[static_cast<std::size_t>(key)];
        highlight.outline = key_polygon(key);
        highlight.stroke_outline = is_black_key(key);
        if (highlight.stroke_outline) {
            continue;
        }
        for (const int neighbor : {key - 1, key + 1}) {
            if (!is_black_key(neighbor)) {
                continue;
            }
            const auto cutout = black_overlap(highlight.outline, key_polygon(neighbor));
            if (cutout.size >= 3) {
                highlight.cutouts[highlight.cutout_count++] = cutout;
            }
        }
    }
    return highlights;
}

inline constexpr auto kKeyHighlights = make_key_highlights();
inline constexpr KeyHighlight kEmptyHighlight{};

} // namespace highlight_detail

// Add outline and each cutout as CLOSED subpaths of one EVEN-ODD filled path.
// Every cutout lies within the white outline; full black polygons would paint
// their portions outside the white key under an even-odd fill. White outlines
// must not be stroked: their original edges pass through neighboring blacks.
// Black keys retain their original fill and outline. Invalid keys are empty.
[[nodiscard]] inline constexpr const KeyHighlight& key_highlight(int key) noexcept {
    return key >= 0 && key < kKeyCount ?
        highlight_detail::kKeyHighlights[static_cast<std::size_t>(key)] :
        highlight_detail::kEmptyHighlight;
}

} // namespace rhodes::vst3::photo
