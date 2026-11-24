# 专注模式预设（API 1）

专注页由播放器打开/关闭，底部播放栏始终由播放器控制。背景、绘制、动画和播放栏主题都可由可选的本地预设提供。不安装预设时只显示静态曲名，不加载预设脚本、样式或启动音频分析。音源插件和视觉预设分别发现，互不依赖。

在 **EXE 同目录** 安装以下文件，完整退出后重启；卸载时移走整个预设目录。目录 id 使用小写字母、数字或连字符，最长 63 字符，不以连字符开头或结尾。仅安装可信代码：预设脚本在播放器界面内执行，不是沙箱。

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
- `reduceMotion`：系统动态效果偏好（MediaQueryList）。`signal`：本次挂载的 AbortSignal，可传给事件监听。`dispose()` 可选，用来停止动画帧、定时器和其他资源。
- 样式表仅在当前预设激活时生效。用 `body[data-focus-preset="my-theme"]` 限定标题栏和播放栏等全局选择器。不要接管原播放控件。切换或退出时宿主撤销订阅、卸载样式并清空两个挂载点。

完整示例见 [`plugins/vinyl/focus/`](../plugins/vinyl/focus)。
