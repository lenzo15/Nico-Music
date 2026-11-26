"use strict";
function icon(name) {
  return `<svg class="iconSvg" aria-hidden="true" viewBox="0 0 24 24">${iconGlyphs[name]}</svg>`;
}
document
  .querySelectorAll("[data-icon]")
  .forEach((el) => { el.innerHTML = el.localName === "svg" ? iconGlyphs[el.dataset.icon] : icon(el.dataset.icon); });
