# Anti-adblock defence: what was tested and what was not

Version 0.4.0, written 2026-11-19. The program's defence is in `resources/adblock-page.js` (section "defence", switch "Defend against pages that detect ad blockers", default on, `antiDetectEnabled`) and in the stand-in files (`src/adblock/StandIns.cpp`). **Nothing here is a promise that "every site works"**: this is what each detector did in the lab and what is known to be open.

## Lab (pages on this computer, real Chromium; `tests/tst_lab.cpp`, test `anAdBlockerDetectorFindsNothing`)
Every row was run with the defence ON and with it OFF (the control). "Detected" = the page's own check said "ad blocker found".

| Detector (how real pages do it) | Defence OFF | Defence ON | How it is answered |
|---|---|---|---|
| Bait box with inline style `width:1px; height:1px; left:-10000px` and ad class names (BlockAdBlock style), measured after 400 ms | detected | clear | an empty box (no child, no text) is never hidden |
| Bait in normal flow holding only a space (`&nbsp;`), class `ad-banner` | detected | clear | same rule |
| Bait made tiny by a style sheet (not inline) | detected | clear | same rule |
| A detector library defined by the page (FuckAdBlock-style: `onDetected` / `onNotDetected` / `check`) | detected | clear | the names `FuckAdBlock`, `BlockAdBlock`, `SniffAdBlock` (and the lower-case instances) are replaced by an object that always says "not detected"; the page cannot overwrite them |
| Bait script `/ads.js` that must set `canRunAds` | clear | clear | stand-in file (also without the defence; Phase 3) |
| `fetch(..., {method: 'HEAD', mode: 'no-cors'})` to an ad address (googlesyndication) | clear | clear | the request is answered by a stand-in, so it "succeeds" |
| Picture from an ad address (`new Image()`, `onerror` = detected) | clear | clear | a blocked picture loads as a transparent dot |
| Wall "Ad blocker detected - please disable your ad blocker" (fixed, full window) that also locks scrolling (`overflow: hidden`) | stays, scroll locked | hidden, scroll free | a fixed / high z-index box over 50 % x 30 % of the window whose text talks about ad blockers is hidden; `html, body` overflow and position are released |
| False-positive controls: cookie notice at the bottom; a paragraph that talks about "ad blocker extensions" inside the article; a full-window box "Subscribe to our newsletter"; a real ad (box with text) | all kept / the ad hidden | all kept / the ad hidden | not touched / hidden as before |

## Known open (not solved, not tested)
- **Real websites: not tested at all.** All rows above are pages written by us. A table of real sites with a date per site is still empty (it needs the owner's choice of sites and permission to use the network).
- Pages that check **on their server** (an ad must be requested and a callback must reach the server), pages that keep the **content hidden until an ad has been shown**, and **server-side ad insertion in video streams** (YouTube style) cannot be solved by a client-side filter.
- A bait that has a **child element or text** is hidden like any ad (this is what real ads look like); a detector that builds its bait that way still sees it hidden.
- Detectors that read **`getComputedStyle` / sizes of a real ad slot** (for example `ins.adsbygoogle` with height 0) still see the hidden slot. Spoofing sizes was not built.
- Detection by **blocked requests the table does not know** (an ad address that is not in the stand-in table and is answered with an error) still fails the page's test. Lists for scriptlets / anti-adblock (uBlock filters, Adblock Warning Removal List) are not chosen and not downloaded.
- Walls in **iframes** and walls whose text does not contain the words searched (English and Thai words for "ad blocker" / "disable ads") are not found.
- The wall hider could hide a legitimate full-window box that talks about ad blockers; the conditions (fixed or high z-index, large, the words) were chosen to avoid it, and the false-positive rows above are the only checks.