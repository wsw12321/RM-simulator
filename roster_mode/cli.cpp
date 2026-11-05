#include "core.hpp"
#include <charconv>
#include <iostream>
#include <limits>
#include <sstream>
namespace {
using roster::I;
bool done(std::istream& in){std::string extra;return !(in>>extra);}
bool integer(std::istream& in,I& value){std::string s;if(!(in>>s)||s.size()>21)return false;const auto r=std::from_chars(s.data(),s.data()+s.size(),value);return r.ec==std::errc{}&&r.ptr==s.data()+s.size();}
bool identifier(std::istream& in,std::string& value,std::size_t maximum=32){return bool(in>>value)&&!roster::validate_identifier(value,maximum);}
bool hash(std::istream& in,std::uint32_t& value){I n;if(!integer(in,n)||n<0||n>std::numeric_limits<std::uint32_t>::max())return false;value=std::uint32_t(n);return true;}
bool request(std::istream& in,roster::Request& r){I v,dx,dy,ax,ay,fire;if(!identifier(in,r.id)||!(in>>r.kind)||!integer(in,v)||!integer(in,dx)||!integer(in,dy)||!integer(in,ax)||!integer(in,ay)||!integer(in,fire))return false;r.action={v,dx,dy,ax,ay,fire};return true;}
bool robot(std::istream& in,roster::RosterEntry& r){auto& p=r.profile;I eligible;if(!identifier(in,r.id)||!integer(in,r.team)||!identifier(in,p.id)||!integer(in,p.max_hp)||!integer(in,p.ammo)||!integer(in,p.radius_mm)||!integer(in,p.speed_mm_tick)||!integer(in,p.projectile_speed_mm_tick)||!integer(in,p.projectile_damage)||!integer(in,p.cooldown_ticks)||!integer(in,p.sensor_range_mm)||!integer(in,p.heat_limit_tenths)||!integer(in,p.cooling_per_second)||!integer(in,r.spawn.x)||!integer(in,r.spawn.y)||!integer(in,r.spawn.yaw)||!integer(in,eligible)||eligible<0||eligible>1)return false;r.eligible=eligible!=0;return true;}
bool frame(std::istream& in,roster::Frame& f,std::size_t n){if(!integer(in,f.tick))return false;f.inputs.resize(n);for(auto& i:f.inputs){std::string controller;if(!identifier(in,i.id)||!(in>>i.kind>>controller)||controller.size()>64||!integer(in,i.sdk_version))return false;i.controller_id=controller=="-"?"":controller;for(auto& a:i.action)if(!integer(in,a))return false;if(!hash(in,i.observation_hash))return false;}return hash(in,f.hash);}
void error(const std::string& message){std::cout<<"{\"error\":\"";for(unsigned char c:message){if(c=='"'||c=='\\')std::cout<<'\\';if(c>=32)std::cout<<c;}std::cout<<"\"}"<<std::endl;}
bool bounded_line(std::string& line,bool& too_long){line.clear();too_long=false;char c;bool any=false;while(std::cin.get(c)){any=true;if(c=='\n')return true;if(line.size()<1048576)line.push_back(c);else too_long=true;}return any;}
}
int main(){
    roster::Engine engine;std::string line;bool too_long;
    while(bounded_line(line,too_long)){
        if(too_long){error("Native command exceeds1MiB.");continue;}
        std::istringstream in(line);std::string command;in>>command;
        try{
            if(command=="BUILD"&&done(in)){std::cout<<"{\"build\":\""<<roster::build_id()<<"\"}"<<std::endl;}
            else if(command=="META"&&done(in)){std::cout<<roster::meta_json()<<std::endl;}
            else if(command=="POLICY_META"&&done(in)){std::cout<<"{\"policy\":"<<roster::policy_metadata_json(roster::policy::descriptor())<<'}'<<std::endl;}
            else if(command=="EXPORT"&&done(in)){std::cout<<engine.replay_json()<<std::endl;}
            else if(command=="STATE"){
                std::string who;if(!identifier(in,who)||!done(in)){error("STATE requires one robot ID.");continue;}
                std::cout<<engine.json(who)<<std::endl;
            }else if(command=="RESET"){
                std::string who;I boundary,credit,damage,heat,threshold,n;
                if(!identifier(in,who)||!integer(in,boundary)||!integer(in,credit)||!integer(in,damage)||!integer(in,heat)||!integer(in,threshold)||!integer(in,n)||boundary<0||boundary>1||credit<0||credit>1||damage<0||damage>1||heat<0||heat>1||threshold<0||threshold>2||n<2||n>8){error("Invalid RESET configuration header.");continue;}
                roster::Config c;c.boundary=static_cast<roster::cap::BoundaryPolicy>(boundary);c.credits=credit!=0;c.damage_enabled=damage!=0;c.heat_enabled=heat!=0;c.heat_threshold=static_cast<roster::heat::ThresholdPolicy>(threshold);c.robots.resize(std::size_t(n));bool valid=true;for(auto& r:c.robots)if(!robot(in,r)){valid=false;break;}
                if(!valid||!done(in)){error("Invalid RESET robot fields.");continue;}
                roster::Engine candidate(c);const auto result=candidate.json(who);engine=std::move(candidate);std::cout<<result<<std::endl;
            }else if(command=="STEP"){
                I count,n;std::string who;if(!integer(in,count)||!identifier(in,who)||!integer(in,n)||n<2||n>8){error("Invalid STEP header.");continue;}
                engine.index(who);std::vector<roster::Request> requests(static_cast<std::size_t>(n));bool valid=true;for(auto& r:requests)if(!request(in,r)){valid=false;break;}
                if(!valid||!done(in)){error("Invalid STEP request fields.");continue;}
                std::vector<roster::Controller> controllers;for(const auto& r:requests)if(r.kind=="policy")controllers.push_back({r.id,roster::policy::descriptor(),roster::policy::decide});
                if(auto e=engine.advance(count,requests,controllers))error(*e);else std::cout<<engine.json(who,true)<<std::endl;
            }else if(command=="FRAMES"){
                I count,n;std::string who;if(!integer(in,count)||count<1||count>roster::kMaxBatch||!identifier(in,who)||!integer(in,n)||n<2||n>8){error("Invalid FRAMES header.");continue;}
                engine.index(who);std::vector<roster::Frame> frames(static_cast<std::size_t>(count));bool valid=true;for(auto& f:frames)if(!frame(in,f,std::size_t(n))){valid=false;break;}
                if(!valid||!done(in)){error("Invalid replay fields.");continue;}
                if(auto e=engine.replay_frames(frames))error(*e);else std::cout<<engine.json(who,true)<<std::endl;
            }else error("Unknown native command or trailing data.");
        }catch(const std::exception& e){error(e.what());}catch(...){error("Native request exception; batch unchanged.");}
    }
}
