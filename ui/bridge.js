"use strict";

// 页面只使用命令、响应和事件；C++ 类型及控件由后端适配器隔离。
window.bridge = (() => {
  const transport = window.chrome?.webview;
  const pending = new Map(), listeners = new Map();
  let sequence = 0;
  if (transport) transport.addEventListener("message", ({data}) => {
    if (data.version !== 1) return;
    if (data.event) {
      for (const listener of listeners.get(data.event) || []) listener(data.payload, data.requestId);
    } else if (pending.has(data.id)) {
      const request = pending.get(data.id);
      pending.delete(data.id);
      if (data.ok) request.resolve(data.payload);
      else request.reject(data.error);
    }
  });
  return {
    available: !!transport,
    request(command, payload = {}) {
      const id = "request-" + (++sequence);
      const promise = new Promise((resolve, reject) => {
        if (!transport) { reject({code: "hostUnavailable"}); return; }
        pending.set(id, {resolve, reject});
        transport.postMessage({version: 1, id, command, payload});
      });
      promise.id = id;
      return promise;
    },
    on(event, listener) {
      if (!listeners.has(event)) listeners.set(event, []);
      listeners.get(event).push(listener);
    }
  };
})();
