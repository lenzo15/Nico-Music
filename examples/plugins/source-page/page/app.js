"use strict";
const host = "https://studio.bilimusic.local";
const button = document.getElementById("toggle"), status = document.getElementById("status");
let sequence = 0, pending;
function request(action) {
  if (pending) return;
  button.disabled = true;
  pending = String(++sequence);
  parent.postMessage({type: "source-request", id: pending, action, data: null}, host);
}
addEventListener("message", event => {
  const msg = event.data;
  if (event.source !== parent || event.origin !== host || msg?.type !== "source-response" || msg.id !== pending) return;
  pending = undefined; button.disabled = false;
  try {
    status.textContent = msg.error || (JSON.parse(msg.data).signedIn ? "已模拟登录，可以播放示例曲目。" : "未登录：请先启用模拟登录。");
  } catch { status.textContent = "无法读取来源响应"; }
});
button.onclick = () => request("toggle-demo");
request("status");
