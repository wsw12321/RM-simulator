#include "terrain.hpp"
#include <cmath>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace field_sim {
namespace {
const double kTiltTan = std::tan(1.5 * 3.14159265358979323846 / 180.0);
std::int32_t read_i32(const unsigned char* p) {
    return static_cast<std::int32_t>(std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24));
}
std::int16_t read_i16(const unsigned char* p) { return static_cast<std::int16_t>(std::uint16_t(p[0]) | (std::uint16_t(p[1]) << 8)); }
}

std::int16_t Terrain::to_mm(double z) {
    double mm = std::round(z * 1000.0);
    if (mm < -32000 || mm > 32000) throw std::runtime_error("terrain height out of range");
    return static_cast<std::int16_t>(mm);
}

Terrain Terrain::flat(int nx, int ny, double cell_m, double height_m) {
    if (nx <= 0 || ny <= 0 || cell_m <= 0) throw std::runtime_error("invalid flat terrain size");
    Terrain t;
    t.nx_ = nx; t.ny_ = ny; t.cell_ = cell_m;
    std::size_t n = static_cast<std::size_t>(nx) * static_cast<std::size_t>(ny);
    t.ground_.assign(n, to_mm(height_m));
    t.over_bottom_.assign(n, NONE);
    t.over_top_.assign(n, NONE);
    return t;
}

Terrain Terrain::load(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open terrain file: " + path);
    std::vector<unsigned char> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (raw.size() < 28 || std::memcmp(raw.data(), "RMTERRN1", 8) != 0) throw std::runtime_error("not a terrain file: " + path);
    int nx = read_i32(&raw[8]), ny = read_i32(&raw[12]), cell = read_i32(&raw[16]), count = read_i32(&raw[20]);
    if (nx <= 0 || ny <= 0 || nx > 8192 || ny > 8192 || cell <= 0 || cell > 1000 || count < 0) throw std::runtime_error("invalid terrain header");
    std::size_t n = static_cast<std::size_t>(nx) * static_cast<std::size_t>(ny);
    if (static_cast<std::size_t>(count) > n || raw.size() != 28 + n * 2 + static_cast<std::size_t>(count) * 8) throw std::runtime_error("terrain file size mismatch");
    Terrain t;
    t.nx_ = nx; t.ny_ = ny; t.cell_ = cell * 0.001;
    t.ground_.resize(n);
    t.over_bottom_.assign(n, NONE);
    t.over_top_.assign(n, NONE);
    const unsigned char* p = raw.data() + 28;
    for (std::size_t k = 0; k < n; ++k) t.ground_[k] = read_i16(p + 2 * k);
    const unsigned char* idx = p + 2 * n;
    const unsigned char* ob = idx + 4 * static_cast<std::size_t>(count);
    const unsigned char* ot = ob + 2 * static_cast<std::size_t>(count);
    for (int k = 0; k < count; ++k) {
        std::uint32_t cell_index = static_cast<std::uint32_t>(read_i32(idx + 4 * k));
        std::int16_t bottom = read_i16(ob + 2 * k), top = read_i16(ot + 2 * k);
        if (cell_index >= n || bottom == NONE || top < bottom || bottom < t.ground_[cell_index]) throw std::runtime_error("invalid overhang record");
        t.over_bottom_[cell_index] = bottom;
        t.over_top_[cell_index] = top;
    }
    return t;
}

double Terrain::floor_z(double y) const { return -std::fabs(y - width() * 0.5) * kTiltTan; }

double Terrain::surface(double x, double y, double z_ref) const {
    return cell_surface(static_cast<int>(std::floor(x / cell_)), static_cast<int>(std::floor(y / cell_)), z_ref);
}

double Terrain::headroom(double x, double y, double z_ref) const {
    std::size_t k = index(static_cast<int>(std::floor(x / cell_)), static_cast<int>(std::floor(y / cell_)));
    if (over_bottom_[k] == NONE || z_ref >= over_bottom_[k] * 0.001) return std::numeric_limits<double>::infinity();
    return (over_bottom_[k] - ground_[k]) * 0.001;
}

double Terrain::support(double x, double y, double z_ref) const {
    double fx = x / cell_ - 0.5, fy = y / cell_ - 0.5;
    int i = static_cast<int>(std::floor(fx)), j = static_cast<int>(std::floor(fy));
    double u = fx - i, v = fy - j;
    double a = cell_surface(i, j, z_ref), b = cell_surface(i + 1, j, z_ref);
    double c = cell_surface(i, j + 1, z_ref), d = cell_surface(i + 1, j + 1, z_ref);
    return (a * (1 - u) + b * u) * (1 - v) + (c * (1 - u) + d * u) * v;
}

bool Terrain::solid(double x, double y, double z) const {
    if (!inside(x, y)) return true;
    std::size_t k = index(static_cast<int>(std::floor(x / cell_)), static_cast<int>(std::floor(y / cell_)));
    if (z < ground_[k] * 0.001) return true;
    return over_bottom_[k] != NONE && z >= over_bottom_[k] * 0.001 && z <= over_top_[k] * 0.001;
}

double Terrain::segment_hit(const Vec3& a, const Vec3& b) const {
    const double clear = 2.0;
    if (!inside(a.x, a.y)) return 0.0;
    double dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
    int i = static_cast<int>(std::floor(a.x / cell_)), j = static_cast<int>(std::floor(a.y / cell_));
    int step_i = dx > 0 ? 1 : -1, step_j = dy > 0 ? 1 : -1;
    const double inf = std::numeric_limits<double>::infinity();
    double t_next_i = dx != 0 ? (((dx > 0 ? i + 1 : i) * cell_) - a.x) / dx : inf;
    double t_next_j = dy != 0 ? (((dy > 0 ? j + 1 : j) * cell_) - a.y) / dy : inf;
    double t_step_i = dx != 0 ? cell_ / std::fabs(dx) : inf, t_step_j = dy != 0 ? cell_ / std::fabs(dy) : inf;
    double t_in = 0.0;
    for (int guard = 0; guard < 2 * (nx_ + ny_) + 8; ++guard) {
        if (i < 0 || j < 0 || i >= nx_ || j >= ny_) return t_in;         // left the field: the fence
        double t_out = std::fmin(std::fmin(t_next_i, t_next_j), 1.0);
        double z_in = a.z + dz * t_in, z_out = a.z + dz * t_out;
        std::size_t k = static_cast<std::size_t>(j) * static_cast<std::size_t>(nx_) + static_cast<std::size_t>(i);
        double g = ground_[k] * 0.001;
        if (z_in < g) return t_in;
        if (z_out < g) return t_in + (z_in - g) / (z_in - z_out) * (t_out - t_in);
        if (over_bottom_[k] != NONE) {
            double lo = over_bottom_[k] * 0.001, hi = over_top_[k] * 0.001;
            if (z_in >= lo && z_in <= hi) return t_in;
            if (z_in < lo && z_out >= lo) return t_in + (lo - z_in) / (z_out - z_in) * (t_out - t_in);
            if (z_in > hi && z_out <= hi) return t_in + (z_in - hi) / (z_in - z_out) * (t_out - t_in);
        }
        if (t_out >= 1.0) return clear;
        if (t_next_i < t_next_j) { i += step_i; t_in = t_next_i; t_next_i += t_step_i; }
        else { j += step_j; t_in = t_next_j; t_next_j += t_step_j; }
    }
    return clear;
}
} // namespace field_sim
