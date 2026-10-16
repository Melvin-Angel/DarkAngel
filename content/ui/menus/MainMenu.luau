return {
    on_open = function(ui, handle) ui:set(handle, "prompt", "Choose Start to enter the scene") end,
    on_command = function(ui, handle, command)
        if command == "menu.start" then return ui:request("play") end
        return false
    end,
}
