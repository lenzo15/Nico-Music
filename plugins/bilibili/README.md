# 哔哩哔哩音源

复制整个 `bilibili/` 到播放器旁的 `plugins/` 目录，完全退出并重启。B 站主页与搜索入口来自本包的 `source.ini`；网页、分 P 和分享链接由本包的 Lua 适配器与私有提取器解析。

- 粘贴 B 站视频链接可添加单曲；个人空间的合集或系列列表链接（如 `https://space.bilibili.com/<UID>/lists/<ID>?type=season`）可批量导入。请复制列表页而不是其中一集的视频页。
- 合集最多 500 首，确认界面由 `confirm/index.html` 提供；取消不会入库。
- CLI：`open @bilibili` 打开主页，`search @bilibili 关键词` 打开搜索页。
- 从源码组装时，在本目录运行 `install-runtime.ps1` 安装经 SHA-256 校验的私有 `yt-dlp.exe`。播放器不自动安装，也不回退到系统中的解析器。
- 卸载：移走整个插件目录并重启。相关网页曲目将隐藏，但不删除原链接或歌单、收藏关系；装回后恢复显示。
