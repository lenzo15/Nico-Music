-- Source API 2 adapter. All executables, extractor discovery and arguments
-- belong to this package. An absolute executable path never falls back to PATH.
local utils = require 'mp.utils'
local dir = mp.get_script_directory()
local prefix = 'bm-' .. dir:match('([^/\\]+)[/\\]*$') .. '://'
local exe = utils.join_path(dir, 'yt-dlp.exe')
local extractors = utils.join_path(dir, 'extractors')

-- Opt in for playback only, e.g. 'firefox' or 'firefox:<profile path>'.
-- An empty value leaves yt-dlp in anonymous mode.
local cookies_from_browser = ''

local function accepts(target) return target:sub(1, #prefix) == prefix end
local function http(url) return type(url) == 'string' and url:match('^https?://') end

local function resolve(target, playing, callback)
    local args = {exe, '--ignore-config', '--no-plugin-dirs', '--plugin-dirs', extractors,
        '--no-playlist', '--playlist-items', '1', '--simulate', '--socket-timeout', '8',
        '--retries', '0', '--extractor-retries', '0', '--encoding', 'utf-8',
        '-f', 'bestaudio/best', '-j'}
    if playing and cookies_from_browser ~= '' then
        args[#args + 1] = '--cookies-from-browser'
        args[#args + 1] = cookies_from_browser
    end
    args[#args + 1] = '--'
    args[#args + 1] = target
    mp.command_native_async({name='subprocess', args=args, playback_only=playing,
        capture_stdout=true, capture_size=8*1024*1024}, function(ok, res)
        local info = ok and res.status == 0 and utils.parse_json(res.stdout)
        callback(type(info) == 'table' and info or nil)
    end)
end

-- Enumerate stable video pages only. Playback continues to resolve each page separately.
mp.register_script_message('bm-collection', function(token, target)
    if not accepts(target) then return end
    local args = {exe, '--ignore-config', '--no-plugin-dirs', '--flat-playlist', '--yes-playlist',
        '--playlist-end', '501', '--socket-timeout', '8', '--retries', '0', '--extractor-retries', '0',
        '--encoding', 'utf-8', '-J', '--', target:sub(#prefix + 1)}
    mp.command_native_async({name='subprocess', args=args, playback_only=false,
        capture_stdout=true, capture_size=8*1024*1024}, function(ok, res)
        local function finish(rows, err)
            mp.commandv('script-message', 'bm-collection-result', token, rows or '', err or '')
        end
        local info = ok and res.status == 0 and utils.parse_json(res.stdout)
        local entries = type(info) == 'table' and info.entries
        if type(entries) ~= 'table' or #entries == 0 then
            return finish(nil, '无法读取 B 站合集，请检查链接、网络及访问权限')
        end
        if #entries > 500 then return finish(nil, '单个合集最多支持 500 首') end
        local rows = {}
        for _, e in ipairs(entries) do
            local page = type(e) == 'table' and (e.url or e.webpage_url)
            if not http(page) or not page:match('^https?://www%.bilibili%.com/video/') then
                return finish(nil, '合集包含无法识别的视频链接')
            end
            rows[#rows+1] = page .. '\t' .. (type(e.title) == 'string' and e.title or ''):gsub('[%c]', ' ')
        end
        finish(table.concat(rows, '\n'))
    end)
end)

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
