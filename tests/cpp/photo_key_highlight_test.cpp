// Geometry checks for photo feedback; these do not establish native raster or
// antialiasing behavior. Hit regions remain the separate photo_geometry API.
#include "plugins/vst3/photo_key_highlight.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
namespace photo = rhodes::vst3::photo;
std::size_t checks = 0;

static_assert(photo::key_highlight(0).outline.size == 6);
static_assert(photo::key_highlight(2).outline.size == 4);
static_assert(photo::key_highlight(-1).outline.size == 0);
static_assert(noexcept(photo::key_highlight(0)));

void require(bool condition, const char* message) {
    ++checks;
    if (!condition) { throw std::runtime_error(message); }
}

enum class Location { outside, boundary, inside };
constexpr double kBoundaryTolerance = 1.0e-8;

struct Bounds {
    double left{};
    double top{};
    double right{};
    double bottom{};

    bool includes(photo::Point point) const {
        return point.x >= left - kBoundaryTolerance &&
               point.x <= right + kBoundaryTolerance &&
               point.y >= top - kBoundaryTolerance &&
               point.y <= bottom + kBoundaryTolerance;
    }
};

template <class Path>
Bounds bounds(const Path& path) {
    Bounds result{path.points[0].x, path.points[0].y,
                  path.points[0].x, path.points[0].y};
    for (std::size_t vertex = 1; vertex < path.size; ++vertex) {
        const auto point = path.points[vertex];
        result.left = std::min(result.left, point.x);
        result.right = std::max(result.right, point.x);
        result.top = std::min(result.top, point.y);
        result.bottom = std::max(result.bottom, point.y);
    }
    return result;
}

long double cross(photo::Point a, photo::Point b, photo::Point point) {
    return (static_cast<long double>(b.x) - a.x) *
               (static_cast<long double>(point.y) - a.y) -
           (static_cast<long double>(b.y) - a.y) *
               (static_cast<long double>(point.x) - a.x);
}

// Independent winding-number oracle, rather than the production clipper or
// photo_geometry's ray crossing. Explicit boundaries are tested separately.
template <class Path>
Location locate(const Path& path, const Bounds& extent, photo::Point point) {
    if (path.size < 3 || path.size > path.points.size() || !extent.includes(point)) {
        return Location::outside;
    }
    int winding = 0;
    for (std::size_t vertex = 0; vertex < path.size; ++vertex) {
        const auto a = path.points[vertex];
        const auto b = path.points[(vertex + 1) % path.size];
        const auto side = cross(a, b, point);
        const double length = std::hypot(b.x - a.x, b.y - a.y);
        if (std::abs(side) <= kBoundaryTolerance * length &&
            point.x >= std::min(a.x, b.x) - kBoundaryTolerance &&
            point.x <= std::max(a.x, b.x) + kBoundaryTolerance &&
            point.y >= std::min(a.y, b.y) - kBoundaryTolerance &&
            point.y <= std::max(a.y, b.y) + kBoundaryTolerance) {
            return Location::boundary;
        }
        if (a.y <= point.y && b.y > point.y && side > 0.0L) { ++winding; }
        if (a.y > point.y && b.y <= point.y && side < 0.0L) { --winding; }
    }
    return winding == 0 ? Location::outside : Location::inside;
}

struct Mask {
    const photo::KeyHighlight* shape{};
    Bounds outline{};
    std::array<Bounds, 2> cutouts{};
};

std::array<Mask, photo::kKeyCount> masks() {
    std::array<Mask, photo::kKeyCount> result{};
    for (int key = 0; key < photo::kKeyCount; ++key) {
        auto& mask = result[static_cast<std::size_t>(key)];
        mask.shape = &photo::key_highlight(key);
        mask.outline = bounds(mask.shape->outline);
        for (std::size_t cutout = 0; cutout < mask.shape->cutout_count; ++cutout) {
            mask.cutouts[cutout] = bounds(mask.shape->cutouts[cutout]);
        }
    }
    return result;
}

struct PaintSample { bool filled{}; bool boundary{}; };

PaintSample sample(const Mask& mask, photo::Point point) {
    const auto outside = locate(mask.shape->outline, mask.outline, point);
    PaintSample result{outside != Location::outside, outside == Location::boundary};
    // Evaluate every subpath even outside the outline: an untrimmed hole would
    // incorrectly add paint there under the renderer's requested even-odd fill.
    for (std::size_t hole = 0; hole < mask.shape->cutout_count; ++hole) {
        const auto location = locate(mask.shape->cutouts[hole], mask.cutouts[hole], point);
        if (location != Location::outside) { result.filled = !result.filled; }
        result.boundary = result.boundary || location == Location::boundary;
    }
    return result;
}

photo::Point blend(photo::Point a, photo::Point b, double fraction) {
    return {a.x + fraction * (b.x - a.x), a.y + fraction * (b.y - a.y)};
}

void structure() {
    std::size_t hole_count = 0;
    std::size_t white_count = 0;
    std::size_t black_count = 0;
    for (int key = 0; key < photo::kKeyCount; ++key) {
        const auto& highlight = photo::key_highlight(key);
        const auto& original = photo::key_polygon(key);
        require(highlight.outline.size == original.size, "original outline vertex count retained");
        for (std::size_t vertex = 0; vertex < original.points.size(); ++vertex) {
            require(highlight.outline.points[vertex].x == original.points[vertex].x &&
                        highlight.outline.points[vertex].y == original.points[vertex].y,
                    "highlight preserves exact original hit polygon coordinates");
        }
        require(highlight.cutout_count <= highlight.cutouts.size(), "at most two cutouts");
        if (photo::is_black_key(key)) {
            ++black_count;
            require(highlight.stroke_outline && highlight.cutout_count == 0,
                    "black keys retain their outline and have no cutouts");
        } else {
            ++white_count;
            require(!highlight.stroke_outline, "white outline cannot cross black interiors");
            const auto adjacent_blacks = static_cast<std::size_t>(photo::is_black_key(key - 1)) +
                                         static_cast<std::size_t>(photo::is_black_key(key + 1));
            require(highlight.cutout_count == adjacent_blacks, "all adjacent black overlaps cut out");
        }
        for (std::size_t hole = 0; hole < highlight.cutout_count; ++hole) {
            const auto& cutout = highlight.cutouts[hole];
            ++hole_count;
            require(cutout.size >= 3 && cutout.size <= cutout.points.size(),
                    "bounded nonempty cutout vertex count");
            long double area = 0.0L;
            bool inside_one_black = false;
            for (int black = 0; black < photo::kKeyCount; ++black) {
                if (!photo::is_black_key(black)) { continue; }
                const auto& black_polygon = photo::key_polygon(black);
                bool all_inside = true;
                for (std::size_t vertex = 0; vertex < cutout.size; ++vertex) {
                    all_inside = all_inside && locate(black_polygon, bounds(black_polygon),
                                                      cutout.points[vertex]) != Location::outside;
                }
                inside_one_black = inside_one_black || all_inside;
            }
            require(inside_one_black, "each whole cutout lies in one original black polygon");
            for (std::size_t vertex = 0; vertex < cutout.size; ++vertex) {
                const auto a = cutout.points[vertex];
                const auto b = cutout.points[(vertex + 1) % cutout.size];
                const auto c = cutout.points[(vertex + 2) % cutout.size];
                require(a.finite(), "cutout vertices are finite");
                require(locate(original, bounds(original), a) != Location::outside,
                        "cutout vertices remain inside original white outline");
                require(std::hypot(b.x - a.x, b.y - a.y) > 1.0e-8,
                        "cutout has no duplicate consecutive vertices");
                require(cross(a, b, c) >= -1.0e-8L, "cutout remains convex");
                area += static_cast<long double>(a.x) * b.y -
                        static_cast<long double>(a.y) * b.x;
            }
            require(area > 1.0e-8L, "cutout has positive nondegenerate area");
        }
    }
    require(white_count == 43 && black_count == 30 && hole_count == 60,
            "all 73 keys and all 60 neighboring overlaps covered");
}

void black_interiors() {
    const auto all_masks = masks();
    std::size_t original_center_overlaps = 0;
    for (int black = 0; black < photo::kKeyCount; ++black) {
        if (!photo::is_black_key(black)) { continue; }
        const auto& polygon = photo::key_polygon(black);
        const auto center = blend(blend(polygon.points[0], polygon.points[1], 0.5),
                                  blend(polygon.points[3], polygon.points[2], 0.5), 0.5);
        bool original_overlap = false;
        for (int white = 0; white < photo::kKeyCount; ++white) {
            if (!photo::is_black_key(white)) {
                original_overlap = original_overlap || photo::key_polygon(white).contains(center);
            }
        }
        original_center_overlaps += static_cast<std::size_t>(original_overlap);
        for (int row = 0; row < 16; ++row) {
            const double vertical = (static_cast<double>(row) + 0.5) / 16.0;
            const auto left = blend(polygon.points[0], polygon.points[3], vertical);
            const auto right = blend(polygon.points[1], polygon.points[2], vertical);
            for (int column = 0; column < 16; ++column) {
                const auto point = blend(left, right, (static_cast<double>(column) + 0.5) / 16.0);
                require(photo::hit_key(point.x, point.y) == black, "sample belongs to original black hit");
                require(sample(all_masks[static_cast<std::size_t>(black)], point).filled,
                        "black interior retains its own feedback");
                for (int white = 0; white < photo::kKeyCount; ++white) {
                    if (!photo::is_black_key(white)) {
                        require(!sample(all_masks[static_cast<std::size_t>(white)], point).filled,
                                "no white highlight paints any sampled black interior");
                    }
                }
            }
        }
    }
    require(original_center_overlaps == 30, "reproduce original overlap for all 30 black centers");
}

void dense_grid() {
    const auto all_masks = masks();
    std::array<std::size_t, photo::kKeyCount> visible_samples{};
    std::size_t checked_samples = 0;
    std::size_t boundary_samples = 0;
    // Subpixel offsets avoid making the dense interior oracle depend on the
    // separately tested tie rules. Include a margin outside the entire keyboard.
    for (int row = 0; row <= 148; ++row) {
        for (int column = 0; column <= 1350; ++column) {
            const photo::Point point{109.375 + column, 505.3125 + row * 0.75};
            int painted_key = -1;
            int painted_count = 0;
            bool boundary = false;
            for (int key = 0; key < photo::kKeyCount; ++key) {
                const auto paint = sample(all_masks[static_cast<std::size_t>(key)], point);
                boundary = boundary || paint.boundary;
                if (paint.filled) { ++painted_count; painted_key = key; }
            }
            if (boundary) { ++boundary_samples; continue; }
            const int hit = photo::hit_key(point.x, point.y);
            require(painted_count == (hit >= 0 ? 1 : 0), "dense grid has exactly the original hit coverage");
            require(painted_key == hit, "dense highlight owner matches original black-first hit_key");
            if (hit >= 0) { ++visible_samples[static_cast<std::size_t>(hit)]; }
            ++checked_samples;
        }
    }
    for (const auto count : visible_samples) {
        require(count >= 100, "dense grid samples every one of the 73 visible key regions");
    }
    require(checked_samples > 200000, "dense comparison retains over 200000 interior/outside samples");
    std::cout << "dense samples=" << checked_samples << " boundary samples=" << boundary_samples << '\n';
}

void boundaries() {
    const auto all_masks = masks();
    for (int key = 0; key < photo::kKeyCount; ++key) {
        const auto& polygon = photo::key_polygon(key);
        for (std::size_t edge = 0; edge < polygon.size; ++edge) {
            for (int step = 0; step <= 16; ++step) {
                const auto point = blend(polygon.points[edge], polygon.points[(edge + 1) % polygon.size],
                                         static_cast<double>(step) / 16.0);
                const int hit = photo::hit_key(point.x, point.y);
                if (photo::is_black_key(key)) {
                    require(hit == key, "original black boundary still wins hit testing");
                    for (int white = 0; white < photo::kKeyCount; ++white) {
                        if (!photo::is_black_key(white)) {
                            require(!sample(all_masks[static_cast<std::size_t>(white)], point).filled,
                                    "black vertices and edge samples excluded from all white masks");
                        }
                    }
                } else {
                    const bool covered_by_black = hit >= 0 && photo::is_black_key(hit);
                    require(sample(all_masks[static_cast<std::size_t>(key)], point).filled != covered_by_black,
                            "white boundaries retain exposed segments and exclude covered segments");
                }
            }
        }
    }
    require(sample(all_masks.front(), {113.0, 615.0}).filled,
            "lowest-key outer front corner retained");
    require(sample(all_masks.back(), {1457.0, 615.0}).filled,
            "highest-key outer front corner retained");
    require(photo::hit_key(143.0, 615.0) == 0, "original equal-color edge tie remains unchanged");
}

void invalid_indices() {
    for (const int key : std::array<int, 6>{{-1, -2, 73, 74,
             std::numeric_limits<int>::min(), std::numeric_limits<int>::max()}}) {
        const auto& highlight = photo::key_highlight(key);
        require(highlight.outline.size == 0 && highlight.cutout_count == 0 &&
                    !highlight.stroke_outline, "invalid index returns empty highlight");
        for (const auto& hole : highlight.cutouts) {
            require(hole.size == 0, "invalid highlight has no cutout subpaths");
        }
    }
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
        run("structure", structure);
        run("black_interiors", black_interiors);
        run("dense_grid", dense_grid);
        run("boundaries", boundaries);
        run("invalid_indices", invalid_indices);
        if (!found) { throw std::runtime_error("unknown fixture: " + std::string{selected}); }
        std::cout << "PASS total checks=" << checks << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
