// DriveVault Browser Pro - "picture in picture": make the best <video> of this page fill the (small) window the tab was moved to.
// Run in the page (main frame, main world) when the user pops the video out. Only style and a class are changed; the video element is NOT moved in the DOM,
// so it keeps playing and keeps its position, also when the picture comes from a stream the page builds itself (MediaSource / blob / canvas).
// The undo is pip-off.js. Returns JSON: {ok, w, h, paused, title}.
(function () {
    var vs = [].slice.call(document.querySelectorAll("video"));
    var score = function (v) { return (v.paused ? 0 : 1e12) + (v.readyState >= 2 ? 1e9 : 0) + (v.videoWidth || 0) * (v.videoHeight || 0); };
    vs = vs.filter(function (v) { return v.currentSrc || v.srcObject || v.querySelector("source"); });
    if (!vs.length) return JSON.stringify({ ok: false });
    vs.sort(function (a, b) { return score(b) - score(a); });
    var v = vs[0];
    var st = document.getElementById("__dv_pip") || document.createElement("style");
    st.id = "__dv_pip";
    st.textContent =
        "html,body{background:#000!important;overflow:hidden!important}" +
        "body *{visibility:hidden!important;transform:none!important;filter:none!important;contain:none!important;will-change:auto!important;perspective:none!important}" +
        "video.__dv_pip_v{visibility:visible!important;position:fixed!important;left:0!important;top:0!important;right:auto!important;bottom:auto!important;" +
        "width:100vw!important;height:100vh!important;max-width:none!important;max-height:none!important;margin:0!important;padding:0!important;border:0!important;" +
        "object-fit:contain!important;background:#000!important;z-index:2147483647!important;opacity:1!important}";
    document.documentElement.appendChild(st);
    v.dataset.dvCtl = v.controls ? "1" : "0";
    v.classList.add("__dv_pip_v");
    v.controls = true;
    return JSON.stringify({ ok: true, w: v.videoWidth, h: v.videoHeight, paused: v.paused, title: document.title });
})();
