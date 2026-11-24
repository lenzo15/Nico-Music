# NicoMusic

一个面向 Windows 的插件化、极简的仿网易云音乐播放器。基础插件支持B站直链播放音频

## 声明

NicoMusic 仅用于学习、研究和个人使用。项目与 BiliBili 及相关服务提供方没有从属、授权或商业合作关系。项目不内置任何音频、视频或歌词资源。

请遵守相关平台服务条款、版权规定和当地法律法规。不要将本项目用于侵犯版权、规避平台限制或违反服务条款的用途。

## 基础插件

| 功能                                       | 名称                              |
| ------------------------------------------ | --------------------------------- |
| 粘贴 B 站视频页，获取音频、标题和封面      | [哔哩哔哩音源](plugins/bilibili/) |
| 在专注页使用毛玻璃封面、旋转黑胶和极光丝带 | [黑胶与极光预设](plugins/vinyl/)  |

将完整插件放入 **EXE 旁的 `plugins/<插件名>/`**，完全退出播放器后重新打开即可。卸载只需移走对应目录，无需重新编译，也不会删除歌单。音源与视觉预设互不依赖；插件以当前用户权限运行，请只安装可信插件。

**`yt-dlp.exe` 是 B 站插件的私有依赖，不是播放器依赖。** 它放在 `plugins/bilibili/` 内，不放在 EXE 旁。没有匹配的音源插件时，播放器不会从系统 PATH 寻找解析器兜底。

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
