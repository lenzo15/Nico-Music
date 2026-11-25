"use strict";
// The plugin owns the contents and buttons. Its sandbox can only return one decision.
function confirmPluginCollection({source, count, play}, destination) {
  if (!/^[a-z0-9-]{1,64}$/.test(source)) return Promise.reject(new Error("音源标识无效"));
  return new Promise((resolve, reject) => {
    const dialog = document.createElement("dialog");
    dialog.className = "pluginConfirm";
    dialog.setAttribute("aria-label", "确认合集导入");
    const frame = document.createElement("iframe");
    frame.title = "音源插件导入确认";
    frame.setAttribute("sandbox", "allow-scripts");
    const timeout = setTimeout(() => finish(false, "音源插件的确认界面未能加载"), 8000);
    let ready = false, finished = false;
    function finish(accepted, error) {
      if (finished) return;
      finished = true;
      clearTimeout(timeout);
      window.removeEventListener("message", receive);
      dialog.close();
      dialog.remove();
      if (error) reject(new Error(error)); else resolve(accepted);
    }
    function receive(event) {
      if (event.source !== frame.contentWindow || event.origin !== "null") return;
      if (!ready && event.data === "collection-ready") {
        ready = true;
        clearTimeout(timeout);
        frame.contentWindow.postMessage({type: "collection-preview", count, play, destination}, "*");
      } else if (ready && (event.data === "collection-accept" || event.data === "collection-cancel")) {
        finish(event.data === "collection-accept");
      }
    }
    dialog.oncancel = (e) => { e.preventDefault(); finish(false); };
    dialog.onclick = (e) => { if (e.target === dialog) finish(false); };
    window.addEventListener("message", receive);
    frame.src = `https://${source}.collection.bilimusic.local/index.html`;
    dialog.append(frame);
    document.body.append(dialog);
    dialog.showModal();
  });
}
