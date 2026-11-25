"use strict";
// Use stable library URLs, never row numbers or native playback indices, as ordering keys.
function createTrackOrder(storage) {
  const cache = new Map(), key = page => `nico.trackOrder.v1.${page}`;
  function saved(page) {
    if (!cache.has(page)) {
      let urls;
      try { urls = JSON.parse(storage.getItem(key(page))); } catch {}
      cache.set(page, Array.isArray(urls) && urls.every(u => typeof u === 'string') ? [...new Set(urls)] : []);
    }
    return cache.get(page);
  }
  return {
    has: page => saved(page).length > 0,
    apply(page, indices, tracks) {
      const urls = saved(page);
      if (!urls.length) return indices;
      const remaining = new Map(indices.map(i => [tracks[i].url, i])), ordered = [];
      for (const url of urls) if (remaining.has(url)) {
        ordered.push(remaining.get(url)); remaining.delete(url);
      }
      return [...remaining.values(), ...ordered]; // Newly added songs keep their default order at the top.
    },
    save(page, urls) {
      storage.setItem(key(page), JSON.stringify(urls));
      cache.set(page, urls);
    },
  };
}

// Rearrange only the visible slots; filtered-out tracks stay exactly where they were.
function moveVisibleTracks(full, visible, moving, target, after) {
  const moved = visible.filter(url => moving.has(url));
  const rest = visible.filter(url => !moving.has(url));
  const at = rest.indexOf(target);
  if (at < 0 || !moved.length) return null;
  const reordered = [...rest.slice(0, at + Number(after)), ...moved, ...rest.slice(at + Number(after))];
  if (reordered.every((url, i) => url === visible[i])) return null;
  const slots = new Set(visible);
  let i = 0;
  return full.map(url => slots.has(url) ? reordered[i++] : url);
}

function createReorderHandles(rows, {selected, visible, move}) {
  let moving = null, hint = null;
  function clearHint() {
    if (hint) delete hint.dataset.insert;
    hint = null;
  }
  function reset() { clearHint(); moving = null; }
  function group(url) { return selected.has(url) ? new Set(selected) : new Set([url]); }
  function target(e) {
    const row = e.target.closest('.pick');
    if (!moving || !row || !rows.contains(row) || moving.has(row.dataset.url)) return null;
    const r = row.getBoundingClientRect();
    return {row, after: e.clientY >= r.top + r.height / 2};
  }
  rows.addEventListener('dragover', e => {
    const dest = target(e);
    if (!dest) { clearHint(); return; }
    e.preventDefault(); e.dataTransfer.dropEffect = 'move';
    if (hint !== dest.row) clearHint();
    hint = dest.row; hint.dataset.insert = dest.after ? 'after' : 'before';
  });
  rows.addEventListener('dragleave', e => { if (!rows.contains(e.relatedTarget)) clearHint(); });
  rows.addEventListener('drop', e => {
    if (!moving) return;
    e.preventDefault();
    const dest = target(e), group = moving;
    reset();
    if (dest) move(group, dest.row.dataset.url, dest.after);
  });
  return {
    reset,
    attach(row, url, title) {
      row.dataset.url = url;
      const handle = document.createElement('button');
      handle.type = 'button'; handle.className = 'icon reorderGrip'; handle.draggable = true;
      handle.title = '拖动排序，也可使用上下方向键';
      handle.setAttribute('aria-label', `调整「${title}」顺序，使用上下方向键移动`);
      handle.innerHTML = '<svg class="iconSvg" viewBox="0 0 24 24" aria-hidden="true"><path d="M5 7h14M5 12h14M5 17h14"/></svg>';
      handle.ondragstart = e => {
        moving = group(url); e.dataTransfer.effectAllowed = 'move';
        e.dataTransfer.setData('text/plain', ''); e.dataTransfer.setDragImage(row, 20, row.offsetHeight / 2);
      };
      handle.ondragend = reset;
      handle.onkeydown = e => {
        if (e.key !== 'ArrowUp' && e.key !== 'ArrowDown') return;
        e.preventDefault(); e.stopPropagation();
        const urls = visible(), moving = group(url), down = e.key === 'ArrowDown';
        const ordered = down ? [...urls].reverse() : urls;
        const edge = ordered.findIndex(u => moving.has(u));
        if (edge > 0 && move(moving, ordered[edge - 1], down)) {
          [...rows.children].find(r => r.dataset.url === url)?.querySelector('.reorderGrip')?.focus();
        }
      };
      row.append(handle);
    },
  };
}
if (typeof module !== 'undefined') module.exports = {createTrackOrder, moveVisibleTracks};
