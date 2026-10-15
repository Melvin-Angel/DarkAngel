#include "editor_app.hpp"
#include <exception>
#include <iostream>

// The editor's entry point: the command line goes to EditorApp, which owns the services and runs the frame loop.
int main(int argc, char** argv) {
    try {
        darkangel::editor_app::EditorApp app(darkangel::editor_app::parse_editor_options(argc, argv));
        return app.run();
    } catch (const std::exception& e) {
        std::cerr << "DarkAngelEditor: " << e.what() << '\n';
        return 1;
    }
}
