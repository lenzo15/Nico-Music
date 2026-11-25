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

// Pixels per second. A short viewport gets smaller, non-overlapping edge zones.
function reorderScrollSpeed(y, top, bottom) {
  const edge = Math.min(56, (bottom - top) / 3);
  if (edge <= 0 || y < top || y >= bottom) return 0;
  const distance = Math.min(y - top, bottom - y);
  if (distance >= edge) return 0;
  const strength = 1 - distance / edge;
  return (y < top + edge ? -1 : 1) * (80 + 640 * strength * strength);
}

function createReorderHandles(rows, {selected, visible, move}) {
  let moving = null, hint = null, pointer = null, frame = null, lastTime = null;
  function clearHint() {
    if (hint) delete hint.dataset.insert;
    hint = null;
  }
  function stopScroll() {
    if (frame !== null) cancelAnimationFrame(frame);
    frame = null; lastTime = null;
  }
  function reset() { stopScroll(); clearHint(); moving = null; pointer = null; }
  function group(url) { return selected.has(url) ? new Set(selected) : new Set([url]); }
  function inside(point, rect) {
    return point && point.x >= rect.left && point.x < rect.right && point.y >= rect.top && point.y < rect.bottom;
  }
  function target(point) {
    if (!moving || !inside(point, rows.getBoundingClientRect())) return null;
    // Drag events can keep their old target while scrolling under a stationary pointer.
    const row = document.elementFromPoint(point.x, point.y)?.closest('.pick');
    if (!row || !rows.contains(row) || moving.has(row.dataset.url)) return null;
    const r = row.getBoundingClientRect();
    return {row, after: point.y >= r.top + r.height / 2};
  }
  function updateHint() {
    const dest = target(pointer);
    if (!dest) { clearHint(); return; }
    if (hint !== dest.row) clearHint();
    hint = dest.row; hint.dataset.insert = dest.after ? 'after' : 'before';
  }
  function speed() {
    const rect = rows.getBoundingClientRect();
    if (!moving || !inside(pointer, rect)) return 0;
    const value = reorderScrollSpeed(pointer.y, rect.top, rect.bottom);
    return value < 0 && rows.scrollTop <= 0 ||
      value > 0 && rows.scrollTop >= rows.scrollHeight - rows.clientHeight - 1 ? 0 : value;
  }
  function scroll(time) {
    frame = null;
    const velocity = speed();
    if (!velocity) { lastTime = null; return; }
    const elapsed = lastTime === null ? 1 / 60 : Math.min(50, time - lastTime) / 1000;
    lastTime = time;
    rows.scrollTop += velocity * elapsed;
    updateHint();
    if (speed()) frame = requestAnimationFrame(scroll);
    else lastTime = null;
  }
  // Track our own drag even over the dragged/selected rows, not just valid drop targets.
  document.addEventListener('dragover', e => {
    if (!moving) return;
    pointer = {x: e.clientX, y: e.clientY};
    if (inside(pointer, rows.getBoundingClientRect())) {
      e.preventDefault();
      if (e.dataTransfer) e.dataTransfer.dropEffect = 'move';
    }
    updateHint();
    if (speed()) {
      if (frame === null) frame = requestAnimationFrame(scroll);
    } else stopScroll();
  }, true);
  rows.addEventListener('dragleave', e => {
    // Crossing children (also caused by scrolling) may report a null relatedTarget.
    if (!inside({x: e.clientX, y: e.clientY}, rows.getBoundingClientRect())) {
      stopScroll(); clearHint(); pointer = null;
    }
  });
  document.addEventListener('dragleave', e => {
    if ((e.target === document.documentElement || e.target === document) && !e.relatedTarget) {
      stopScroll(); clearHint(); pointer = null;
    }
  });
  rows.addEventListener('drop', e => {
    if (!moving) return;
    e.preventDefault();
    const dest = target({x: e.clientX, y: e.clientY}), group = moving;
    reset();
    if (dest) move(group, dest.row.dataset.url, dest.after);
  });
  document.addEventListener('drop', e => { if (!rows.contains(e.target)) reset(); });
  document.addEventListener('dragend', reset);
  window.addEventListener('blur', reset);
  document.addEventListener('visibilitychange', () => { if (document.hidden) reset(); });
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
        reset();
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
if (typeof module !== 'undefined') module.exports = {createTrackOrder, moveVisibleTracks, reorderScrollSpeed};
