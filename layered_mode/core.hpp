#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <istream>

namespace layered {
using I = int64_t;
constexpr I Q = 1024;
struct Point { I x=0,y=0; };
using Polygon = std::vector<Point>;
struct Surface { std::string id,layer; I z=0,dx=0,dy=0,ceiling=0; bool has_ceiling=false,traversable=true; Polygon polygon; };
struct Volume { std::string id; I low=0,high=0; Polygon polygon; std::vector<int> scopes; };
struct Portal { std::string id; int from=0,to=0; Point a,b; bool bidirectional=false; int edge_from=-1,edge_to=-1; };
struct Config { I radius=120,height=400,speed=20,max_slope=512,max_ticks=36000; Point start; int surface=0; };
struct Map { std::string map_hash,config_hash; Polygon boundary; std::vector<Surface> surfaces; std::vector<Volume> solids,triggers; std::vector<Portal> portals; };
struct Contact { std::string kind,id; };
struct Event { I tick=0; int substep=0; std::string type,id,from,to; };
struct Frame { I tick,dx,dy; std::string hash; };
class Engine {
public:
    std::optional<std::string> load(std::istream&);
    std::optional<std::string> advance(I count,I dx,I dy,std::optional<I> frame_tick=std::nullopt);
    std::string json() const;
    std::string result_json() const;
    std::string hash() const;
    bool ready=false;
    Map map; Config config; Point position; int surface=0; I tick=0;
    std::vector<Contact> contacts;
    std::vector<Event> events;
    std::vector<Event> batch_events;
    std::vector<bool> active;
    std::vector<Frame> frames;
private:
    void step(I dx,I dy);
};
std::string build_id();
}
