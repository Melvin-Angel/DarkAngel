#include <Luau/Compiler.h>
#include <lua.h>
#include <lualib.h>
#include <chrono>
#include <iostream>
#include <memory>
struct Budget { std::chrono::steady_clock::time_point deadline; };
static void interrupt(lua_State* state, int) {
    const auto* budget = static_cast<Budget*>(lua_callbacks(state)->userdata);
    if (std::chrono::steady_clock::now() > budget->deadline) luaL_error(state, "M0 execution budget exceeded");
}
int main() {
    std::unique_ptr<lua_State, decltype(&lua_close)> state(luaL_newstate(), lua_close);
    if (!state) { std::cerr << "Luau VM allocation failed\n"; return 1; }
    Budget budget{std::chrono::steady_clock::now() + std::chrono::seconds(1)};
    lua_callbacks(state.get())->userdata = &budget;
    lua_callbacks(state.get())->interrupt = interrupt;
    luaL_sandbox(state.get());
    const auto bytecode = Luau::compile("return 6 * 7");
    if (luau_load(state.get(), "=M0-probe", bytecode.data(), bytecode.size(), 0) != 0 || lua_pcall(state.get(), 0, 1, 0) != 0) {
        const char* error = lua_tostring(state.get(), -1);
        std::cerr << "Luau compile/execute failed: " << (error ? error : "no diagnostic") << '\n'; return 2;
    }
    if (!lua_isnumber(state.get(), -1) || lua_tonumber(state.get(), -1) != 42.0) {
        std::cerr << "Luau expected numeric result 42\n"; return 3;
    }
    std::cout << "Luau isolated VM passed\n";
}
