#include "sim.hpp"
// Optional Emscripten C ABI scaffold. No WebAssembly binary was built in this
// environment. Native transport is the tested browser path in prototype 0.1.
static sandbox::Engine engine;
static std::string output;
extern "C" {
    void sandbox_reset() {
        engine.reset();
    }
    const char* sandbox_state_json() {
        output=engine.json();
        return output.c_str();
    }
    const char* sandbox_replay_json() {
        output=engine.replay_json();
        return output.c_str();
    }
    int sandbox_frame(int tick,int dx0,int dy0,int ax0,int ay0,int fire0,int dx1,int dy1,int ax1,int ay1,int fire1) {
        if(tick!=engine.state.tick)return 0;
        return engine.step( {
            {
                {dx0,dy0,ax0,ay0,fire0}, {dx1,dy1,ax1,ay1,fire1}
            }
        })?1:0;
    }
}
