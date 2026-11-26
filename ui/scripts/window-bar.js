// Only the trusted local WebView page is allowed to post window commands.
(() => {
  const bridge = window.chrome?.webview;
  const bar = document.getElementById('windowBar');
  const max = document.getElementById('windowMax');
  const send = action => bridge?.postMessage(`window ${action}`);
  const exitDialog = document.getElementById('exitDialog');
  const remember = document.getElementById('rememberExit');
  const reset = document.getElementById('resetExit');
  const prefKey = 'titleBarCloseAction';
  const validAction = action => action === 'minimize' || action === 'close';
  function preference() {
    try { return localStorage.getItem(prefKey); } catch { return null; }
  }
  function refreshReset() { reset.disabled = !validAction(preference()); }
  refreshReset();
  window.addEventListener('storage', refreshReset);
  reset.onclick = () => {
    try { localStorage.removeItem(prefKey); } catch {}
    refreshReset();
  };
  function requestClose() {
    const action = preference();
    if (validAction(action)) return send(action);
    if (exitDialog.open) return;
    remember.checked = false;
    exitDialog.showModal();
  }
  document.getElementById('exitForm').onsubmit = event => {
    event.preventDefault();
    const action = event.submitter?.value;
    if (!validAction(action)) return;
    if (remember.checked) {
      try { localStorage.setItem(prefKey, action); } catch {}
      refreshReset();
    }
    exitDialog.close();
    send(action);
  };
  let nativeDrag = true;
  for (const button of bar.querySelectorAll('[data-window]')) {
    button.addEventListener('click', () => button.dataset.window === 'close' ? requestClose() : send(button.dataset.window));
  }
  bridge?.addEventListener('message', ({data}) => {
    if (!data.windowState) return;
    const {maximized, nativeDrag: supported} = data.windowState;
    nativeDrag = supported;
    bar.classList.toggle('isMaximized', maximized);
    max.title = maximized ? '还原' : '最大化';
    max.setAttribute('aria-label', max.title);
  });

  // Older WebView2 runtimes ignore app-region. Start a native move only after
  // the drag threshold, leaving single and double clicks distinguishable.
  let press;
  bar.addEventListener('pointerdown', e => {
    if (nativeDrag || e.button !== 0 || e.target.closest('.windowControls')) return;
    press = {id: e.pointerId, x: e.screenX, y: e.screenY};
    bar.setPointerCapture(e.pointerId);
  });
  bar.addEventListener('lostpointercapture', () => { press = null; });
  bar.addEventListener('pointermove', e => {
    if (!press || e.pointerId !== press.id || !(e.buttons & 1)) return;
    if (Math.hypot(e.screenX - press.x, e.screenY - press.y) < 4) return;
    press = null;
    bar.releasePointerCapture(e.pointerId);
    send('drag');
  });
  bar.addEventListener('dblclick', e => {
    if (!nativeDrag && !e.target.closest('.windowControls')) send('maximize');
  });
})();
