#pragma once
#include <darkangel/ui_document.hpp>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace darkangel {
// R6.2 (U2): the client's UI facade. Authored documents are opened as screen layers (Player HUD elements: passive, under
// everything), modal screens (menus and puzzle panels, a stack: the top one owns keyboard/controller navigation) or world
// anchors (markers positioned each frame over an actor). Every open document is an independent instance with its own
// context and "ui" model, so two markers of the same document show different actors.
//
// Handles are generation-checked: a closed handle never reaches a later document in the same slot; closing is idempotent.
// Each document belongs to a scope (for example "hud", "actor:<id>", "object:<id>", "frontend"); closing a scope closes
// everything opened in it, which is how Stop, travel and actors leaving clean up.
//
// A document may carry a local Luau presentation script (its "script" member): a chunk returning a table with optional
// on_open(ui, handle) and on_command(ui, handle, command) functions. The ui object offers open(document, "modal"|"passive"),
// close(handle), set(handle, field, value) for its model (name, prompt, flag, health, stamina, cooldown) and request(kind,
// value), which asks the host application through the Bridge and returns whether it was sent; scripts never touch RmlUi or
// the world directly. A command the script handles (on_command returns true) is not passed on to C++.
struct UiDocumentSource {
    std::string markup,style,script,binding{"ui-v1"};
    std::vector<std::string> surfaces{"screen","world_anchor"};
    std::vector<UiFont> fonts;
    struct Image {std::string url;UiRuntime::Image image;};
    std::vector<Image> images;
};
using UiDocumentView=UiDocumentPreview;
class UiService {
public:
    enum class Input {Passive,Modal};
    struct Handle {std::uint32_t slot{},generation{};explicit operator bool()const{return generation!=0;}bool operator==(const Handle&)const=default;};
    using Loader=std::function<UiDocumentSource(std::string_view document)>;
    using Bridge=std::function<bool(std::string_view kind,std::string_view value)>;
    UiService(Rml::RenderInterface&,UiFont fallback,Loader,Bridge={});
    ~UiService();
    UiService(const UiService&)=delete;
    UiService& operator=(const UiService&)=delete;
    // A screen document: passive layers stay under modals; a modal joins the top of the stack and takes navigation.
    Handle open(std::string_view document,Input,std::string scope,int width,int height);
    // A world-anchor document, drawn where place() last put it.
    Handle attach(std::string_view document,std::string scope,int width,int height);
    void close(Handle);
    void close_scope(std::string_view scope);
    bool valid(Handle)const;
    // The instance's "ui" model values, applied by update(). Throws on a stale handle.
    UiPreviewSample& model(Handle);
    void place(Handle,float x,float y,bool visible);
    void resize_screens(int width,int height);
    // Keyboard/controller navigation: the top modal moves focus (Up/Down/Left/Right) and activates it (Confirm); Cancel closes
    // it and reports "ui.cancel". Without a modal nothing is consumed and gameplay keeps the input.
    bool key(UiKey);
    bool modal_open()const;
    std::optional<Handle> top_modal()const;
    void set_input(Handle,Input,bool close_on_cancel=true);
    void update_owner(Handle,const OwnerUiModel&);
    void update_inventory(Handle,const InventoryUiModel&);
    std::vector<InventoryUiAction> drain_inventory_actions(Handle);
    void update_altar(Handle,const AltarUiModel&);
    std::vector<AltarUiAction> drain_altar_actions(Handle);
    std::optional<UiBox> box(Handle,const std::string&)const;
    // Pointer input: the top modal when there is one, otherwise the screen layers. Coordinates are viewport pixels.
    void pointer(int x,int y,bool pressed,bool released,float wheel=0);
    struct Command {Handle handle;std::string document,command;};
    // Commands the documents' scripts did not handle, oldest first.
    std::vector<Command> drain_commands();
    std::vector<Command> drain_commands(Handle);
    // Applies models, runs scripts' callbacks for new commands and lays out every document.
    void update();
    // Draws every visible document: screen layers, then world anchors, then the modal stack (bottom to top).
    void render(const std::function<void(UiDocumentView&,float x,float y,float width,float height)>& draw);
    std::size_t screens()const;
    std::size_t anchors()const;
    std::size_t modals()const;
    std::string text(Handle,const std::string& id)const;
    // Load and script problems so far (bounded), for the console.
    std::string problems()const;
    struct Impl;
private:
    std::unique_ptr<Impl> impl_;
};
}
