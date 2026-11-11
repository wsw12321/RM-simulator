#pragma once
// Layered height field of the battlefield (see field/rmuc2027_v1 and
// tools/field_extract). Lengths are metres here; the file stores millimetres.
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace field_sim {
struct Vec3 { double x = 0, y = 0, z = 0; };

class Terrain {
public:
    // Throws std::runtime_error on a missing, truncated or inconsistent file.
    static Terrain load(const std::string& path);
    // A flat test field without a file (used by unit tests).
    static Terrain flat(int nx, int ny, double cell_m, double height_m = 0.0);

    int nx() const { return nx_; }
    int ny() const { return ny_; }
    double cell() const { return cell_; }
    double length() const { return nx_ * cell_; }
    double width() const { return ny_ * cell_; }
    bool inside(double x, double y) const { return x >= 0 && y >= 0 && x < length() && y < width(); }

    // Cell access (indices are clamped to the grid).
    double ground(int i, int j) const { return ground_[index(i, j)] * 0.001; }
    bool has_overhang(int i, int j) const { return over_bottom_[index(i, j)] != NONE; }
    double overhang_bottom(int i, int j) const { return over_bottom_[index(i, j)] * 0.001; }
    double overhang_top(int i, int j) const { return over_top_[index(i, j)] * 0.001; }
    void set_ground(int i, int j, double z) { ground_[index(i, j)] = to_mm(z); }
    void set_overhang(int i, int j, double bottom, double top) { over_bottom_[index(i, j)] = to_mm(bottom); over_top_[index(i, j)] = to_mm(top); }

    // The surface under a body at (x, y): the overhang top when z_ref is at or
    // above the overhang underside, the ground otherwise. Nearest cell.
    double surface(double x, double y, double z_ref) const;
    // Free height between that surface and an overhang above it (infinity when none).
    double headroom(double x, double y, double z_ref) const;
    // surface() interpolated between the four nearest cell centres.
    double support(double x, double y, double z_ref) const;
    // True when the point is inside terrain material or outside the field.
    bool solid(double x, double y, double z) const;
    // First contact of the segment a->b with terrain or the field boundary, as a
    // parameter in [0, 1]; a value above 1 means the segment is clear.
    double segment_hit(const Vec3& a, const Vec3& b) const;
    bool visible(const Vec3& a, const Vec3& b) const { return segment_hit(a, b) > 1.0; }
    // Nominal floor: two planes falling from the long centre line.
    double floor_z(double y) const;

private:
    static constexpr std::int16_t NONE = std::numeric_limits<std::int16_t>::min();
    int nx_ = 0, ny_ = 0;
    double cell_ = 0.02;
    std::vector<std::int16_t> ground_, over_bottom_, over_top_;
    std::size_t index(int i, int j) const {
        if (i < 0) i = 0; else if (i >= nx_) i = nx_ - 1;
        if (j < 0) j = 0; else if (j >= ny_) j = ny_ - 1;
        return static_cast<std::size_t>(j) * static_cast<std::size_t>(nx_) + static_cast<std::size_t>(i);
    }
    static std::int16_t to_mm(double z);
    double cell_surface(int i, int j, double z_ref) const {
        std::size_t k = index(i, j);
        return (over_bottom_[k] != NONE && z_ref >= over_bottom_[k] * 0.001) ? over_top_[k] * 0.001 : ground_[k] * 0.001;
    }
};
} // namespace field_sim
