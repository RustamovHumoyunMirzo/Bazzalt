// Compile before the engine to catch Clang's member-property callback regression.
#include <sol/sol.hpp>
#include <string>
struct Probe { int Priority=0; float Volume=1; std::string Name; };
void BindProbe(sol::state& lua) {
    auto type=lua.new_usertype<Probe>("Probe",sol::constructors<Probe()>());
    type["Priority"]=&Probe::Priority;
    type["Volume"]=&Probe::Volume;
    type["Name"]=&Probe::Name;
}
