"use strict";
// Only host controls can authorize a library write. Source frames supply data, never decisions.
let confirmingCollection = false;
async function confirmCollection(sourceName, preview, destination) {
  const dialog = document.getElementById("sourceImportDialog");
  if (confirmingCollection || dialog.open) return false;
  document.getElementById("sourceImportSummary").textContent =
    `将从${sourceName}导入 ${preview.count} 首歌，${destination}${preview.play ? "导入后开始播放。" : ""}`;
  dialog.returnValue = "cancel";
  dialog.showModal();
  confirmingCollection = true;
  return new Promise(resolve => dialog.addEventListener("close", () => {
    confirmingCollection = false;
    resolve(dialog.returnValue === "accept");
  }, {once:true}));
}
