// DriveVault Browser Pro - the script that runs inside a page before the page's own scripts (main world, main frame).
// Written for this program. It is wrapped by the program as:  (function(){ var CFG = {...}; <this file> })();
//   CFG.sel   specific hide selectors     CFG.css  style rules to add         CFG.sl   [[scriptlet, [args]], ...]
//   CFG.gen   hide generic names (true/false)  CFG.cls / CFG.ids  names of the generic ".class" / "#id" rules, space separated   CFG.goff  names switched off here
// Nothing is added to the page's global scope; styles go into a constructable style sheet (no <style> element, not stopped by the page's CSP).
"use strict";
var win = window, doc = document;
var defProp = Object.defineProperty;
var setT = win.setTimeout.bind(win);                       // our own references: scriptlets below may replace the page's timers
var origToString = Function.prototype.toString;

function safe(fn) { try { return fn(); } catch (e) { return undefined; } }

// a needle: "" = always, "/regex/" = regular expression, "!x" = not, anything else = contained text
function matcher(needle) {
    if (needle === undefined || needle === null || needle === "") return function () { return true; };
    var s = String(needle), neg = false;
    if (s.charAt(0) === "!") { neg = true; s = s.slice(1); }
    var test;
    if (s.length > 2 && s.charAt(0) === "/" && s.charAt(s.length - 1) === "/") {
        var rx = null;
        try { rx = new RegExp(s.slice(1, -1)); } catch (e) { rx = null; }
        test = function (x) { return rx !== null && rx.test(String(x)); };
    } else {
        test = function (x) { return String(x).indexOf(s) !== -1; };
    }
    return neg ? function (x) { return !test(x); } : test;
}

function constant(text) {
    switch (text) {
    case "undefined": return undefined;
    case "false": return false;
    case "true": return true;
    case "null": return null;
    case "noopFunc": return function () {};
    case "trueFunc": return function () { return true; };
    case "falseFunc": return function () { return false; };
    case "throwFunc": return function () { throw new Error("blocked"); };
    case "emptyStr": case "": return "";
    case "emptyArr": return [];
    case "emptyObj": return {};
    case "yes": case "no": return text;
    case "-1": return -1;
    }
    if (/^-?\d+(\.\d+)?$/.test(text)) { var n = Number(text); if (Math.abs(n) <= 32767) return n; }
    return undefined;
}

// Put a trap on the last part of a dotted path ("a.b.c"): when a part in the middle does not exist yet, wait until the page creates it.
function walkChain(chain, leaf) {
    var parts = String(chain).split(".");
    (function step(owner, i) {
        var key = parts[i];
        if (i === parts.length - 1) { leaf(owner, key); return; }
        var cur;
        try { cur = owner[key]; } catch (e) { return; }
        if (cur !== null && (typeof cur === "object" || typeof cur === "function")) { step(cur, i + 1); return; }
        var d = Object.getOwnPropertyDescriptor(owner, key);
        if (d && d.configurable === false) return;
        var held = cur;
        defProp(owner, key, {
            configurable: true, enumerable: true,
            get: function () { return held; },
            set: function (v) { held = v; if (v !== null && (typeof v === "object" || typeof v === "function")) step(v, i + 1); }
        });
    })(win, 0);
}

function abortMessage() { return "ReferenceError: " + Math.random().toString(36).slice(2, 10) + " is not defined"; }

var S = {};                                                  // the scriptlets, by canonical name

S["abort-on-property-read"] = function (chain) {
    walkChain(chain, function (o, k) {
        var held = safe(function () { return o[k]; });
        defProp(o, k, { configurable: true, enumerable: true, get: function () { throw new ReferenceError(abortMessage()); }, set: function (v) { held = v; } });
    });
};

S["abort-on-property-write"] = function (chain) {
    walkChain(chain, function (o, k) {
        var held = safe(function () { return o[k]; });
        defProp(o, k, { configurable: true, enumerable: true, get: function () { return held; }, set: function () { throw new ReferenceError(abortMessage()); } });
    });
};

function abortWhen(chain, needle, useStack) {
    var m = matcher(needle);
    function hit() {
        if (useStack) {
            var st = "";
            try { throw new Error(); } catch (e) { st = String(e.stack || ""); }
            return m(st);
        }
        var cs = doc.currentScript;
        if (!cs) return false;
        return m(cs.src ? cs.src : cs.textContent);
    }
    walkChain(chain, function (o, k) {
        var held = safe(function () { return o[k]; });
        defProp(o, k, {
            configurable: true, enumerable: true,
            get: function () { if (hit()) throw new ReferenceError(abortMessage()); return held; },
            set: function (v) { held = v; }
        });
    });
}
S["abort-current-script"] = function (chain, needle) { abortWhen(chain, needle, false); };
S["abort-on-stack-trace"] = function (chain, needle) { abortWhen(chain, needle, true); };

S["set-constant"] = function (chain, value) {
    var v = constant(value);
    if (v === undefined && value !== "undefined") return;       // a value this program does not know: do nothing
    walkChain(chain, function (o, k) {
        defProp(o, k, { configurable: true, enumerable: true, get: function () { return v; }, set: function () {} });
    });
};

S["no-xhr-if"] = function (needle) {
    var XHR = win.XMLHttpRequest;
    if (!XHR) return;
    var m = matcher(needle), info = new WeakMap(), open = XHR.prototype.open, send = XHR.prototype.send;
    XHR.prototype.open = function (method, url) { info.set(this, { m: String(method), u: String(url) }); return open.apply(this, arguments); };
    XHR.prototype.send = function () {
        var i = info.get(this);
        if (i && m(i.u + " " + i.m)) {
            var x = this;
            ["readyState", "status", "statusText", "response", "responseText", "responseURL"].forEach(function (p) {
                var val = p === "readyState" ? 4 : p === "status" ? 200 : p === "statusText" ? "OK" : p === "responseURL" ? i.u : "";
                safe(function () { defProp(x, p, { value: val, configurable: true }); });
            });
            setT(function () { ["readystatechange", "load", "loadend"].forEach(function (t) { safe(function () { x.dispatchEvent(new Event(t)); }); }); }, 1);
            return;
        }
        return send.apply(this, arguments);
    };
};

S["no-fetch-if"] = function (needle) {
    var f = win.fetch;
    if (typeof f !== "function") return;
    var m = matcher(needle);
    win.fetch = function (input, init) {
        var url = typeof input === "string" ? input : (input && input.url) || String(input);
        var method = (init && init.method) || (input && input.method) || "GET";
        if (m(url + " " + method)) return Promise.resolve(new Response("", { status: 200 }));
        return f.apply(this, arguments);
    };
};

function timerDefuser(name, needle, delay) {
    var orig = win[name];
    if (typeof orig !== "function") return;
    var m = matcher(needle), dm = (delay === undefined || delay === "") ? null : (function () {
        var neg = String(delay).charAt(0) === "!", n = Number(neg ? String(delay).slice(1) : delay);
        return function (d) { return neg ? Number(d) !== n : Number(d) === n; };
    })();
    win[name] = function (cb, d) {
        var text = typeof cb === "function" ? origToString.call(cb) : String(cb);
        if (m(text) && (dm === null || dm(d))) return 0;
        return orig.apply(this, arguments);
    };
}
S["no-setTimeout-if"] = function (needle, delay) { timerDefuser("setTimeout", needle, delay); };
S["no-setInterval-if"] = function (needle, delay) { timerDefuser("setInterval", needle, delay); };

function timerBooster(name, needle, delay, boost) {
    var orig = win[name];
    if (typeof orig !== "function") return;
    var m = matcher(needle), want = (delay === undefined || delay === "") ? 1000 : Number(delay), b = (boost === undefined || boost === "") ? 0.05 : Number(boost);
    if (!(b > 0) || !(b <= 50)) return;
    win[name] = function (cb, d) {
        var text = typeof cb === "function" ? origToString.call(cb) : String(cb);
        var args = Array.prototype.slice.call(arguments);
        if (Number(d) === want && m(text)) args[1] = Math.max(1, Number(d) * b);
        return orig.apply(this, args);
    };
}
S["adjust-setTimeout"] = function (needle, delay, boost) { timerBooster("setTimeout", needle, delay, boost); };
S["adjust-setInterval"] = function (needle, delay, boost) { timerBooster("setInterval", needle, delay, boost); };

S["prevent-addEventListener"] = function (type, needle) {
    var proto = win.EventTarget && win.EventTarget.prototype;
    if (!proto) return;
    var mt = matcher(type), mn = matcher(needle), orig = proto.addEventListener;
    proto.addEventListener = function (t, listener) {
        var text = typeof listener === "function" ? origToString.call(listener) : (listener && typeof listener.handleEvent === "function" ? origToString.call(listener.handleEvent) : String(listener));
        if (mt(t) && mn(text)) return;
        return orig.apply(this, arguments);
    };
};

function prunePath(o, path) {
    var parts = path.split(".");
    (function go(node, i) {
        if (!node || typeof node !== "object") return;
        var k = parts[i];
        if (i === parts.length - 1) {
            if (k === "[]" || k === "*") { if (Array.isArray(node)) node.length = 0; } else { delete node[k]; }
            return;
        }
        if (k === "[]" || k === "*") { Object.keys(node).forEach(function (kk) { go(node[kk], i + 1); }); } else { go(node[k], i + 1); }
    })(o, 0);
}
function has(o, path) {
    var cur = o, parts = path.split(".");
    for (var i = 0; i < parts.length; ++i) { if (!cur || typeof cur !== "object" || !(parts[i] in cur)) return false; cur = cur[parts[i]]; }
    return true;
}
S["json-prune"] = function (props, required) {
    var list = String(props || "").split(/\s+/).filter(Boolean), need = String(required || "").split(/\s+/).filter(Boolean);
    if (list.length === 0) return;
    function work(r) {
        if (!r || typeof r !== "object") return r;
        if (need.length && !need.every(function (p) { return has(r, p); })) return r;
        list.forEach(function (p) { prunePath(r, p); });
        return r;
    }
    var parse = JSON.parse;
    JSON.parse = function () { return work(parse.apply(this, arguments)); };
    if (win.Response && win.Response.prototype.json) {
        var rj = win.Response.prototype.json;
        win.Response.prototype.json = function () { return rj.apply(this, arguments).then(work); };
    }
};

var mutationTasks = [];
function onDom(fn) {
    mutationTasks.push(fn);
    if (mutationTasks.length === 1) {
        var queued = false;
        function run() { queued = false; mutationTasks.forEach(function (t) { safe(t); }); }
        function queue() { if (!queued) { queued = true; setT(run, 30); } }
        doc.addEventListener("DOMContentLoaded", queue);
        new MutationObserver(queue).observe(doc, { childList: true, subtree: true, attributes: true });
        queue();
    }
}
S["remove-attr"] = function (attrs, selector) {
    var list = String(attrs || "").split("|").filter(Boolean), sel = selector || "[" + list[0] + "]";
    if (!list.length) return;
    onDom(function () { doc.querySelectorAll(sel).forEach(function (el) { list.forEach(function (a) { el.removeAttribute(a); }); }); });
};
S["remove-class"] = function (names, selector) {
    var list = String(names || "").split("|").filter(Boolean), sel = selector || list.map(function (c) { return "." + c; }).join(",");
    if (!list.length) return;
    onDom(function () { doc.querySelectorAll(sel).forEach(function (el) { list.forEach(function (c) { el.classList.remove(c); }); }); });
};

var SAFE_VALUES = ["true", "false", "1", "0", "yes", "no", "on", "off", "accept", "accepted", "reject", "rejected", "allow", "allowed", "deny", "denied", "necessary", "essential", "required", "t", "f", "y", "n", "ok", "enabled", "disabled", "granted", "denied", "dismiss", "dismissed", "agree", "disagree", "optout", "opt-out", "optin", "opt-in", "emptyStr"];
function storageItem(kind, key, value) {
    var st = safe(function () { return win[kind]; });
    if (!st || !key) return;
    if (value === "$remove$") { safe(function () { st.removeItem(key); }); return; }
    if (SAFE_VALUES.indexOf(value) === -1 && !/^-?\d{1,5}$/.test(String(value))) return;
    safe(function () { st.setItem(key, value === "emptyStr" ? "" : value); });
}
S["set-local-storage-item"] = function (key, value) { storageItem("localStorage", key, value); };
S["set-session-storage-item"] = function (key, value) { storageItem("sessionStorage", key, value); };

function setCookie(name, value, path, reload) {
    if (!name || /[;=\s,]/.test(name)) return;
    if (SAFE_VALUES.indexOf(value) === -1 && !/^-?\d{1,5}$/.test(String(value))) return;
    var v = value === "emptyStr" ? "" : value, before = safe(function () {
        var m = doc.cookie.split("; ").filter(function (c) { return c.indexOf(name + "=") === 0; })[0];
        return m === undefined ? undefined : m.slice(name.length + 1);
    });
    if (before === v) return;
    var p = !path || path === "/" ? "/" : String(path).replace(/[;\s]/g, "");
    safe(function () { doc.cookie = name + "=" + v + "; path=" + p + "; max-age=31536000"; });
    if (reload) safe(function () { win.location.reload(); });
}
S["set-cookie"] = function (name, value, path) { setCookie(name, value, path, false); };
S["set-cookie-reload"] = function (name, value, path) { setCookie(name, value, path, true); };

S["cookie-remover"] = function (needle) {
    var m = matcher(needle);
    function go() {
        String(doc.cookie).split(";").forEach(function (c) {
            var name = c.split("=")[0].trim();
            if (!name || !m(name)) return;
            var host = win.location.hostname.split(".");
            for (var i = 0; i < host.length - 1; ++i)
                safe(function () { doc.cookie = name + "=; expires=Thu, 01 Jan 1970 00:00:00 GMT; path=/; domain=" + host.slice(i).join("."); });
            safe(function () { doc.cookie = name + "=; expires=Thu, 01 Jan 1970 00:00:00 GMT; path=/"; });
        });
    }
    go();
    win.addEventListener("beforeunload", go);
};

S["no-window-open-if"] = function (needle) {
    var open = win.open, m = matcher(needle);
    win.open = function (url) { if (m(String(url))) return null; return open.apply(this, arguments); };
};

S["nowebrtc"] = function () {
    var Stub = function () {};
    Stub.prototype = { close: function () {}, createDataChannel: function () { return {}; }, createOffer: function () { return Promise.resolve({}); },
        setLocalDescription: function () { return Promise.resolve(); }, addEventListener: function () {}, removeEventListener: function () {} };
    win.RTCPeerConnection = Stub;
    win.webkitRTCPeerConnection = Stub;
};

// ---- scriptlets first: they must be in place before the page's scripts run -------------------------------------------------------
(CFG.sl || []).forEach(function (call) {
    var fn = S[call[0]];
    if (typeof fn === "function") safe(function () { fn.apply(null, call[1]); });
});

// ---- hiding -------------------------------------------------------------------------------------------------------------------------
function makeSheet() {
    try {
        var s = new CSSStyleSheet();
        doc.adoptedStyleSheets = doc.adoptedStyleSheets.concat([s]);
        return s;
    } catch (e) { return null; }
}
function addRules(sheet, selectors) {
    var CHUNK = 40, rule = "{display:none!important}";
    for (var i = 0; i < selectors.length; i += CHUNK) {
        var chunk = selectors.slice(i, i + CHUNK);
        try { sheet.insertRule(chunk.join(",") + rule, sheet.cssRules.length); }
        catch (e) { chunk.forEach(function (sel) { safe(function () { sheet.insertRule(sel + rule, sheet.cssRules.length); }); }); }
    }
}
var pageSheet = null;
if ((CFG.sel && CFG.sel.length) || (CFG.css && CFG.css.length)) {
    pageSheet = makeSheet();
    if (pageSheet) {
        if (CFG.sel && CFG.sel.length) addRules(pageSheet, CFG.sel);
        (CFG.css || []).forEach(function (text) { safe(function () { pageSheet.insertRule(text, pageSheet.cssRules.length); }); });
    }
}

// generic rules (".name" / "#name"): only names that really occur in the page are given a rule
if (CFG.gen) {
    var classNames = new Set(CFG.cls ? CFG.cls.split(" ") : []), idNames = new Set(CFG.ids ? CFG.ids.split(" ") : []);
    var off = new Set(CFG.goff || []), seenC = new Set(), seenI = new Set(), found = [], genSheet = null;
    var checkEl = function (el) {
        var id = el.id;
        if (id && typeof id === "string" && !seenI.has(id)) {
            seenI.add(id);
            if (idNames.has(id) && !off.has("#" + id) && /^[\w-]+$/.test(id)) found.push('[id="' + id + '"]');
        }
        var cl = el.classList;
        if (cl) for (var i = 0; i < cl.length; ++i) {
            var c = cl[i];
            if (seenC.has(c)) continue;
            seenC.add(c);
            if (classNames.has(c) && !off.has("." + c) && /^[\w-]+$/.test(c)) found.push('[class~="' + c + '"]');
        }
    };
    var queue = [], queued = false;
    var flush = function () {
        queued = false;
        var work = queue; queue = [];
        work.forEach(function (n) {
            if (n.nodeType !== 1) return;
            checkEl(n);
            if (n.querySelectorAll) n.querySelectorAll("[class],[id]").forEach(checkEl);
        });
        if (found.length) {
            if (!genSheet) genSheet = makeSheet();
            if (genSheet) addRules(genSheet, found);
            found = [];
        }
    };
    var push = function (n) { queue.push(n); if (!queued) { queued = true; setT(flush, 20); } };
    new MutationObserver(function (records) {
        records.forEach(function (r) {
            if (r.type === "attributes") push(r.target);
            else r.addedNodes.forEach(push);
        });
    }).observe(doc, { childList: true, subtree: true, attributes: true, attributeFilter: ["class", "id"] });
    doc.addEventListener("DOMContentLoaded", function () { push(doc.documentElement); });
    if (doc.documentElement) push(doc.documentElement);
}
