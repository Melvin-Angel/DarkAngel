#pragma once
#include <darkangel/party_inventory.hpp>
#include <darkangel/world_session.hpp>
#include <memory>

namespace darkangel {
// Gameplay adapter, not party-control traffic. Construct before session.attach().
// PartyInventory outlives this scene's bridge; avatar ownership remains in WorldSession.
class InventoryHostSession {
public:
    InventoryHostSession(WorldSession&,PartyInventory&);
    ~InventoryHostSession();
    void bind(std::uint64_t network,StableId profile);
    void unbind(std::uint64_t network);
    void pump();
private:struct Impl;std::unique_ptr<Impl> impl_;
};
class InventoryClientSession {
public:
    InventoryClientSession(WorldSession&,StableId avatar,StableId profile,std::shared_ptr<const ItemCatalog>);
    ~InventoryClientSession();
    // Once a baseline identifies the avatar, request a fresh authoritative view.
    void pump(std::uint64_t network);
    bool submit(InventoryRequest); // operation is assigned here; one in flight
    bool retry(); // identical request, same operation, through the same path
    const InventoryOwnerState* state()const;
    bool pending()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
}
