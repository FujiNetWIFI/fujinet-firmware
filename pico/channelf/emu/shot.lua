-- Snapshot the screen after SHOT_FRAMES frames, then exit a few frames later.
-- The exit must not share a frame with the snapshot or the file never flushes.
local target = tonumber(os.getenv("SHOT_FRAMES") or "180")
local n = 0
_G._shot_token = emu.add_machine_frame_notifier(function()
    n = n + 1
    if n == target then
        manager.machine.video:snapshot()
    elseif n == target + 15 then
        manager.machine:exit()
    end
end)
