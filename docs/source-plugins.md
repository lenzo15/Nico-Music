# 音乐来源插件（API 2）

桌面版和 CLI 只负责按网页域名或本地文件扩展名分派原链接、播放媒体和管理歌单。**播放器不附带、不下载、不在 PATH 查找网页解析器**，libmpv 内置的 ytdl hook 已禁用。每个来源选择并携带自己的解析工具，也可以仅使用 Lua，不依赖 yt-dlp。只安装可信插件：插件以当前用户权限执行。

## 安装、卸载与升级

将完整包放在 **EXE 同目录** 的 `plugins/<id>/`。桌面版自动识别安装、修改和移除；CLI 在下一条命令前刷新。建议在 `plugins/` 外组装完整包再移入。仅修改音源的 `page/` 下的图标、HTML、JS 或 CSS 时，桌面只刷新该音源的资源，CLI 不作处理。解析脚本、依赖或清单变动会重新加载 mpv Lua 引擎，取消待确认的合集；仍可播放的当前曲目会重新打开，可定位的媒体会尝试恢复进度。移走整个目录即可卸载，已保存的歌单不会被删除。没有匹配的插件时，网页不能添加或播放，原有网页曲目在列表中隐藏，不会参与连播；恢复插件后即可重新显示。明确带媒体文件后缀的 HTTP(S) 直链无需插件。

示例网页音源包结构（目录 id 为小写 ASCII 字母、数字、连字符，最多 64 字符）：

```text
plugins/webradio/
    VERSION                          # 整个包的发布版本，例如 1.0.0；不是 api 号
    source.ini
    main.lua                         # mpv Lua 插件入口
    yt-dlp.exe
    extractors/site/yt_dlp_plugins/extractor/bilimusic_webradio.py
```

`source.ini` 是 UTF-8 的 `key=value` 清单，支持整行 `#` 注释：

```ini
api=2
name=示例电台
hosts=radio.example
home=https://radio.example/
search=https://radio.example/search?q={query}
requires=yt-dlp.exe
```

`hosts` 填裸域名（不含端口），匹配该域名及子域名；最具体的匹配优先，同等匹配按 id 排序。`search` 可省略。`requires` 是可选的插件内文件名，不能是路径。缺少 `main.lua` 或所声明的依赖时不注册该来源，不借用别的插件、安装目录或系统中的同名程序。API 1 的插件需升级，不能只改版本号。

源码仓库忽略 `.exe`。从源码组装发行包时，请按插件自身的说明配齐并校验私有工具。安装器不由播放器自动运行。发行和更新时复制整个插件包，保留上游许可。`examples/plugins/webradio` 使用相同的适配器，也需要**它自己的** `yt-dlp.exe`。

## 本地文件链接

在 API 2 清单里用可选的 `file_extensions` 声明要接管的本地文件类型。纯本地来源不需要虚构网站；同一插件也可同时声明 `hosts`、`home` 和 `file_extensions`。

```ini
api=2
name=本地音乐链接
file_extensions=.bmlink
```

- 扩展名包含前导点，以逗号分隔，不区分大小写（例如 `.abc,.xyz`，不支持通配符）。同一扩展名由按 id 排序的第一个完整插件处理。匹配插件优先；无匹配时，MP3、WAV 等内置格式仍可直接播放，其他格式不会送进播放引擎猜测。
- 桌面文件选择器会列出插件声明的格式。拖放、选择和粘贴本机绝对路径都转为同一 `file:///C:/...` URI；CLI 也可 `add file:///C:/Music/My%20Song.bmlink`。只接收实际存在的普通文件，不接收目录或 UNC 链接。
- 插件收到 `bm-<id>://file:///C:/...`，播放和元数据沿用下方协议。读取文件时去除前缀，按 UTF-8 解码百分号转义一次；不要把 `+` 当空格。歌单保留原 URI，不保存解析结果。卸载插件后，自定义格式条目仍保留，但连播时会跳过。
- [可运行示例](../examples/plugins/local-link/)：将示例目录复制到 EXE 旁的 `plugins/local-link/`。新建 UTF-8 的 `试听.bmlink`，内容为 `{"title":"试听","file":"伴奏.wav"}`，与 `伴奏.wav` 放在同一目录。拖入 `.bmlink` 后应显示「试听」，播放的是同目录的 WAV。示例只在该目录内解析 WAV 文件名，不需要额外工具。

## 侧栏入口与内嵌设置页（桌面，可选）

```ini
sidebar=page
icon=icon.svg
```

- `sidebar=external`（默认）：点击后用系统浏览器打开 `home`，与旧版 B 站来源一致。`sidebar=page`：点击后在右侧主区域打开 `page/index.html`，不替换底部播放栏；有 `home` 时，页头仍提供浏览器入口。入口文件丢失时回退到外部主页（若有）。本地文件来源也可提供内嵌页，不必填写 `home`。
- `icon` 是 `page/` 内的文件名，支持 `.svg`、`.png`、`.webp`，也可用于外跳入口。用 ASCII 字母、数字、点、下划线和连字符；缺失或加载失败回退到宿主的地球图标。SVG 作为图片加载，不作为 HTML 注入。
- 内嵌页通过沙箱 iframe 加载，不共享播放器的 DOM、浏览器存储或 WebView2 原生桥；使用插件包内的相对 CSS、JS、图片。页面不用于嵌入第三方登录站点。需要联网、持久化配置或正式授权时，交给本插件的 Lua/私有程序；按平台规则在系统浏览器完成授权，不把密码、令牌发回页面或写进日志。持久化用户数据不要放进被热重载监视的插件安装目录。
- 页面发送 `parent.postMessage({type: "source-request", id: "1", action: "status", data: null}, "https://studio.bilimusic.local")`。`id` 是页面自己的关联 ID（最多 64 字符），`action` 为小写字母、数字、`_`、`-`（1–64 字符），`data` 为可 JSON 序列化的数据（最多 16 KiB）。同时只允许一个请求。
- 宿主按当前 iframe 决定来源，发送 `script-message bm-source-page <来源 id> <token> <action> <JSON data>`。Lua 必须先比较自己的 id，再按白名单处理 action，校验参数；用 `script-message bm-source-page-result <token> <JSON data 或空串> <error 或空串>` 回答一次。宿主会把响应发回同一页：`{type:"source-response", id, data, error}`。页面只接收 `event.source === parent` 且 `event.origin === "https://studio.bilimusic.local"` 的响应，检查 id 并自行 `JSON.parse(data)`。超时时返回错误；离开页面后不再转发旧响应。
- 桌面来源页还可发送 `{type:"source-profile", url}` 设置页头外链（`null` 恢复默认），或 `{type:"source-collection", url, name}` 请求预览歌单，新建同名本地列表。消息使用相同的父窗口及目标 origin；仅接受当前 iframe。只支持属于当前插件的 HTTPS 链接，歌单路径还须在 `collection_paths` 内。主页须由用户点击宿主页头按钮；歌单须完成标准合集确认，不接受页面直接写入曲目。
- [离线示例](../examples/plugins/source-page/) 演示状态查询、模拟登录及播放报错，不涉及真实账户。复制到 EXE 同目录的 `plugins/source-page/`，在 WAV 旁新建 `.bmsource` 文件，写入 `{"file":"伴奏.wav"}` 后添加到音乐库。构建时开启 `-DNICOMUSIC_SOURCE_TESTS=ON` 后，可运行 `NicoMusicSourceTest <libmpv-2.dll 所在目录> <源码根目录>` 验证资源分派和真实 Lua 消息往返。

## 自定义配置页（桌面，可选）

音源和专注预设都可以提供 `page/settings.html`。宿主自动在「设置 → 插件」对应条目下显示「插件设置」，无需添加侧栏入口，也不用修改宿主。双类型插件共享一页；没有该文件则不显示按钮。页面在独立沙箱中打开，资源、CSP 和消息来源校验规则与侧栏内嵌页相同。仅修改 `page/` 不重启播放；正在使用的配置页若已更新或被移除会关闭，下次打开读取新内容。

- 音源设置页沿用上文的 `source-request` / `source-response` 调用本插件 Lua，由插件校验并持久化参数；不能发起侧栏导航或导入。普通配置和登录凭据应分开：不要在 HTML 配置页读取或保存 Cookie，使用下方掩码表单。
- 专注预设设置页使用 `parent.postMessage({type:"preset-read", id:"1"}, "https://studio.bilimusic.local")` 读取，或用 `type:"preset-write"` 和 `data`（最多 16 KiB 的 JSON）保存非敏感偏好。检查父窗口和 origin 后读取 `{type:"preset-response", id, data, error?}`；未保存时 `data` 为 `null`。宿主按插件 ID 隔离，预设 `mountFocus` 的上下文可调用 `getSettings()` 读取最新值。

## 登录凭据设置（桌面，可选）

插件可在 `source.ini` 声明登录凭据表单。宿主在「设置 → 插件」的对应插件条目内提供入口，并用统一弹窗渲染。它与 `sidebar` 独立，无需插件提供 HTML 或修改播放器：

```ini
credential_helper=example.exe
credential_title=示例音源 · 登录凭据
credential_help=粘贴在该音源的官方网页取得的登录凭据。
credential_label=登录凭据
credential_placeholder=粘贴凭据
```

- `credential_helper` 是插件根目录下现有的普通 `.exe` 文件名，不接受路径，不从 PATH 搜索。文件缺失或无效时只禁用设置入口，不影响来源注册。其余四项为可选纯文字，长度上限分别为 160、800、160、240 个 UTF-8 字节，超限或含控制字符时使用宿主默认值。`credential_help` 可使用字面量 `\n` 换行。
- 助手仅处理三个固定参数：`credential-status` 查询本地状态，`credential-save` 从标准输入读取单行 UTF-8（最多 16 KiB，EOF 结束，不保证有换行），`credential-clear` 清除本地凭据。状态和清除没有输入。退出码：`20` 已保存（查询或保存成功）、`21` 未保存（查询或清除成功）、`22` 输入无效、`23` 处理失败。命令应在 5 秒内完成，不启动窗口。
- 宿主使用掩码输入框，只把用户提交的凭据从本机管道交给已选音源的助手。助手负责验证、按当前用户加密保存及删除凭据，不打印凭据，不使用明文文件，不在状态查询中联网；不要把它放进命令行、日志或插件页面。宿主不会读取或回显已保存的凭据。插件仍以当前用户权限运行，只安装可信插件。

## 插件协议

- 宿主只加载已注册来源的 mpv 脚本包。播放器向插件传递 `bm-<id>://<原始 HTTP(S) 或 file URI>`，包括分 P 和查询参数。歌单内只保存原链接，不保存带时效的 CDN 地址。
- 播放或连播到该条目时，`main.lua` 的 `on_load` 钩子识别**自己的**前缀，解析它并设置 `stream-open-filename`，必要时设置文件局部的 `http-header-fields`、`user-agent` 和 `force-media-title`。不要改变其他来源的链接。对长耗时操作使用异步子进程并在结束时继续钩子。
- 后台查标题、封面时宿主发送 `script-message bm-metadata <token> <带前缀的链接>`。对应插件异步应答 `script-message bm-metadata-result <token> <标题> <封面 URL>`；失败可回两个空串。宿主匹配 token，超时放弃该次请求，不阻塞其他来源。
- 示例电台适配器用 `mp.get_script_directory()` 定位本包的可执行文件与 `extractors/`，以参数数组调用私有工具，不使用 shell、不读取 yt-dlp 用户配置，只加载本包的提取器。插件自行选择 `bestaudio/best` 和 HTTP(S) 媒体流。

### 结构化播放错误与取消（可选）

- 在 Lua 的 `start-file` 事件记录 `tostring(event.playlist_entry_id)`；在自己的 `on_load` 钩子中捕获该 ID 和未改写的目标 URL。解析确实失败时，在 `hook:cont()` 前发送 `script-message bm-playback-error <entry-id> <原始 bm-目标> <code> <message>`，保留原目标让 mpv 正常结束该次加载。`message` 可为空（用宿主提示），自定义文本上限 512 字节，只提供面向用户的信息。
- 支持 `auth_required`、`unavailable`、`network`、`timeout`、`resolve_failed`。宿主只在对应条目加载失败时使用该错误，成功加载或取消后不显示。桌面在 `auth_required` 时优先提供已声明的「配置登录」入口，否则在该来源有内嵌页时提供「打开来源」；CLI 输出文本。未采用新接口的旧插件继续使用通用播放错误。
- 原生手动切歌或停止时，宿主会向**已观察到的在加载条目**发送 `script-message bm-playback-cancel <entry-id>`。只取消符合该 ID 的本插件任务，可用 `mp.abort_async_command(request_id)` 并标记已取消。仍须使用 `mp.command_native_async({name='subprocess', ..., playback_only=true}, callback)` 绑定 mpv 播放生命周期，覆盖快速切歌、引擎销毁等场景；取消通知不是唯一保证。回调应忽略已取消任务，不写临时播放地址、不报告失败，并始终恰好调用一次 `hook:cont()`。后台元数据或合集任务不属于播放解析，不受此消息影响。
- 测试：`python tests/source_playback.py build/Release/NicoMusicCLI.exe` 使用本地音频和慢解析夹具验证结构化错误、切歌取消及继续播放。

### 一个链接导入多首歌（可选）

在 API 2 清单中声明 `collection_paths=/lists/,/channel/collectiondetail`。仅对本来源的 HTTP(S) 链接检查路径（不含查询和片段），按路径段匹配；不声明时仍按单曲处理。单页网站可额外声明 `#/playlist` 这样的路由，只在站点根页的 `#/` 片段中匹配（忽略路由查询参数），不会把普通锚点或查询参数中的链接当作合集。具体支持的合集链接由插件定义。请复制列表页而不是其中某一集的视频页。

- 命中后宿主发送 `script-message bm-collection <token> <带前缀的合集链接>`。只处理属于本来源的前缀。
- 返回一次 `script-message bm-collection-result <token> <rows> <error>`。成功时 error 为空，rows 是一个字符串参数：每行 `原始 HTTP(S) 曲目链接<TAB>标题`（标题可为空），行间用 LF 分隔。不要返回临时播放地址；标题中的控制符须替换为空格。失败时 rows 为空，error 为简短的错误说明。
- 最多 500 条、1 MiB，60 秒超时。宿主校验整批，拒绝空列表、不受支持的链接和嵌套合集；按返回顺序去重生成只读预览。只有用户明确确认后，才一次保存到音乐库。已有歌曲也会加入本次的目标歌单或收藏。“加入并播放”使用本次导入顺序。曲名输入仅适用于单曲。
- 确认由宿主负责：桌面弹窗显示来源、曲数、目标歌单和是否播放，CLI 使用 `[y/N]`。插件只返回预览数据，不能代替用户确认。宿主忽略 `confirm/`；新插件无需提供确认页，仍兼容旧宿主的插件可以保留它。
- 测试：`python tests/collection_import.py build/Release/NicoMusicCLI.exe` 使用临时插件和数据目录，不访问网络。

CLI 输入 `sources` 查看已注册来源；`open @webradio` 打开主页，`search @webradio 关键词` 打开搜索页。离线集成测试：`python tests/source_plugins.py build/Release/NicoMusicCLI.exe`（按测试脚本的要求提供发行包中的测试依赖）。
