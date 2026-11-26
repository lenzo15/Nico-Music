# NicoMix 歌单文件（v1）

`.nicomix` 是 UTF-8 JSON，不包含音频、封面、登录凭据或可执行插件。数组顺序就是分享者的歌单顺序，与搜索条件无关。示例：

```json
{
  "format": "nicomix",
  "version": 1,
  "name": "下班路上",
  "tracks": [
    {"title": "第一首", "url": "https://example.org/music/one.mp3"},
    {"title": "第二首", "url": "https://example.org/music/two.ogg"}
  ]
}
```

- `name` 为 1–60 个 UTF-16 代码单元。导出文件名会替换 Windows 不允许的字符；JSON 内的歌单名不变，同名文件自动编号。
- `url` 必须是原始 HTTP(S) 链接，不接受携带用户名密码的地址、本机路径或 `file:` URI。标题可省略，此时显示链接。导出时跳过本地文件；不要分享含私密访问令牌的链接。
- 文件最大 8 MiB，最多 10000 条记录。导入时先整份校验，再按 URL 去重（保留首个位置）；未知版本、损坏文件不会部分入库。
- 导入前可修改新歌单名称，默认避开已有名称。取消不写入；默认新建歌单，也可点击“创建歌单”左侧的按钮直接导入当前列表：在全部音乐页只入库，在我喜欢的页同时收藏，在自建歌单页合并到该歌单。已有成员保留原顺序，新成员按文件顺序追加，不重复。缺少音源的网页条目仍保留，装回插件后显示。
- 无需音源插件即可读写此格式。可拖入桌面版、在“添加”中选择文件或粘贴本机路径，也可用 Windows 的“打开方式”选择 `NicoMusic.exe`。桌面版正常启动一次后，会为当前 Windows 用户注册歌单文件图标和双击打开命令，无需管理员权限。双击后先预览，确认才导入；已打开播放器时会复用原窗口。如果系统提示选择打开方式，请选 NicoMusic 并设为“始终”。不会覆盖其他已选默认程序。移动绿色版后从新位置启动即可刷新路径；也可运行 `NicoMusic.exe --register-nicomix` 仅刷新关联。使用 `NICOMUSIC_HOME` 的隔离配置不会自动注册。

桌面集成测试：`python tests/desktop_playlists.py build/NicoMusic.exe --nicomix`，需退出桌面版并安装 `websocket-client` 和 `pywin32`。测试使用临时数据与导出目录。已关联 NicoMusic 的机器上，可用 `python tests/nicomix_shell.py build/NicoMusic.exe` 只读检查 Windows 实际解析到的程序、打开命令和多尺寸文件图标。
