#include "CallbackPage.h"

#include <QString>

namespace dv::account {
namespace {

// Tokens: @@KIND@@ (ok | no), @@ICON@@, @@TITLE_TH@@, @@TITLE_EN@@, @@TEXT_TH@@, @@TEXT_EN@@, @@BUTTON@@.
const char kTemplate[] = R"HTML(<!doctype html>
<html lang="th">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="color-scheme" content="dark">
<title>DriveVault</title>
<style>
:root{--bg:#0D0E11;--card:#16181D;--line:#2A2D36;--text:#F2F3F6;--muted:#9AA0AD;--amber:#F59E0B;--violet:#8B5CF6;--red:#EF4444;--ok:#22C55E;--warn:#F59E0B;color-scheme:dark}
*{box-sizing:border-box}
html,body{height:100%}
body{margin:0;display:grid;place-items:center;background:var(--bg);color:var(--text);overflow:hidden;
  font:16px/1.6 "Inter","Segoe UI Variable","Segoe UI","Noto Sans Thai","Leelawadee UI",system-ui,sans-serif;-webkit-font-smoothing:antialiased}
.glow{position:fixed;inset:-10%;pointer-events:none;filter:blur(8px);
  background:
   radial-gradient(520px 380px at 16% 14%,rgba(139,92,246,.26),transparent 70%),
   radial-gradient(560px 400px at 88% 20%,rgba(245,158,11,.22),transparent 70%),
   radial-gradient(520px 380px at 58% 104%,rgba(239,68,68,.18),transparent 70%);
  animation:drift 14s ease-in-out infinite alternate}
.grain{position:fixed;inset:0;pointer-events:none;opacity:.5;
  background-image:linear-gradient(rgba(255,255,255,.025) 1px,transparent 1px),linear-gradient(90deg,rgba(255,255,255,.025) 1px,transparent 1px);
  background-size:44px 44px;-webkit-mask-image:radial-gradient(ellipse at center,#000 30%,transparent 75%);mask-image:radial-gradient(ellipse at center,#000 30%,transparent 75%)}
main{position:relative;width:min(540px,calc(100vw - 32px));padding:40px 40px 28px;text-align:center;border-radius:28px;
  border:1px solid var(--line);
  background:linear-gradient(180deg,rgba(255,255,255,.055),rgba(255,255,255,.012) 38%),var(--card);
  box-shadow:0 40px 90px rgba(0,0,0,.6),0 0 0 1px rgba(255,255,255,.02),inset 0 1px 0 rgba(255,255,255,.07);
  animation:rise .7s cubic-bezier(.2,.8,.2,1) both}
main::before{content:"";position:absolute;left:12%;right:12%;top:-1px;height:2px;border-radius:2px;
  background:linear-gradient(90deg,var(--violet),var(--amber),var(--red));opacity:.9}
.brand{display:inline-flex;align-items:center;gap:10px;margin-bottom:26px;font-weight:700;letter-spacing:.2px;font-size:15px;color:var(--muted)}
.brand b{color:var(--text);font-weight:700}
.dots{display:inline-flex;gap:5px}
.dots i{width:8px;height:8px;border-radius:50%;display:block}
.dots i:nth-child(1){background:var(--amber)}.dots i:nth-child(2){background:var(--violet)}.dots i:nth-child(3){background:var(--red)}
.badge{position:relative;width:92px;height:92px;margin:0 auto 22px;display:grid;place-items:center}
.badge::before{content:"";position:absolute;inset:0;border-radius:50%;background:radial-gradient(circle at 50% 35%,rgba(255,255,255,.1),transparent 62%),rgba(255,255,255,.03);
  border:1px solid var(--line)}
.badge::after{content:"";position:absolute;inset:-14px;border-radius:50%;border:1px solid var(--tint);opacity:.0;animation:ring 2.4s ease-out .5s infinite}
.badge svg{position:relative;width:46px;height:46px;overflow:visible}
.ok{--tint:var(--ok)}.no{--tint:var(--warn)}
.badge .halo{position:absolute;inset:6px;border-radius:50%;box-shadow:0 0 38px var(--tint);opacity:.38}
.badge path{fill:none;stroke:var(--tint);stroke-width:3.6;stroke-linecap:round;stroke-linejoin:round;stroke-dasharray:60;stroke-dashoffset:0;animation:draw .7s .35s cubic-bezier(.3,.7,.2,1) both}
h1{margin:0;font-size:27px;line-height:1.3;font-weight:700;letter-spacing:-.2px}
h2{margin:2px 0 14px;font-size:15px;font-weight:500;color:var(--muted)}
p{margin:0 auto;max-width:420px;color:#C6CAD4;font-size:15px}
p.en{margin-top:6px;color:var(--muted);font-size:14px}
.apps{display:flex;flex-wrap:wrap;justify-content:center;gap:8px;margin:24px 0 6px}
.apps span{display:inline-flex;align-items:center;gap:8px;padding:6px 12px;border-radius:999px;border:1px solid var(--line);background:rgba(255,255,255,.03);font-size:13px;font-weight:600;color:#D7DAE2}
.apps span i{width:8px;height:8px;border-radius:50%;display:block;box-shadow:0 0 10px currentColor}
button{margin-top:22px;appearance:none;cursor:pointer;border:0;border-radius:14px;padding:12px 26px;font:inherit;font-weight:700;font-size:15px;color:#14161B;
  background:linear-gradient(135deg,#FBBF24,#F59E0B);box-shadow:0 10px 26px rgba(245,158,11,.28),inset 0 1px 0 rgba(255,255,255,.45);transition:transform .15s ease,box-shadow .15s ease}
button:hover{transform:translateY(-1px);box-shadow:0 14px 32px rgba(245,158,11,.36),inset 0 1px 0 rgba(255,255,255,.5)}
button:active{transform:translateY(0)}
button:focus-visible{outline:2px solid #fff;outline-offset:3px}
.hint{min-height:22px;margin-top:12px;font-size:13px;color:var(--muted)}
footer{margin-top:20px;padding-top:16px;border-top:1px solid var(--line);font-size:12px;color:#7D8391}
@keyframes rise{from{opacity:0;transform:translateY(18px) scale(.985)}to{opacity:1;transform:none}}
@keyframes draw{from{stroke-dashoffset:60}to{stroke-dashoffset:0}}
@keyframes ring{0%{transform:scale(.86);opacity:.55}100%{transform:scale(1.28);opacity:0}}
@keyframes drift{from{transform:translate3d(-1.5%,0,0) scale(1)}to{transform:translate3d(1.5%,1%,0) scale(1.06)}}
@media (prefers-reduced-motion:reduce){*,*::before,*::after{animation:none!important;transition:none!important}}
@media (max-width:480px){main{padding:32px 22px 22px;border-radius:22px}h1{font-size:23px}}
</style>
</head>
<body>
<div class="glow"></div><div class="grain"></div>
<main class="@@KIND@@" role="main">
  <div class="brand"><span class="dots" aria-hidden="true"><i></i><i></i><i></i></span><span><b>DriveVault</b></span></div>
  <div class="badge" aria-hidden="true"><span class="halo"></span>@@ICON@@</div>
  <h1>@@TITLE_TH@@</h1>
  <h2>@@TITLE_EN@@</h2>
  <p>@@TEXT_TH@@</p>
  <p class="en">@@TEXT_EN@@</p>
  <div class="apps" aria-hidden="true">
    <span style="color:#F59E0B"><i style="background:#F59E0B"></i>Master Pro</span>
    <span style="color:#8B5CF6"><i style="background:#8B5CF6"></i>Video Editor Pro</span>
    <span style="color:#EF4444"><i style="background:#EF4444"></i>PDF Pro</span>
  </div>
  <button type="button" id="close">@@BUTTON@@</button>
  <div class="hint" id="hint" role="status"></div>
  <footer>บัญชีนี้เก็บไว้ในเครื่องของคุณเท่านั้น &middot; Stored only on this PC</footer>
</main>
<script>
history.replaceState(null,'',location.pathname);
document.getElementById('close').addEventListener('click',function(){
  window.close();
  setTimeout(function(){document.getElementById('hint').textContent='ปิดแท็บนี้ได้เลย · You can close this tab now'},350);
});
</script>
</body>
</html>
)HTML";

const char kCheck[] = R"(<svg viewBox="0 0 46 46"><path d="M10 24.5l9.2 9.2L36.5 14"/></svg>)";
const char kInfo[] = R"(<svg viewBox="0 0 46 46"><path d="M23 12.5v14"/><path d="M23 34v.2"/></svg>)";

} // namespace

QByteArray callbackPage(bool ok)
{
    QString page = QString::fromUtf8(kTemplate);
    const auto put = [&page](const char* token, const QString& value) { page.replace(QLatin1String(token), value); };
    put("@@KIND@@", ok ? QStringLiteral("ok") : QStringLiteral("no"));
    put("@@ICON@@", QString::fromUtf8(ok ? kCheck : kInfo));
    put("@@TITLE_TH@@", ok ? QStringLiteral("เข้าสู่ระบบสำเร็จ") : QStringLiteral("ยังไม่ได้เข้าสู่ระบบ"));
    put("@@TITLE_EN@@", ok ? QStringLiteral("You are signed in") : QStringLiteral("Sign-in was not completed"));
    put("@@TEXT_TH@@", ok ? QStringLiteral("บัญชี Google ของคุณพร้อมใช้งานในทุกโปรแกรม DriveVault กลับไปที่โปรแกรมได้เลย")
                          : QStringLiteral("การเข้าสู่ระบบถูกยกเลิกหรือเกิดข้อผิดพลาด กลับไปที่โปรแกรมแล้วลองใหม่ได้"));
    put("@@TEXT_EN@@", ok ? QStringLiteral("Your Google account is ready in every DriveVault program. Go back to the app.")
                          : QStringLiteral("The sign-in was cancelled or something went wrong. Go back to the program and try again."));
    put("@@BUTTON@@", QStringLiteral("ปิดหน้านี้ · Close"));
    return page.toUtf8();
}

} // namespace dv::account
