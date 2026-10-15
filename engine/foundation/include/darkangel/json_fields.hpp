#pragma once
#include <darkangel/check.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <string>
#include <utility>

namespace darkangel::json_fields {
using Json = nlohmann::json;
inline void known(const Json& value, std::initializer_list<const char*> fields, const char* error) {
    for (const auto& [key, field] : value.items())
        require(std::any_of(fields.begin(), fields.end(), [&](const char* name) { return key == name; }), error);
}
inline std::string text(const Json& value, const char* key, std::string fallback, std::size_t limit, const char* error) {
    if (!value.contains(key))
        return fallback;
    require(value.at(key).is_string(), error);
    auto result = value.at(key).get<std::string>();
    require(!result.empty() && result.size() <= limit, error);
    return result;
}
inline double number(const Json& value, const char* key, double fallback, double low, double high, const char* error) {
    if (!value.contains(key))
        return fallback;
    require(value.at(key).is_number(), error);
    const auto result = value.at(key).get<double>();
    require(std::isfinite(result) && result >= low && result <= high, error);
    return result;
}
inline bool flag(const Json& value, const char* key, bool fallback, const char* error) {
    if (!value.contains(key))
        return fallback;
    require(value.at(key).is_boolean(), error);
    return value.at(key).get<bool>();
}
} // namespace darkangel::json_fields
