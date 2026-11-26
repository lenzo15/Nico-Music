# 专注模式预设（API 1）

专注页由播放器打开/关闭，底部播放栏始终由播放器控制。背景、绘制、动画和播放栏主题都可由可选的本地预设提供。不安装预设时只显示静态曲名，不加载预设脚本、样式或启动音频分析。音源插件和视觉预设分别发现，互不依赖。

在 **EXE 同目录** 安装以下文件；桌面版会在目录变动稳定后自动更新（通常约 2 秒）。编辑当前预设的 JS/CSS 会重新挂载，不刷新播放器页面或中断音乐。移走整个预设目录即卸载，当前预设回退为「简洁」；重新放回后恢复选择。目录 id 使用小写字母、数字或连字符，最长 63 字符，不以连字符开头或结尾。仅安装可信代码：预设脚本在播放器界面内执行，不是沙箱。

```
plugins/my-theme/focus/
  preset.ini    # UTF-8，两行：api=1、name=预设名称
  style.css     # 可以为空
  preset.js
```

宿主只注册 API 版本匹配且文件齐全的包。用户第一次打开时默认使用按 id 排序的第一个预设；在顶部选择器切换预设或「简洁」，选择会保存在本地。指定的预设被卸载、脚本或样式加载失败、挂载或更新抛错时回到简洁模式。

`preset.js` 是经典脚本。在脚本执行期间把同步工厂赋给 `document.currentScript.mountFocus`。脚本只负责注册；不要在脚本顶层修改页面或启动计时器。示例：

```js
document.currentScript.mountFocus = ({ root, background, renderCover }) => {
  const heading = document.createElement('h2');
  root.append(heading);
  return {
    update({title}) {
      heading.textContent = title;
      renderCover(background);
    }
  };
};
```

- `root`：专注页内容挂载点；`background`：覆盖全窗的背景挂载点（定位、模糊等由预设 CSS 决定）。`renderCover(element)` 使用宿主校验过的当前封面，支持回退图标。
- `update({title, playing, progress})`：必需。`progress` 为 0–1；不在此回调内重建播放器或控制音源。
- `requestLevels(true/false)`：按需订阅/取消本进程音频的低、中、高频能量。返回对象可实现 `levels([bass, mid, high])`，各值 0–100；`null` 表示设备不支持。没有订阅时不启动分析。宿主在暂停、页面隐藏或系统减少动态效果时停止订阅。恢复后可在 `update` 中重新订阅。
- 需要频谱的预设可传 `requestLevels(true, {spectrum:true})`，在 `levels([bass, mid, high], spectrum)` 的第二个参数接收 128 个 0–100 的绝对频带电平，按约 35 Hz 到 18 kHz 从低到高排列。采样仍来自本播放器的进程回环，不使用麦克风；只有请求频谱时才计算 4096 点立体声 FFT。旧宿主只传第一个参数。
- `reduceMotion`：系统动态效果偏好（MediaQueryList）。`signal`：本次挂载的 AbortSignal，可传给事件监听。`dispose()` 可选，用来停止动画帧、定时器和其他资源。预设可返回 `exitFadeMs`：队列结束、无当前歌曲时，宿主移除 `#focusView.playing` 并短暂保留最后画面再关闭（最多 2 秒）；手动退出仍立即关闭。
- 样式表仅在当前预设激活时生效。用 `body[data-focus-preset="my-theme"]` 限定标题栏和播放栏等全局选择器。不要接管原播放控件。切换或退出时宿主撤销订阅、卸载样式并清空两个挂载点。

内置的可选预设：
- [黑胶与极光](../plugins/vinyl/focus)：唱盘、唱针与音频驱动的极光丝带。
- [环形声谱](../plugins/wavehalo/focus)：圆形封面持续旋转，白色光条从右上角开始顺时针对应 128 段真实频谱。每条独立向内外伸缩，邻域频率对比突出窄峰，减去当帧公共底量，不用音量增量或随机数模拟节拍。播放或恢复时光条淡入（`fadeInMs`，默认 320 ms）；暂停或队列结束时淡出（`fadeOutMs`，默认 450 ms），封面不参与淡出。播放中缺少实时数据时，光条收回到基础长度；旧版三频宿主降级为宽频带响应。
  - 参数集中在 `plugins/wavehalo/focus/preset.js` 开头的 `tuning` 对象，不读取旧面板保存的值。编辑运行目录中的该文件并保存即可热重载，不需要重启或重新编译。`attack` / `release` 为上升和回落时间（默认 12 / 85 ms）。
  - 达到「低音提亮阈值」后只增强白光与泛光，不改色。`brightenMs` 控制提亮时间（默认 150 ms，0 表示立即提亮）；低于阈值 8 个百分点后，280 ms 内恢复原亮度。
