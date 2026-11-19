// Undo of pip-on.js: the page looks as it did before the video was popped out. Returns "ok".
(function () {
    var v = document.querySelector("video.__dv_pip_v");
    if (v) {
        v.classList.remove("__dv_pip_v");
        v.controls = v.dataset.dvCtl === "1";
        delete v.dataset.dvCtl;
    }
    var st = document.getElementById("__dv_pip");
    if (st) st.remove();
    return "ok";
})();
