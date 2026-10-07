--!strict
type State = { angle: number }
type Config = { speed: number }
return {
    update = function(ctx: Context, state: State, config: Config, dt: number)
        state.angle += config.speed * dt
        ctx:set_yaw(state.angle)
    end,
    migrate = function(previousVersion: number, oldState: State): State
        return { angle = oldState.angle }
    end,
}
