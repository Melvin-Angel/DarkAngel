#include "native_authoring_test_support.hpp"

void run_native_authoring_graph_commands() {
    NativeAuthoringCommandFixture fixture;
    auto& assets = *fixture.service;
    const auto& source = fixture.source;
    const auto& root = fixture.root;
    const auto kit = fixture.kit, heavy = fixture.heavy, burn = fixture.burn, human = fixture.human;
    const auto& roots = fixture.roots;
    auto id = [&](const char* file) { return fixture.id(file); };

    {
        NativeAuthoring connecting;
        auto graph_id = AssetId::parse(connecting.open(assets, kit).value.at("locomotion_stance").get<std::string>());
        auto before = connecting.open(assets, graph_id).value;
        auto root = before.at("root").get<unsigned>();
        auto node = connecting.duplicate_graph_node(assets, graph_id, 1);
        auto count = connecting.history().size(), revision = connecting.revision();
        rejects_with([&] { connecting.connect_graph_input(assets, graph_id, root, 0, root); }, "cycle");
        rejects([&] { connecting.connect_graph_input(assets, graph_id, root, 99, node); });
        rejects([&] { connecting.connect_graph_input(assets, graph_id, root, 0, 9999); });
        check(connecting.history().size() == count && connecting.revision() == revision, "Rejected graph connection changed history");
        connecting.connect_graph_input(assets, graph_id, root, 0, node);
        auto edited = connecting.open(assets, graph_id).value;
        connecting.undo(assets);
        for (const auto& n : connecting.open(assets, graph_id).value.at("nodes"))
            if (n.at("id") == root)
                check(n.at("points").at(0).at("input") == 1, "Graph connection Undo lost original port");
        connecting.redo(assets);
        check(connecting.open(assets, graph_id).value == edited, "Graph connection Redo changed graph");
        connecting.remove_graph_node(assets, graph_id, 1);
        auto summary = connecting.validate(assets, graph_id, roots);
        check(summary.sources == 1, "Connected graph consumer closure not valid");
        auto clone = connecting.duplicate_graph_node(assets, graph_id, root);
        connecting.connect_graph_input(assets, graph_id, clone, 0, root);
        revision = connecting.revision();
        rejects_with([&] { connecting.connect_graph_input(assets, graph_id, node, 0, root); }, "port");
        rejects_with([&] { connecting.connect_graph_input(assets, graph_id, root, 0, clone); }, "cycle");
        check(connecting.revision() == revision, "Transitive cycle changed draft");
    }
}
