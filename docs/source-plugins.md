# 音乐来源插件（API 2）

桌面版和 CLI 只负责按网页域名或本地文件扩展名分派原链接、播放媒体和管理歌单。**播放器不附带、不下载、不在 PATH 查找网页解析器**，libmpv 内置的 ytdl hook 已禁用。每个来源选择并携带自己的解析工具，也可以仅使用 Lua，不依赖 yt-dlp。只安装可信插件：插件以当前用户权限执行。

## 安装、卸载与升级

将完整包放在 **EXE 同目录** 的 `plugins/<id>/`，退出并重启播放器。移走整个目录即可卸载，已保存的歌单不会被删除。没有匹配的插件时，网页不能添加或播放，旧网页曲目会从连播队列跳过。明确带媒体文件后缀的 HTTP(S) 直链无需插件。

示例 B 站包结构（目录 id 为小写 ASCII 字母、数字、连字符，最多 64 字符）：

```text
plugins/bilibili/
    source.ini
    main.lua                         # mpv Lua 插件入口
    yt-dlp.exe
    extractors/site/yt_dlp_plugins/extractor/bilimusic_bilibili.py
```

`source.ini` 是 UTF-8 的 `key=value` 清单，支持整行 `#` 注释：

```ini
api=2
name=哔哩哔哩
hosts=bilibili.com,b23.tv,bili2233.cn
home=https://www.bilibili.com/
search=https://search.bilibili.com/all?keyword={query}
requires=yt-dlp.exe
```

`hosts` 填裸域名（不含端口），匹配该域名及子域名；最具体的匹配优先，同等匹配按 id 排序。`search` 可省略。`requires` 是可选的插件内文件名，不能是路径。缺少 `main.lua` 或所声明的依赖时不注册该来源，不借用别的插件、安装目录或系统中的同名程序。API 1 的插件需升级，不能只改版本号。

源码仓库忽略 `.exe`。从源码组装 B 站发行包时，可在插件目录运行 `install-runtime.ps1` 配齐该版本的私有工具（校验 SHA-256），也可自行放入兼容的官方 `yt-dlp.exe`。安装器不由播放器自动运行。发行和更新时复制整个插件包，保留上游许可。`examples/plugins/webradio` 使用相同的适配器，也需要**它自己的** `yt-dlp.exe`。

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
- [可运行示例](../examples/plugins/local-link/)：将示例目录复制到 EXE 旁的 `plugins/local-link/` 并重启。新建 UTF-8 的 `试听.bmlink`，内容为 `{"title":"试听","file":"伴奏.wav"}`，与 `伴奏.wav` 放在同一目录。拖入 `.bmlink` 后应显示「试听」，播放的是同目录的 WAV。示例只在该目录内解析 WAV 文件名，不需要额外工具。

## 插件协议

- 宿主只加载已注册来源的 mpv 脚本包。播放器向插件传递 `bm-<id>://<原始 HTTP(S) 或 file URI>`，包括分 P 和查询参数。歌单内只保存原链接，不保存带时效的 CDN 地址。
- 播放或连播到该条目时，`main.lua` 的 `on_load` 钩子识别**自己的**前缀，解析它并设置 `stream-open-filename`，必要时设置文件局部的 `http-header-fields`、`user-agent` 和 `force-media-title`。不要改变其他来源的链接。对长耗时操作使用异步子进程并在结束时继续钩子。
- 后台查标题、封面时宿主发送 `script-message bm-metadata <token> <带前缀的链接>`。对应插件异步应答 `script-message bm-metadata-result <token> <标题> <封面 URL>`；失败可回两个空串。宿主匹配 token，超时放弃该次请求，不阻塞其他来源。
- 提供的 B 站/电台适配器用 `mp.get_script_directory()` 定位本包的可执行文件与 `extractors/`，以参数数组调用私有工具，不使用 shell、不读取 yt-dlp 用户配置，只加载本包的提取器。插件自行选择 `bestaudio/best` 和 HTTP(S) 媒体流。B 站提取器负责移动端/分享链接，并复用工具自带的 B 站客户端。

CLI 输入 `sources` 查看已注册来源；`open @bilibili` 打开主页，`search @bilibili 关键词` 打开搜索页。离线集成测试：`python tests/source_plugins.py build/Release/NicoMusicCLI.exe`（该测试需要旁边有完整的 `plugins/bilibili` 包）。
