#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#ifndef SIM_BUILD_ID
#define SIM_BUILD_ID "development-unpinned"
#endif
namespace sandbox {
    // Every number here is an engineering assumption, NOT an RMUC rule.
    inline constexpr int WIDTH=12000, HEIGHT=7600, HZ=60, RADIUS=280;
    inline constexpr int MAX_HP=100, SPEED=55, PROJECTILE_SPEED=260;
    inline constexpr int DAMAGE=10, COOLDOWN=10, AMMO=120, DURATION=7200;
    inline constexpr int ABI_VERSION=1;
    struct Vec {
        int x {},y {};
    };
    struct Rect {
        int x1,y1,x2,y2;
    };
    inline constexpr std::array<Rect,3> WALLS {
        {
            {5250,2800,6750,4800}, {2600,1350,3800,1950}, {8200,5650,9400,6250}
        }
    };
    struct Action {
        int dx {},dy {},aim_x {},aim_y {},fire {};
    };
    struct Robot {
        int x {},y {},yaw {},hp {MAX_HP},ammo {AMMO},cooldown {},shots {},hits {};
    };
    struct Bullet {
        int id {},owner {},x {},y {},vx {},vy {},life {};
    };
    struct Event {
        int tick {},kind {},actor {},target {},amount {};
    };
    // 1 shot, 2 hit, 3 disabled, 4 cover
    struct State {
        int tick {},next_id {1},terminal {};
        std::array<Robot,2> robots {};
        std::vector<Bullet> bullets;
    };
    struct Frame {
        int tick {};
        std::array<Action,2> actions {};
        std::uint32_t hash {};
    };
    struct EngineOptions {
        int duration_ticks {DURATION};
        bool stop_on_disable {true};
        bool record_journal {true};
    };
    class Engine {
        public: State state;
        std::vector<Frame> journal;
        std::vector<Event> events;
        EngineOptions options;
        explicit Engine(EngineOptions configured = {}) : options(configured) {
            reset();
        }
        void reset();
        bool step(const std::array<Action,2>&);
        std::uint32_t hash() const;
        std::string json() const;
        std::string replay_json() const;
    };
    // Read-only omniscient sandbox observation; deliberately NOT a referee-equivalent sensor.
    struct Observation {
        int tick {},self {};
        std::array<Robot,2> robots;
    };
    Action scripted_policy(const Observation&);
    bool line_blocked(Vec a,Vec b);
    Vec direction(int yaw);
    int aim_direction(Vec from,Vec target);
    std::uint64_t isqrt(std::uint64_t);
}
