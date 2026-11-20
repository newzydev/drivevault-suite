// Undo of pip-on.js: the page looks as it did before the video was popped out. Returns "ok".
(function () {
    function restore(doc) {
        var node = doc.querySelector(".__dv_pip_v");
        if (node) {
            node.classList.remove("__dv_pip_v");
            if (node.tagName === "VIDEO") {
                node.controls = node.dataset.dvCtl === "1";
                delete node.dataset.dvCtl;
            } else if (node.contentDocument) {
                restore(node.contentDocument);
            }
        }
        var st = doc.getElementById("__dv_pip");
        if (st) st.remove();
    }
    restore(document);
    return "ok";
})();
