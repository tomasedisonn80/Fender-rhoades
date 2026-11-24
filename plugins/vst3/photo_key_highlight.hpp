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

// A white key can have a detached rear sliver where the photographed black
// trapezoid crosses both its rear and side edges. The immutable geometry needs
// at most two simple contours, with at most sixteen vertices in either one.
struct HighlightPath {
    std::array<Point, 16> points{};
    std::size_t size{};
};

struct KeyHighlight {
    Polygon outline{};
    std::array<HighlightCutout, 2> cutouts{};
    std::size_t cutout_count{};
    bool stroke_outline{};
    std::array<HighlightPath, 2> visible_paths{};
    std::size_t visible_path_count{};
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

[[nodiscard]] constexpr long double precise_side(Point a, Point b, Point point) noexcept {
    return (static_cast<long double>(b.x) - a.x) *
               (static_cast<long double>(point.y) - a.y) -
           (static_cast<long double>(b.y) - a.y) *
               (static_cast<long double>(point.x) - a.x);
}

[[nodiscard]] constexpr bool opposite(long double a, long double b) noexcept {
    return (a < 0.0L && b > 0.0L) || (a > 0.0L && b < 0.0L);
}

[[nodiscard]] constexpr bool inside_black(const Polygon& polygon, Point point) noexcept {
    for (std::size_t edge = 0; edge < polygon.size; ++edge) {
        if (precise_side(polygon.points[edge],
                         polygon.points[(edge + 1) % polygon.size], point) <= 0.0L) {
            return false;
        }
    }
    return polygon.size == 4;
}

[[nodiscard]] constexpr bool inside_white_surface(
    const Polygon& polygon, Point point) noexcept {
    constexpr std::array<std::size_t, 4> surface{{0, 1, 2, 5}};
    for (std::size_t edge = 0; edge < surface.size(); ++edge) {
        if (precise_side(polygon.points[surface[edge]],
                         polygon.points[surface[(edge + 1) % surface.size()]], point) <= 0.0L) {
            return false;
        }
    }
    return polygon.size == 6;
}

struct VisibleContours {
    std::array<HighlightPath, 2> paths{};
    std::size_t size{};
};

// This arrangement stores an intersection once and shares its node between
// both intersecting edges. No near-equal endpoint matching, geometry epsilon,
// outline/hole cancellation, or allocation is involved. Kept white segments
// run forwards; kept black segments run backwards. Their directed cycles are
// exactly the exposed contours, including any disconnected rear sliver.
[[nodiscard]] constexpr VisibleContours visible_white_contours(int key) noexcept {
    constexpr std::size_t capacity = 32;
    struct Node {
        Point point{};
        std::size_t next{capacity};
        std::size_t incoming{};
        bool visited{};
    };
    struct SplitEdge {
        std::array<std::size_t, 8> nodes{};
        std::array<long double, 8> fractions{};
        std::size_t size{};

        [[nodiscard]] constexpr bool insert(std::size_t node, long double fraction) noexcept {
            if (size == nodes.size()) { return false; }
            std::size_t position = size++;
            while (position > 0 && fractions[position - 1] > fraction) {
                nodes[position] = nodes[position - 1];
                fractions[position] = fractions[position - 1];
                --position;
            }
            nodes[position] = node;
            fractions[position] = fraction;
            return true;
        }
    };

    const auto& white = key_polygon(key);
    if (white.size != 6 || is_black_key(key)) { return {}; }
    std::array<const Polygon*, 2> blacks{};
    std::size_t black_count = 0;
    for (const int neighbor : {key - 1, key + 1}) {
        if (is_black_key(neighbor)) { blacks[black_count++] = &key_polygon(neighbor); }
    }
    std::array<Node, capacity> nodes{};
    std::size_t node_count = white.size;
    std::array<SplitEdge, 6> white_edges{};
    std::array<std::array<SplitEdge, 4>, 2> black_edges{};
    for (std::size_t edge = 0; edge < white.size; ++edge) {
        nodes[edge].point = white.points[edge];
        if (!white_edges[edge].insert(edge, 0.0L) ||
            !white_edges[edge].insert((edge + 1) % white.size, 1.0L)) { return {}; }
    }
    for (std::size_t black = 0; black < black_count; ++black) {
        const auto& polygon = *blacks[black];
        const auto base = node_count;
        if (polygon.size != 4 || node_count + polygon.size > capacity) { return {}; }
        for (std::size_t edge = 0; edge < polygon.size; ++edge) {
            // Black geometry is wholly above the white front face. Thus its
            // segments can be classified against the convex white top surface.
            if (polygon.points[edge].y >= white.points[2].y ||
                polygon.points[edge].y >= white.points[5].y) { return {}; }
            nodes[node_count++].point = polygon.points[edge];
            if (!black_edges[black][edge].insert(base + edge, 0.0L) ||
                !black_edges[black][edge].insert(base + (edge + 1) % polygon.size, 1.0L)) {
                return {};
            }
        }
        for (std::size_t white_edge = 0; white_edge < white.size; ++white_edge) {
            const auto a = white.points[white_edge];
            const auto b = white.points[(white_edge + 1) % white.size];
            for (std::size_t black_edge = 0; black_edge < polygon.size; ++black_edge) {
                const auto c = polygon.points[black_edge];
                const auto d = polygon.points[(black_edge + 1) % polygon.size];
                const auto a_side = precise_side(c, d, a);
                const auto b_side = precise_side(c, d, b);
                const auto c_side = precise_side(a, b, c);
                const auto d_side = precise_side(a, b, d);
                // The immutable inputs have only proper crossings. A future
                // endpoint/collinear contact must get an explicit topology
                // implementation instead of an epsilon perturbation.
                if ((a_side == 0.0L || b_side == 0.0L) && opposite(c_side, d_side)) {
                    return {};
                }
                if ((c_side == 0.0L || d_side == 0.0L) && opposite(a_side, b_side)) {
                    return {};
                }
                if (!opposite(a_side, b_side) || !opposite(c_side, d_side)) { continue; }
                if (node_count == capacity) { return {}; }
                const auto white_fraction = a_side / (a_side - b_side);
                const auto black_fraction = c_side / (c_side - d_side);
                // Construct from the black edge so every newly exposed segment
                // uses its original boundary, including an exact horizontal y.
                nodes[node_count].point = {
                    static_cast<double>(c.x + black_fraction * (static_cast<long double>(d.x) - c.x)),
                    static_cast<double>(c.y + black_fraction * (static_cast<long double>(d.y) - c.y))};
                if (!white_edges[white_edge].insert(node_count, white_fraction) ||
                    !black_edges[black][black_edge].insert(node_count, black_fraction)) { return {}; }
                ++node_count;
            }
        }
    }
    const auto connect = [&nodes](std::size_t start, std::size_t end) constexpr {
        if (start == end || nodes[start].next != capacity || nodes[end].incoming != 0) {
            return false;
        }
        nodes[start].next = end;
        ++nodes[end].incoming;
        return true;
    };
    const auto midpoint = [&nodes](std::size_t a, std::size_t b) constexpr {
        return Point{nodes[a].point.x * 0.5 + nodes[b].point.x * 0.5,
                     nodes[a].point.y * 0.5 + nodes[b].point.y * 0.5};
    };
    for (const auto& edge : white_edges) {
        for (std::size_t part = 1; part < edge.size; ++part) {
            const auto a = edge.nodes[part - 1];
            const auto b = edge.nodes[part];
            bool covered = false;
            for (std::size_t black = 0; black < black_count; ++black) {
                covered = covered || inside_black(*blacks[black], midpoint(a, b));
            }
            if (!covered && !connect(a, b)) { return {}; }
        }
    }
    for (std::size_t black = 0; black < black_count; ++black) {
        for (const auto& edge : black_edges[black]) {
            for (std::size_t part = 1; part < edge.size; ++part) {
                const auto a = edge.nodes[part - 1];
                const auto b = edge.nodes[part];
                if (inside_white_surface(white, midpoint(a, b)) && !connect(b, a)) { return {}; }
            }
        }
    }
    for (std::size_t node = 0; node < node_count; ++node) {
        if ((nodes[node].next == capacity) != (nodes[node].incoming == 0)) { return {}; }
    }
    VisibleContours result{};
    for (std::size_t start = 0; start < node_count; ++start) {
        if (nodes[start].next == capacity || nodes[start].visited) { continue; }
        if (result.size == result.paths.size()) { return {}; }
        auto& path = result.paths[result.size++];
        auto current = start;
        do {
            if (current >= node_count || nodes[current].visited || path.size == path.points.size()) {
                return {};
            }
            path.points[path.size++] = nodes[current].point;
            nodes[current].visited = true;
            current = nodes[current].next;
        } while (current != start);
        if (path.size < 3) { return {}; }
    }
    return result;
}

[[nodiscard]] constexpr std::array<KeyHighlight, kKeyCount> make_key_highlights() noexcept {
    std::array<KeyHighlight, kKeyCount> highlights{};
    for (int key = 0; key < kKeyCount; ++key) {
        auto& highlight = highlights[static_cast<std::size_t>(key)];
        highlight.outline = key_polygon(key);
        highlight.stroke_outline = is_black_key(key);
        if (highlight.stroke_outline) {
            auto& path = highlight.visible_paths[0];
            path.size = highlight.outline.size;
            for (std::size_t vertex = 0; vertex < path.size; ++vertex) {
                path.points[vertex] = highlight.outline.points[vertex];
            }
            highlight.visible_path_count = 1;
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
        const auto visible = visible_white_contours(key);
        highlight.visible_paths = visible.paths;
        highlight.visible_path_count = visible.size;
    }
    return highlights;
}

inline constexpr auto kKeyHighlights = make_key_highlights();
inline constexpr KeyHighlight kEmptyHighlight{};

static_assert([] {
    for (const auto& highlight : kKeyHighlights) {
        if (highlight.visible_path_count == 0 ||
            highlight.visible_path_count > highlight.visible_paths.size()) { return false; }
    }
    return true;
}(), "photo geometry must have bounded, closed visible contours");

} // namespace highlight_detail

// Render ONLY visible_paths[0..visible_path_count) as closed, normally filled
// subpaths. The paths have disjoint interiors and never cross a black interior;
// they do not depend on coincident edges cancelling in a rasterizer. The legacy
// outline/cutouts remain available for geometry comparisons, not rendering.
// White contours are fill-only. Black keys retain their fill and outline.
// Shared mathematical boundaries retain photo_geometry's black-first ownership;
// a rasterizer still determines antialias coverage at actual exposed edges.
// Invalid keys are empty.
[[nodiscard]] inline constexpr const KeyHighlight& key_highlight(int key) noexcept {
    return key >= 0 && key < kKeyCount ?
        highlight_detail::kKeyHighlights[static_cast<std::size_t>(key)] :
        highlight_detail::kEmptyHighlight;
}

} // namespace rhodes::vst3::photo
