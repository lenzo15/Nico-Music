-- Source API 2 adapter. All executables, extractor discovery and arguments
-- belong to this package. An absolute executable path never falls back to PATH.
local utils = require 'mp.utils'
local dir = mp.get_script_directory()
local prefix = 'bm-' .. dir:match('([^/\\]+)[/\\]*$') .. '://'
local exe = utils.join_path(dir, 'yt-dlp.exe')
local extractors = utils.join_path(dir, 'extractors')
local function accepts(target) return target:sub(1, #prefix) == prefix end
local function http(url) return type(url) == 'string' and url:match('^https?://') end

local function resolve(target, playing, callback)
    local args = {exe, '--ignore-config', '--no-plugin-dirs', '--plugin-dirs', extractors,
        '--no-playlist', '--playlist-items', '1', '--simulate', '--socket-timeout', '8',
        '--retries', '0', '--extractor-retries', '0', '--encoding', 'utf-8',
        '-f', 'bestaudio/best', '-j', '--', target}
    mp.command_native_async({name='subprocess', args=args, playback_only=playing,
        capture_stdout=true, capture_size=8*1024*1024}, function(ok, res)
        local info = ok and res.status == 0 and utils.parse_json(res.stdout)
        callback(type(info) == 'table' and info or nil)
    end)
end

mp.register_script_message('bm-metadata', function(token, target)
    if not accepts(target) then return end
    resolve(target, false, function(info)
        mp.commandv('script-message', 'bm-metadata-result', token,
            info and info.title or '', info and info.thumbnail or '')
    end)
end)

mp.add_hook('on_load', 10, function(hook)
    local target = mp.get_property('stream-open-filename', '')
    if not accepts(target) then return end
    hook:defer()
    resolve(target, true, function(info)
        if info and http(info.url) then
            local headers = {}
            for k,v in pairs(info.http_headers or {}) do
                if type(v) == 'string' and not (k..v):find('[\r\n]') then
                    if k:lower() == 'user-agent' then mp.set_property('file-local-options/user-agent', v)
                    else headers[#headers+1] = k .. ': ' .. v end
                end
            end
            mp.set_property_native('file-local-options/http-header-fields', headers)
            if info.title then mp.set_property('file-local-options/force-media-title', info.title) end
            mp.set_property('stream-open-filename', info.url)
        end
        hook:cont() -- unresolved bm- URLs fail normally, never retry as webpages
    end)
end)
