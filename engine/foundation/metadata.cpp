#include "metadata.hpp"
#include <charconv>
#include <iomanip>
#include <sstream>
#include <stdexcept>
namespace darkangel {
std::span<const TypeMetadata> metadata() { return schema::types; }
std::string StableId::text() const {
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << high << std::setw(16) << low;
    return out.str();
}
StableId StableId::parse(std::string_view text) {
    if (text.size()!=32 || text.find_first_not_of("0123456789abcdef")!=text.npos) throw std::runtime_error("Expected canonical 128-bit stable ID");
    StableId id;
    auto a=std::from_chars(text.data(),text.data()+16,id.high,16);
    auto b=std::from_chars(text.data()+16,text.data()+32,id.low,16);
    if (a.ec!=std::errc{} || b.ec!=std::errc{} || !id) throw std::runtime_error("Invalid or zero stable ID");
    return id;
}
std::string luau_types() {
    std::string out="--!strict\n-- Generated from DarkAngel schema; exact IDs are strings.\n";
    for (const auto& type : metadata()) {
        out+="export type "+std::string(type.name)+" = {\n";
        for (const auto& p : type.properties) if (p.flags&ScriptVisible)
            out+="    "+std::string(p.name)+": "+(p.kind==ValueKind::Number ? "number" : "string")+",\n";
        out+="}\n";
    }
    out+="export type Context = { get_yaw: (Context) -> number, set_yaw: (Context, number) -> (), wait_event: (Context, string) -> (), wait_seconds: (Context, number) -> () }\n";
    return out;
}
}
