# NicoMusic

一个面向 Windows 的插件化、极简的仿网易云音乐播放器，基础插件支持B站直链播放音频。

## 声明

NicoMusic 仅用于学习、研究和个人使用。项目与 BiliBili 及相关服务提供方没有从属、授权或商业合作关系。项目不内置任何音频、视频或歌词资源。

请遵守相关平台服务条款、版权规定和当地法律法规。不要将本项目用于侵犯版权、规避平台限制或违反服务条款的用途。

## 应用功能

- **本地与直链播放**：选择、拖入单个本地音频文件，或粘贴本地路径、音频直链；支持 MP3、FLAC、WAV、AAC、M4A、OGG、Opus 等常见格式，可自定义曲名。
- **播放控制**：播放 / 暂停、上一首 / 下一首、拖动进度、音量调节与静音；支持顺序播放、单曲循环和列表循环。
- **音乐库与收藏**：浏览全部音乐、标记「我喜欢的」，按曲名或来源链接筛选，从音乐库移除曲目。
- **自建歌单**：创建、删除歌单，将歌曲加入多个歌单或从歌单移出；删除歌单不会删除音乐库中的歌曲。
- **批量操作**：多选或全选歌曲，批量播放、加入歌单、喜欢 / 取消喜欢，以及移出歌单或音乐库。
- **播放队列**：查看当前队列并点播其中的歌曲；从收藏或自建歌单播放时按对应列表建立队列，切换页面不影响当前队列。
- **专注模式**：点击当前歌曲封面进入专注页，内置简洁视图。
- **托盘与快捷操作**：最小化到系统托盘后继续播放，点击托盘图标恢复窗口；关闭窗口时可选择收起或退出并记住选择；支持空格播放 / 暂停、Esc 返回列表。
- **本地保存**：保留音乐库、自建歌单、收藏及播放模式、专注预设等偏好，重启后仍可使用。
- **插件扩展**：音源与专注预设独立安装、卸载，无需重新编译；不安装插件也可播放本地音频和受支持的音频直链。
- **命令行模式**：提供独立 CLI，支持添加音乐、查看列表、播放控制及循环模式，无需 WebView2。

## 基础插件

以下功能由插件提供，需安装对应插件后使用：

| 名称                              | 提供的功能                                                                                            |
| --------------------------------- | ----------------------------------------------------------------------------------------------------- |
| [哔哩哔哩音源](plugins/bilibili/) | 粘贴 B 站视频页链接，获取音频、标题和封面，无需保持网页打开；提供 B 站主页入口与 CLI 打开搜索页功能。 |
| [黑胶与极光预设](plugins/vinyl/)  | 在专注页显示毛玻璃封面、旋转黑胶和随音乐变化的极光丝带，支持开关可视化特效。                          |

将完整插件放入 **应用文件夹的 `plugins/<插件名>/`**，完全退出播放器后重新打开即可。卸载只需移走对应目录，无需重新编译，也不会删除歌单。音源与视觉预设互不依赖；插件以当前用户权限运行，请只安装可信插件。

开发自己的插件：[音源插件协议](docs/source-plugins.md) · [本地文件解析示例](examples/plugins/local-link/) · [网页电台示例](examples/plugins/webradio/) · [专注预设协议](docs/focus-presets.md)

## 从源码运行

在 Windows 10/11 x64 上，安装 Visual Studio 2022 的「使用 C++ 的桌面开发」、Windows SDK 10.0.20348 或更新版，以及 CMake 3.21+。在项目根目录执行：

```powershell
# 可选：需要 B 站音源时，先为插件准备私有解析器
# .\plugins\bilibili\install-runtime.ps1

cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

将 [x64 libmpv](https://github.com/zhongfly/mpv-winbuild/releases) 的 `libmpv-2.dll` 及其配套 DLL 放入 `build/Release/`。桌面版还需 [WebView2 Runtime](https://developer.microsoft.com/microsoft-edge/webview2/)，并保留 EXE 旁自动复制的 `ui/`。启动 `NicoMusic.exe` 即可；只用命令行可启动 `NicoMusicCLI.exe`，无需 WebView2 Runtime。

<details>
<summary>本地数据、测试与许可</summary>

音乐库默认保存在 `%LOCALAPPDATA%\NicoMusic\library.tsv`。升级时沿用已有的 `%LOCALAPPDATA%\BiliMusic` 目录，保留歌单、收藏和偏好；设置 `NICOMUSIC_HOME` 可更换数据目录（兼容旧变量 `BILIMUSIC_HOME`）。验证无插件播放：`python tests/smoke.py build/Release/NicoMusicCLI.exe`；验证无插件桌面模式：`python tests/desktop_playlists.py build/Release/NicoMusic.exe --minimal`（需先退出桌面播放器并安装 `websocket-client`）。

[mpv 客户端头文件](third_party/mpv/client.h) 保留原 ISC 声明；libmpv 二进制、WebView2 和插件使用的第三方工具各自遵循上游许可。

</details>
