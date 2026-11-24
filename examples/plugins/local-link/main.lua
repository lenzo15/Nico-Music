-- Example .bmlink JSON: {"title":"试听","file":"伴奏.wav"} (UTF-8).
local utils = require 'mp.utils'
local id = mp.get_script_directory():match('([^/\\]+)[/\\]*$')
local prefix = 'bm-' .. id .. '://file:///'
local function resolve(target)
    -- The host validated the file URI. Decode percent escapes once; '+' is literal.
    local path = target:sub(#prefix + 1):gsub('%%(%x%x)', function(h)
        return string.char(tonumber(h, 16))
    end)
    local f = io.open(path, 'rb')
    if not f then return end
    local raw = f:read(65537)
    f:close()
    if not raw or #raw > 65536 then return end
    local entry = utils.parse_json(raw)
    if type(entry) ~= 'table' or type(entry.file) ~= 'string' or
        entry.file:find('[%c/\\:*?"<>|]') or not entry.file:lower():match('%.wav$') then return end
    local dir = utils.split_path(path)
    return utils.join_path(dir, entry.file), type(entry.title) == 'string' and entry.title or entry.file
end

mp.register_script_message('bm-metadata', function(token, target)
    if target:sub(1, #prefix) ~= prefix then return end
    local _, title = resolve(target)
    mp.commandv('script-message', 'bm-metadata-result', token, title or '', '')
end)

mp.add_hook('on_load', 10, function()
    local target = mp.get_property('stream-open-filename', '')
    if target:sub(1, #prefix) ~= prefix then return end
    local path, title = resolve(target)
    if path then
        mp.set_property('file-local-options/force-media-title', title)
        mp.set_property('stream-open-filename', path)
    end
end)
