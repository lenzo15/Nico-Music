-- Offline example: a source page can query or change resolver settings.
local utils = require 'mp.utils'
local id = mp.get_script_directory():match('([^/\\]+)[/\\]*$')
local prefix = 'bm-' .. id .. '://file:///'
local signed_in = false
local entry_id

mp.register_event('start-file', function(event)
    entry_id = tostring(event.playlist_entry_id)
end)

mp.register_script_message('bm-source-page', function(source, token, action)
    if source ~= id then return end
    local err = ''
    if action == 'toggle-demo' then signed_in = not signed_in
    elseif action ~= 'status' then err = '此插件不支持该设置操作' end
    mp.commandv('script-message', 'bm-source-page-result', token,
        utils.format_json({signedIn = signed_in}), err)
end)

mp.add_hook('on_load', 10, function()
    local target = mp.get_property('stream-open-filename', '')
    if target:sub(1, #prefix) ~= prefix then return end
    local function fail(code)
        if entry_id then mp.commandv('script-message', 'bm-playback-error', entry_id, target, code, '') end
    end
    if not signed_in then return fail('auth_required') end
    local path = target:sub(#prefix + 1):gsub('%%(%x%x)', function(hex)
        return string.char(tonumber(hex, 16))
    end)
    local f = io.open(path, 'rb')
    if not f then return fail('unavailable') end
    local raw = f:read(4097)
    f:close()
    if not raw or #raw > 4096 then return fail('resolve_failed') end
    local entry = utils.parse_json(raw)
    if type(entry) ~= 'table' or type(entry.file) ~= 'string' or
        entry.file:find('[%c/\\:*?"<>|]') or not entry.file:lower():match('%.wav$') then return fail('resolve_failed') end
    local folder = utils.split_path(path)
    mp.set_property('stream-open-filename', utils.join_path(folder, entry.file))
end)
