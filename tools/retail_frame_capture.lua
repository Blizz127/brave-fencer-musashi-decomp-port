-- Capture one framebuffer from a normal retail interpreter boot.  The startup
-- loop directly calls 800596F4 (DrawSync dispatch) once after its render work;
-- delaying thirty such calls avoids taking the first partially initialized
-- frame. Visual inspection determines whether this is menu evidence. The
-- script writes outside the repository and changes no guest state.
local capture_after_hits = 30
local hit_count = 0
local output_path = '/tmp/musashi-retail-frame.ppm'

local function capture_ppm()
    local screenshot = PCSX.GPU.takeScreenShot()
    assert(screenshot.width > 0 and screenshot.height > 0, 'empty framebuffer')
    local expected_bytes = screenshot.width * screenshot.height * (screenshot.bpp == 0 and 2 or 3)
    assert(#screenshot.data == expected_bytes, 'unexpected framebuffer size')

    local output = assert(io.open(output_path, 'wb'))
    output:write(string.format('P6\n%d %d\n255\n', screenshot.width, screenshot.height))
    if screenshot.bpp == 0 then
        for index = 0, #screenshot.data - 1, 2 do
            local pixel = screenshot.data[index] + screenshot.data[index + 1] * 256
            output:write(string.char(
                math.floor((math.floor(pixel / 1024) % 32) * 255 / 31),
                math.floor((math.floor(pixel / 32) % 32) * 255 / 31),
                math.floor((pixel % 32) * 255 / 31)))
        end
    else
        output:write(tostring(screenshot.data))
    end
    output:close()
    print(string.format('MUSASHI_RETAIL_FRAME_CAPTURE path=%s width=%d height=%d bpp=%d drawsync_hits=%d cycles=%s',
                        output_path, screenshot.width, screenshot.height, screenshot.bpp,
                        hit_count, tostring(PCSX.getCPUCycles())))
end

musashi_frame_capture_bp = PCSX.addBreakpoint(0x800596f4, 'Exec', 4, 'retail DrawSync frame capture', function()
    hit_count = hit_count + 1
    if hit_count == capture_after_hits then
        capture_ppm()
        PCSX.quit(0)
    end
    return false
end)

print(string.format('MUSASHI_RETAIL_FRAME_CAPTURE_ARMED target=800596f4 capture_after_hits=%d',
                    capture_after_hits))
