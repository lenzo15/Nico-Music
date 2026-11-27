# NicoMusic

一个面向 Windows 的轻量音乐播放器，支持本地音频、媒体直链和可选音源插件。

## 下载

[下载 NicoMusic](https://github.com/lenzo15/Nico-Music/releases)

## 声明

NicoMusic 仅用于学习、研究和个人使用。项目与第三方音源服务提供方没有从属、授权或商业合作关系。项目不内置任何音频、视频或歌词资源。

请遵守相关平台服务条款、版权规定和当地法律法规。不要将本项目用于侵犯版权、规避平台限制或违反服务条款的用途。

## 应用功能

- **本地与直链播放**：选择、拖入单个本地音频文件，或粘贴本地路径、音频直链；支持 MP3、FLAC、WAV、AAC、M4A、OGG、Opus 等常见格式，可自定义曲名。
- **播放控制**：播放 / 暂停、上一首 / 下一首、拖动进度、音量调节与静音；支持顺序播放、单曲循环和列表循环。
- **音乐库与收藏**：音乐库、收藏和自建歌单均按首次导入音乐库的时间从新到旧显示；可按曲名或来源链接筛选、标记「我喜欢的」或从音乐库移除曲目。
- **自建歌单**：创建、删除歌单，将歌曲加入多个歌单或从歌单移出；删除歌单不会删除音乐库中的歌曲。
- **分享歌单**：在全部音乐、我喜欢的或自建歌单中点击添加按钮左侧的分享图标，选择目录，生成 `歌单名称.nicomix`；保留列表顺序，同名文件自动编号，不导出本机路径。启动桌面版一次后，可在 Windows 中双击带有专属歌单图标的 `.nicomix` 文件预览导入。也可将文件拖入播放器、在「添加」中选择，或用 `NicoMusic.exe "路径\歌单.nicomix"` 打开（[文件格式](docs/nicomix.md)）。可新建歌单，也可导入当前列表（全部音乐、我喜欢的或自建歌单），不重复入库、不覆盖原歌单、不自动播放。缺少对应音源插件的网页歌曲保留至插件装回。
- **批量操作**：多选或全选歌曲，批量播放、加入歌单、喜欢 / 取消喜欢，以及移出歌单或音乐库。拖动歌曲右侧的排序手柄可调整当前列表顺序，拖动已勾选的歌曲可整组移动；拖到列表上下边缘时自动滚动，越靠近边缘越快，鼠标停住也会持续滚动；顺序自动保存，也可聚焦手柄后按 ↑ / ↓ 微调。
- **撤销与重做**：导入音乐 / 歌单、从音乐库删除歌曲或从自建歌单移出歌曲后，可按 Ctrl+Z 撤销、Ctrl+Y 重做，也可使用操作提示或常规设置中的按钮。
- **播放队列**：查看当前队列并点播其中的歌曲；从收藏或自建歌单播放时按对应列表建立队列，切换页面不影响当前队列。
- **专注模式**：点击当前歌曲封面进入专注页，内置简洁视图。
- **设置与热键**：原「关于」位置改为齿轮设置入口，分为常规、热键、插件和关于，默认打开常规页；可直接打开当前数据文件夹，方便备份音乐库、歌单和偏好。
- **插件热插拔**：在运行目录的 `plugins/` 中放入、修改或移走插件，桌面版会自动刷新。修改专注预设或来源页资源不打断播放；音源脚本、依赖或清单变更会重新加载播放引擎，不改动歌单。
- **本地保存**：保留音乐库、自建歌单、收藏及播放模式、专注预设等偏好，重启后仍可使用。
- **插件扩展**：音源与专注预设独立安装、卸载，无需重新编译；不安装插件也可播放本地音频和受支持的音频直链。
- **命令行模式**：提供独立 CLI，支持添加音乐、查看列表、播放控制及循环模式，无需 WebView2。

## 发布更新

更新来源固定为 [NicoMusic Releases](https://github.com/lenzo15/Nico-Music/releases)，只检查已发布的正式版。手动检查每分钟最多一次。发现新版后，点击「设置 → 关于 → 更新并重启」即可原地更新，无需选择目录。

本地修正版的更新器使用安装包内的 [随包插件清单](plugins/bundled-plugins.tsv)：已安装的插件按各自的 `VERSION` 比较，更新包版本更高才覆盖，同版或更低版本跳过整个插件；首次随包版本高于当前播放器版本的插件会自动添加；此前已随包但被移除的插件保持不装。未提供清单的更新包仍只更新已安装插件。被选中更新的插件会整目录替换，包括删除新包不再提供的旧文件；失败时还原原目录。升级前请备份自己修改过的插件文件，持久化数据不要放在插件安装目录。

**插件版本号：** 每个插件在 `plugins/<id>/VERSION` 中用 UTF-8 写一行 `主版本.次版本.修订号`（如 `1.0.0`，每段 1–9 位数字、不可有多余前导零）。这个号属于整个插件包，与 `api=` 的协议号、播放器版本和首次随包版本无关。插件发布内容有变化时递增；本地开发不必为热重载改号。「设置 → 插件 → 已加载插件」显示当前版本；插件提供配置页或登录凭据表单时，入口就在对应条目下。CLI 的 `sources` 显示音源版本。旧插件没有 `VERSION` 时仍可加载并沿用原更新行为；有版本的插件不会被无版本包覆盖。格式错误的插件不加载，更新器不覆盖其本地文件。版本保护只适用于新版播放器发起的更新，不拦截手动复制。

**旧版限制：** 发起升级的是旧 EXE 内置的更新器。从已发布的 v1.0.2 或 v1.0.3 首次升级时，缺失的插件仍需从新版 Windows ZIP 的 `NicoMusic/plugins/` 手动复制一次。修正版不会猜测此前缺失的插件是否被主动删除。

请从干净的构建目录打包，仅收录随包清单中的插件，不要直接分发个人使用的运行目录。发布新版本时，新增的随包插件须在清单中记录首次发布版本（后续不要改动）。请同时更新 `CMakeLists.txt` 中 `project(NicoMusic VERSION X.Y.Z ...)` 的版本号，并使用对应的 `vX.Y.Z` Release 标签；附加以 `windows-x64.zip` 或 `win-x64.zip` 结尾的安装包（例如 `NicoMusic-v1.1.0-windows-x64.zip`），播放器会自动选择该包，而不是源码或 ARM64 包。下载后的程序版本取自构建时的版本号，不会把 GitHub 上的版本号当成当前版本。

## 基础插件

以下功能由插件提供，默认已跟随包体：

| 插件                                               | 类型     | 提供的功能                                                                                                                                      |
| -------------------------------------------------- | -------- | ----------------------------------------------------------------------------------------------------------------------------------------------- |
| [哔哩哔哩（bilibili）](plugins/bilibili/README.md) | 音源     | B 站主页与搜索入口；解析视频、分 P 及分享链接；批量导入个人空间的合集或系列列表，单次最多 500 首，确认后入库。需插件目录内的私有 `yt-dlp.exe`。 |
| [黑胶与极光（vinyl）](plugins/vinyl/focus/)        | 专注预设 | 黑胶唱盘、唱针与随音频变化的极光丝带。                                                                                                          |
| [环形声谱（wavehalo）](plugins/wavehalo/focus/)    | 专注预设 | 旋转圆形封面与频谱光条。                                                                                                                        |

音源插件提供网站入口、网页解析及合集导入；视觉预设插件提供专注页外观。专注预设的使用与调节说明见[专注预设文档](docs/focus-presets.md)。

将完整插件放入 **应用文件夹的 `plugins/<插件名>/`**，完全退出播放器后重新打开即可。卸载只需移走对应目录，无需重新编译，也不会删除歌单。音源与视觉预设互不依赖；插件以当前用户权限运行，请只安装可信插件。

开发自己的插件：[音源插件协议](docs/source-plugins.md) · [本地文件解析示例](examples/plugins/local-link/) · [网页电台示例](examples/plugins/webradio/) · [专注预设协议](docs/focus-presets.md)

## 目录导航

| 目录 | 用途 |
| --- | --- |
| [src/](src/) | 原生代码：`desktop/` 桌面窗口与系统集成，`cli/` 命令行入口，`audio/` 播放与频谱，`library/` 音乐库与歌单，`plugins/` 插件宿主，`updates/` 更新流程。 |
| [art/icons/windows/](art/icons/windows/) | 编入 EXE 的应用图标、`.nicomix` 文件图标及其 SVG 原稿。 |
| [ui/](ui/) | 界面入口 `index.html`；交互代码在 `scripts/`，样式在 `styles/`。 |
| [ui/art/icons/](ui/art/icons/) | 随 WebView 界面分发的图标：`glyphs.js` 集中存放 SVG 图形，独立使用的勾选图标为 `selection-check.svg`。 |
| [plugins/](plugins/) | 独立安装、热重载的插件包；插件自己的脚本和样式保留在包内。 |

## 从源码运行

在 Windows 10/11 x64 上，安装 Visual Studio 2022 的「使用 C++ 的桌面开发」、Windows SDK 10.0.20348 或更新版，以及 CMake 3.21+。在项目根目录执行：

```powershell
# 可选：按所选插件包内的说明准备其私有运行依赖

cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

只构建核心（不复制随包插件）：在新的构建目录配置 `-DNICOMUSIC_BUNDLE_PLUGINS=OFF`；仅需 CLI 时再加 `-DNICOMUSIC_DESKTOP=OFF`。关闭随包构建不会移除已有输出目录里手动安装的插件。

将 [x64 libmpv](https://github.com/zhongfly/mpv-winbuild/releases) 的 `libmpv-2.dll` 及其配套 DLL 放入 `build/Release/`。桌面版还需 [WebView2 Runtime](https://developer.microsoft.com/microsoft-edge/webview2/)，并保留 EXE 旁自动复制的 `ui/`。启动 `NicoMusic.exe` 即可；只用命令行可启动 `NicoMusicCLI.exe`，无需 WebView2 Runtime。

<details>
<summary>本地数据、测试与许可</summary>

音乐库默认保存在 `%LOCALAPPDATA%\NicoMusic\library.tsv`，组合热键保存在同目录的 `hotkeys.ini`（仅含三个可编辑组合键，内置媒体键不受影响）。升级时沿用已有的 `%LOCALAPPDATA%\BiliMusic` 目录，保留歌单、收藏和偏好；设置 `NICOMUSIC_HOME` 可更换数据目录。验证无插件播放：`python tests/smoke.py build/Release/NicoMusicCLI.exe`；验证无插件桌面模式：`python tests/desktop_playlists.py build/Release/NicoMusic.exe --minimal`（需先退出桌面播放器并安装 `websocket-client`）。

撤销 / 重做回归：`node --test tests/edit_history.js`、`python tests/edit_history_ui.py`（无头浏览器、模拟原生消息，不操作正在运行的播放器）。配置加 `-DNICOMUSIC_HISTORY_TESTS=ON`，构建并运行 `NicoMusicHistoryTest`，验证原生导入、删除、重复歌曲保护、元数据与顺序恢复、持久保存及写入失败回滚。

拖动排序边缘滚动回归：`node --test tests/track_order.js`、`python tests/reorder_scroll_ui.py`，验证持续上下滚动、整组落点、插入提示跟随及退出拖动后的停止。

列表滚动布局回归：`python tests/collection_scroll.py`（需安装 Edge 或 Chrome 及 `websocket-client`，无头运行，不影响已打开的播放器）。

设置与快捷键回归：`node --test tests/shortcuts.js`、`python tests/settings_ui.py`（无头浏览器，不影响正在运行的播放器）。配置时加 `-DNICOMUSIC_HOTKEY_TESTS=ON`，构建后运行 `build/Release/NicoMusicHotkeyTest.exe`，验证原生保存、重启读取、占用及磁盘失败回滚和录制恢复（模拟注册，不抢占系统热键）。Windows 桌面集成：`python tests/desktop_shortcuts.py build/Release/NicoMusic.exe`，加 `--conflict` 验证全局组合键被占用时的提示与窗口内降级（需先退出桌面播放器，且组合键和媒体键未被其他程序占用）。

更新检查及下载逻辑回归：`node --test tests/updates.js`；「关于」页及下载状态无头回归：`python tests/updates_ui.py`（模拟原生消息，不实际下载）。原生联网测试：配置时加 `-DNICOMUSIC_UPDATE_TESTS=ON`，构建后运行 `python tests/updater_network.py build/Release/NicoMusicUpdateTest.exe`，验证真实 GitHub 检查、下载、校验失败保护及取消清理（临时目录，不影响已安装播放器）。使用同一测试程序运行 `python tests/update_install.py build/Release/NicoMusicUpdateTest.exe` 可验证原地安装、重启、回滚和用户文件保留。

[mpv 客户端头文件](third_party/mpv/client.h) 保留原 ISC 声明；libmpv 二进制、WebView2 和插件使用的第三方工具各自遵循上游许可。

</details>
