"use strict";
// Membership uses library URLs, not titles or row numbers. Reordering or resolving
// metadata never changes which songs belong to a collection.
class PlaylistStore {
  constructor(storage) {
    this.storage = storage;
    this.key = "bilimusic.playlists.v1";
    this.items = JSON.parse(storage.getItem(this.key) || "[]");
    if (!Array.isArray(this.items) || !this.items.every(list =>
      typeof list?.id === "string" && typeof list.name === "string" &&
      Array.isArray(list.urls) && list.urls.every(u => typeof u === "string")))
      throw new Error("歌单数据无法读取");
  }
  find(id) { return this.items.find(list => list.id === id); }
  commit(items) {
    this.storage.setItem(this.key, JSON.stringify(items));
    this.items = items;
  }
  create(name, urls) {
    name = name.trim();
    if (!name || name.length > 60) throw new Error("请输入 1–60 字的歌单名称");
    if (this.items.some(l => l.name === name)) throw new Error("已有同名歌单");
    const list = {id: crypto.randomUUID(), name, urls: [...new Set(urls)]};
    this.commit([...this.items, list]);
    return list.id;
  }
  assign(url, ids) {
    this.commit(this.items.map(l => {
      if (ids.has(l.id) === l.urls.includes(url)) return l;
      return {...l, urls: ids.has(l.id) ? [...l.urls, url] : l.urls.filter(u => u !== url)};
    }));
  }
  addSongs(urls, ids) {
    this.commit(this.items.map(l => ids.has(l.id) ? {...l, urls: [...new Set([...l.urls, ...urls])]} : l));
  }
  removeSongs(id, urls) {
    this.commit(this.items.map(l => l.id === id ? {...l, urls: l.urls.filter(u => !urls.has(u))} : l));
  }
  remove(id) { this.commit(this.items.filter(l => l.id !== id)); }
  prune(valid) {
    if (this.items.some(l => l.urls.some(u => !valid.has(u))))
      this.commit(this.items.map(l => ({...l, urls: l.urls.filter(u => valid.has(u))})));
  }
}
