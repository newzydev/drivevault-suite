# Anti-adblock defence: what was tested and what was not

Version 0.4.1, written 2026-11-19 (lab results of 0.4.0; real websites tested with 0.4.1). The program's defence is in `resources/adblock-page.js` (section "defence", switch "Defend against pages that detect ad blockers", default on, `antiDetectEnabled`) and in the stand-in files (`src/adblock/StandIns.cpp`). **Nothing here is a promise that "every site works"**: this is what each detector did in the lab and what is known to be open.

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
- **The defence has not been shown to work on a real ad block wall**: see the real-site section below - 20 real sites were opened and none of them showed such a wall, with or without the defence.
- Pages that check **on their server** (an ad must be requested and a callback must reach the server), pages that keep the **content hidden until an ad has been shown**, and **server-side ad insertion in video streams** (YouTube style) cannot be solved by a client-side filter.
- A bait that has a **child element or text** is hidden like any ad (this is what real ads look like); a detector that builds its bait that way still sees it hidden.
- Detectors that read **`getComputedStyle` / sizes of a real ad slot** (for example `ins.adsbygoogle` with height 0) still see the hidden slot. Spoofing sizes was not built.
- Detection by **blocked requests the table does not know** (an ad address that is not in the stand-in table and is answered with an error) still fails the page's test. Lists for scriptlets / anti-adblock (uBlock filters, Adblock Warning Removal List) are not chosen and not downloaded.
- Walls in **iframes** and walls whose text does not contain the words searched (English and Thai words for "ad blocker" / "disable ads") are not found.
- The wall hider could hide a legitimate full-window box that talks about ad blockers; the conditions (fixed or high z-index, large, the words) were chosen to avoid it, and the false-positive rows above are the only checks.

## Real websites (2026-11-19, program 0.4.1 release build, this PC, the owner allowed network use)
Method: each site opened for 14 s in a fresh profile (no log-in, no clicks, muted) in three configurations - **A** the program as it is (blocker + defence), **B** blocker without the defence, **C** no blocker at all - with the real lists of 2026-11-19 (EasyList, EasyPrivacy, Fanboy Social and Annoyance; 111,416 network rules, 82,563 page rules, 30 scriptlets). Measured by the program's `--survey` diagnostic: length of the page text, number of frames, requests stopped, visible walls whose text talks about ad blockers (fixed or high z-index, large), scroll lock; plus a screenshot per run. 20 sites x 3 = 60 runs. The raw file of that run was overwritten by later partial runs (the table below was copied from the printed summary).

| Site | Text kept with the blocker (A / C) | Requests stopped (A) | Frames A / C | Wall seen (A / B) | Note |
|---|---|---|---|---|---|
| sanook.com | 98 % (9,051 / 9,200) | 23 | 2 / 34 | 0 / 0 | |
| kapook.com | 96 % (9,981 / 10,402) | 14 | 1 / 23 | 0 / 0 | |
| pantip.com | 101 % | 1 | 0 / 0 | 0 / 0 | |
| thairath.co.th | 0 / 108 | 16 | 0 / 9 | 0 / 0 | **not testable**: a blue empty page also without the blocker |
| mthai.com | 100 % | 10 | 0 / 40 | 0 / 0 | |
| dailynews.co.th | 99 % | 14 | 1 / 13 | 0 / 0 | |
| forbes.com | 0 / 0 | 0 | 1 / 1 | 0 / 0 | **not testable**: "Verification Required" (bot check) in every configuration |
| businessinsider.com | 100 % | 13 | 2 / 19 | 0 / 0 | |
| theguardian.com | 100 % | 12 | 2 / 2 | 0 / 0 | scroll locked by the site's own consent dialog in A and C alike |
| edition.cnn.com | 100 % (12,630 / 12,642) | 34 | 6 / 33 | 0 / 0 | the ad is gone, its slot stays as an empty dark box (same in B; the site reserves the space) |
| imdb.com | 101 % | 23 | 1 / 1 | 0 / 0 | |
| w3schools.com | 100 % | 3 | 1 / 16 | 0 / 0 | |
| speedtest.net | 265 / 265 | 0 | 0 / 0 | 0 / 0 | **not testable**: a Cloudflare "Just a moment..." challenge in every configuration |
| independent.co.uk | 100 % | 23 | 3 / 22 | 0 / 0 | |
| wired.com | 100 % | 10 | 1 / 17 | 0 / 0 | |
| nytimes.com | 0 / 0 | 0 | 1 / 1 | 0 / 0 | **not testable**: empty page in every configuration |
| bild.de | 100 % | 12 | 3 / 3 | 0 / 0 | scroll locked by a consent dialog in A and C alike |
| thesun.co.uk | 102 % | 24 | 2 / 19 | 0 / 0 | |
| dailymail.co.uk | 100 % | 16 | 3 / 8 | 0 / 0 | |
| cbssports.com | **74 %** (9,605 / 13,063) | 79 | 6 / 27 | 0 / 0 | 26 % of the text is missing with the blocker: not looked into (promotions, betting boxes?) - to be checked |

**What this shows**
- On the 16 sites that loaded in every configuration the blocker stopped 3 - 79 requests per page (median about 14) and removed most advertising frames (for example CNN 33 -> 6, mthai 40 -> 0, sanook 34 -> 2, independent 22 -> 3), and the page content was kept: 15 of the 16 keep 96 - 102 % of their text; one (cbssports) keeps 74 %.
- Pages look right in the screenshots (CBS Sports, Sanook with Thai text, CNN): the page is usable, empty slots of advertising can remain.
- **No ad block wall appeared on any of the 20 sites, in any configuration - including B, without the defence.** So these runs show that the blocker does not break ordinary sites, but they say **nothing** about whether the defence works on a real wall: it has only been shown on lab pages. Four sites (thairath, forbes, speedtest, nytimes) could not be judged at all.
- Not measured: page load speed on real sites, video pages, log-in pages, sites reached from other countries, repeat visits (a wall often appears only after some visits), mobile layouts.

**Faults found by this run (and what was done)**
- The program crashed (`Qt6Qml.dll`, access violation) when it was ended in configuration C on a heavy page: 6 of 16 runs at first. Cause: the tab model was destroyed before the pages that still write into it. Fixed in 0.4.1 (model declared after the views, the pages are stopped and the model cleared in `onClosing`, a closed window is destroyed 2 s later). After the fix 2 of 20 runs crashed, then 0 of 6 repeats on those two sites: **improved, not proven fixed**.
- The program ignored `DV_NO_AUDIO` (only the tests set it): pages could play sound in the first runs of this test. Fixed in 0.4.1 (`--mute-audio` when the variable is set); the final 60 runs were muted.
- A diagnostic bug of mine (a reference to a variable that no longer existed in `main.cpp`) made `--survey` end at once; fixed.