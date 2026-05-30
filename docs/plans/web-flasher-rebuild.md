# Web Flasher Rebuild — Implementation Plan

> **Status:** Plan only — not yet approved by Petar. Do not start implementation
> until the open questions below are answered.
> **Author:** planner sub-agent (Clawy/SDA), 2026-05-30
> **Branch:** `plan/web-flasher` (this commit)

---

## Goal

Rebuild HiveKit's browser-based firmware flasher properly — visually polished,
production-ready, and easy to extend as new sensors are added. Anyone with a
supported browser (Chrome / Edge / Opera, desktop) should be able to land on
`https://petarivanov-msft.github.io/hivekit/`, plug in a XIAO ESP32-C6, click
**Install**, and end up with a working HiveKit Zigbee sensor without ever
opening a terminal.

The flasher must:
1. Pull merged firmware binaries from the latest GitHub Release (so flashing is
   pinned to a versioned, immutable artifact, not whatever happened to be in
   `main` today).
2. Look like part of the HiveKit brand — gradient hero, bold typography, sensor
   cards with a tested/untested status badge (★ green / ☆ amber), dark/light
   mode, mobile-responsive layout (mobile gets a "use desktop Chrome" notice,
   not a broken UI).
3. Be ready for additional sensors with zero re-architecture (per-sensor
   manifest, sensor selector, single Install button per sensor card).
4. Auto-republish whenever a release tag is cut.

## Out of scope

- OTA updates (Phase 2/3 — separate plan).
- Custom domain (uses default `*.github.io` URL for now).
- Firmware signing / secure boot (separate concern, post-MVP).
- A nightly/dev-channel flasher served from `dev` branch builds (deferred —
  see open question Q1).
- Building firmware in this plan; CI already builds individual `.bin` files.
  We only add the merged-bin step + Pages deploy.

## Assumptions (flag any that are wrong before implementing)

- A1. Petar wants a single Pages site at the repo's default URL
  (`https://petarivanov-msft.github.io/hivekit/`), source = "GitHub Actions".
- A2. The flasher only ever serves **stable releases** (tags matching `v*`,
  excluding `*-beta*`, `*-alpha*`, `*-rc*`). Pre-releases are not flashable
  from the web.
- A3. ESP Web Tools (`esp-web-tools` web component) is acceptable — same stack
  the previous v0.3.0 flasher used, same stack florianL21 uses, well-supported
  by Espressif. No need to rewrite on top of raw esptool-js.
- A4. We can `esptool.py merge_bin` the four artifacts (bootloader,
  partition-table, factory, OTA data if any) into a single
  `hivekit-<sensor>-<version>.bin` flashable at offset `0x0`.
- A5. Branding source: there is no logo asset yet. We'll generate a simple SVG
  wordmark in-repo (`flasher/assets/logo.svg`) — Petar can replace later.
- A6. SCD40 is the only currently-tested sensor. All others (BME280, SHT40,
  PIR, etc.) are placeholders not yet built — they don't appear in the flasher
  until their firmware exists.

## Audit — what's missing today vs. a "proper" flasher

| # | Required artifact | State today |
|---|-------------------|-------------|
| 1 | `flasher/index.html` (UI) | ❌ Missing — only `.gitkeep` |
| 2 | `flasher/manifest-<sensor>.json` (ESP Web Tools manifest) | ❌ Missing |
| 3 | `flasher/assets/` (logo, favicon, fonts, CSS) | ❌ Missing |
| 4 | `.github/workflows/release.yml` — tag-driven release + merged-bin upload + Pages deploy | ❌ Missing (only `build.yml` for ephemeral artifacts) |
| 5 | GitHub Releases with `.bin` assets attached | ❌ `v0.3.2` and `v0.3.3-beta.1` have 0 assets |
| 6 | GitHub Pages enabled (source = "GitHub Actions") | ❌ Disabled (`GET /repos/.../pages` → 404) |
| 7 | README link + screenshot pointing to live flasher | ❌ Not present (README still says "Phase 1 — not yet built") |
| 8 | Browser compatibility gate (Chrome/Edge only — block FF/Safari with friendly notice) | ❌ Missing |
| 9 | Sensor "★ Tested / ☆ Untested" badge system | ❌ Missing |
| 10 | Tested-on-real-hardware after rebuild (end-to-end flash from web) | ❌ Pending Petar |

## Tasks

Tasks are independent and can be picked up in any order **except where
`depends-on` is noted**. Each task is sized for one implementer sub-agent run.

---

### Task F1 — Merged-bin release workflow

**Title:** Add `.github/workflows/release.yml` that builds, merges, and
publishes per-sensor `.bin` files on stable tags.

**Acceptance criteria:**
- Triggered by `push` of tags matching `v*` **but only stable** — workflow
  skips on tags containing `-alpha`, `-beta`, `-rc` (job-level `if:` guard).
- Reuses the same `espressif/esp-idf-ci-action@v1` build as `build.yml`
  (don't duplicate logic — refactor into a reusable workflow if it keeps
  things DRY, otherwise inline is fine).
- After the build, runs `esptool.py --chip esp32c6 merge_bin` to combine
  bootloader (`0x0`), partition table (`0x8000`), and `factory` app
  (`0x10000`) into a single `hivekit-scd40-c6-<tag>.bin` flashable at
  offset `0x0`. **Note:** the partition table starts at `0x8000` for
  ESP32-C6; double-check against `partitions.csv` (today: `nvs` at
  `0x9000` so partition table is implicitly `0x8000`).
- Uploads the merged `.bin` AND the individual `.bin` files (bootloader,
  partition-table, app) as assets to the GitHub Release for that tag,
  using `softprops/action-gh-release@v2` (or equivalent). Pin to a SHA in
  the actual implementation.
- Produces a job output `firmware-version` containing the tag (consumed by
  Task F4).
- Workflow fails loudly if `esptool.py merge_bin` fails — never silently
  publishes an incomplete release.

**Files affected:**
- `.github/workflows/release.yml` (new)

**Depends on:** none (but coordinate with F4 — they run in the same workflow
run; either keep them in one file or use `workflow_run`).

**Est. complexity:** medium (~45 min). The merge_bin offsets are the
fiddly bit — verify by manually building once locally and running
`esptool.py merge_bin` to confirm output flashes successfully on Petar's
XIAO before declaring this task done.

---

### Task F2 — ESP Web Tools manifest(s)

**Title:** Author per-sensor ESP Web Tools manifest pointing at GitHub
Release assets.

**Acceptance criteria:**
- File `flasher/manifest-scd40-c6.json` exists and validates against the
  ESP Web Tools manifest schema (https://esphome.github.io/esp-web-tools/).
- `name` = "HiveKit SCD40 (XIAO ESP32-C6)".
- `version` = the **latest stable tag** (this can be hardcoded for now and
  bumped by the release workflow — see F4 — OR templated and substituted
  at deploy time; pick one and document).
- `new_install_prompt_erase: true` so users always start clean.
- `builds[]` entry with `chipFamily: "ESP32-C6"`, single `parts[]` entry
  pointing at the merged `.bin` at `path:
  "https://github.com/petarivanov-msft/hivekit/releases/download/<tag>/hivekit-scd40-c6-<tag>.bin"`,
  `offset: 0`.
- A second placeholder file `flasher/manifest-template.json` (with
  `<SENSOR>` / `<VERSION>` markers) so adding a new sensor in Phase 2 is
  copy-paste.

**Files affected:**
- `flasher/manifest-scd40-c6.json` (new)
- `flasher/manifest-template.json` (new)

**Depends on:** F1 must define the asset URL convention.

**Est. complexity:** small (~20 min).

---

### Task F3 — `flasher/index.html` + branding assets

**Title:** Build the polished landing page with sensor cards and Install
buttons.

**Acceptance criteria:**
- `flasher/index.html` is a single self-contained page (CSS inlined or in
  one stylesheet, no build step). Loads `esp-web-tools` from the official
  CDN pinned to a specific version (e.g.
  `https://unpkg.com/esp-web-tools@10/dist/web/install-button.js?module`).
- **Hero section:** HiveKit wordmark + tagline ("Flash a HiveKit Zigbee
  sensor in 30 seconds — no terminal required."), gradient background
  (palette: pick from existing HiveKit docs/PDFs — see open question Q3).
- **Sensor cards grid:** one card per sensor. Each card shows:
  - sensor name + board (e.g. "SCD40 — XIAO ESP32-C6")
  - status badge: **★ Tested** (green) or **☆ Untested** (amber).
    SCD40 = ★ on day 1 once Petar verifies flash + pair end-to-end.
    All future placeholders = ☆ until proven.
  - one-line description (e.g. "CO₂ + temp + humidity")
  - `<esp-web-install-button manifest="manifest-scd40-c6.json">` Install
    button styled to match HiveKit brand.
- **Browser-compat banner:** detects non-Chromium browsers
  (Firefox/Safari) via `navigator.serial` feature check; if missing,
  hides Install buttons and shows a friendly card explaining "Open this
  page in Chrome, Edge, or Opera on a desktop." Mobile UA also gets a
  "use a desktop" notice.
- **Instructions panel:** numbered steps ("1. Plug XIAO into USB. 2. Hold
  BOOT, tap RESET, release BOOT. 3. Click Install. 4. Pick the
  /dev/ttyUSB / COM port. 5. Wait ~60s. 6. Pair with Z2M."). Tightly
  written, no fluff.
- **Footer:** version (auto-injected by F4 OR hardcoded for now), link to
  GitHub repo, link to Z2M converter setup, "Report issue" link.
- **Dark/light mode:** `prefers-color-scheme` driven, both modes look
  good, no FOUC.
- **Mobile responsive:** layout collapses cleanly down to ~360px. Mobile
  shows the "use desktop" banner prominently above any cards.
- Lighthouse on the deployed page scores ≥ 90 in Performance, Accessibility,
  Best Practices (no build step, so SEO is best-effort).
- No external network requests at runtime except: ESP Web Tools CDN, the
  release-asset .bin download, and Google Fonts (or self-host if Petar
  prefers — see Q4).

**Files affected:**
- `flasher/index.html` (new)
- `flasher/style.css` (new — or inlined; pick one)
- `flasher/assets/logo.svg` (new — generate placeholder SVG wordmark)
- `flasher/assets/favicon.svg` (new)
- `flasher/assets/og-image.png` (new — for social embeds; can be
  Lighthouse-deferred placeholder)

**Depends on:** F2 (manifest path must exist).

**Est. complexity:** large (~60 min). This is where the "looks nice" bar
lives. Reviewer should screenshot the page locally (Python `http.server`)
before approving.

---

### Task F4 — GitHub Pages deploy workflow

**Title:** Deploy the `flasher/` directory to GitHub Pages on every
stable tag (and optionally on `main` for preview).

**Acceptance criteria:**
- Either a separate `.github/workflows/pages.yml`, OR a job appended to
  `release.yml`. **Recommended:** single workflow file `release.yml` with
  jobs `build` → `release` → `deploy-pages` running serially (so Pages
  always points at the latest released firmware).
- Uses `actions/configure-pages@v5`, `actions/upload-pages-artifact@v3`,
  `actions/deploy-pages@v4`. Pin major versions; SHA-pin in the actual
  implementation.
- Permissions block at workflow level: `pages: write`, `id-token: write`.
- Concurrency group `pages` with `cancel-in-progress: false` (don't kill
  in-flight deploys).
- BEFORE uploading, the job substitutes `<VERSION>` placeholders in
  `flasher/manifest-*.json` and `flasher/index.html` with the current
  release tag (sed or envsubst — pick one, document).
- Post-deploy smoke check: workflow `curl -sf
  https://petarivanov-msft.github.io/hivekit/manifest-scd40-c6.json |
  jq .version` and asserts the version matches the tag. Fails the
  workflow if mismatched.

**Files affected:**
- `.github/workflows/release.yml` (extend from F1, or new
  `.github/workflows/pages.yml`)

**Depends on:** F1, F2, F3.

**Est. complexity:** medium (~40 min).

---

### Task F5 — README + docs update

**Title:** Update `README.md` and `docs/roadmap.md` to advertise the
flasher and link to it.

**Acceptance criteria:**
- README "Quickstart for developers" gets a sibling section
  **"Quickstart for users (no terminal)"** at the top, with a link to
  `https://petarivanov-msft.github.io/hivekit/` and a one-line "open in
  Chrome, plug XIAO, click Install".
- README repo-layout block updates `flasher/` description from "Web
  flasher (Phase 1 — not yet built)" to "Web flasher (live at
  https://petarivanov-msft.github.io/hivekit/)".
- `docs/roadmap.md` Phase 1 row for "flasher" gets a status emoji
  upgrade.
- Add `docs/flasher.md` — short developer guide on how to add a new
  sensor card (copy `manifest-template.json`, edit `index.html` to add a
  `<sensor-card>`-equivalent block, raise PR).
- Optional: include one screenshot at `docs/img/flasher-screenshot.png`
  (taken from F3's local preview) — flag if Petar wants this.

**Files affected:**
- `README.md`
- `docs/roadmap.md`
- `docs/flasher.md` (new)
- `docs/img/flasher-screenshot.png` (new, optional)

**Depends on:** F3 (so the screenshot is real), F4 (so the live URL is
real).

**Est. complexity:** small (~25 min).

---

### Task F6 — Manual: enable GitHub Pages

**This is a manual step Petar must do — not a sub-agent task.**

Before F4 can deploy successfully, Petar must:

1. Go to https://github.com/petarivanov-msft/hivekit/settings/pages
2. Under **Source**, select **GitHub Actions**.
3. (Optional) Under **Custom domain**, leave blank for default
   `*.github.io` URL.
4. Save.

After this, the next stable tag push will deploy. If the controller
opens the PR before this is done, the F4 deploy step will fail — the PR
itself will still merge cleanly because F4 only runs on tag pushes, not
PR merges. Document this clearly in the PR description.

**Files affected:** none (GitHub web UI).

**Depends on:** Petar's bandwidth.

**Est. complexity:** trivial (~2 min).

---

## Risks / unknowns

- **R1.** ESP Web Tools v10.x dropped some browsers; verify current
  support matrix and the exact CDN URL before pinning. (esp-web-tools
  v10.2.1 was used in the v0.3.0 era — see mem.py #8439 for the
  "Invalid head of packet (0x45)" failure mode that occurred there;
  re-test on Petar's hardware after F1+F4 land.)
- **R2.** `esptool.py merge_bin` offset for ESP32-C6 with our custom
  `partitions.csv` — the partition table is at `0x8000` by default but
  we should sanity check against `idf.py partition-table`. If wrong, the
  merged bin will boot-loop on flash.
- **R3.** GitHub Pages serves over HTTPS; ESP Web Tools requires HTTPS
  (WebSerial only works on secure contexts). `*.github.io` gives us this
  for free — but if Petar later moves to a custom domain, the cert must
  be valid.
- **R4.** Browser caching on Pages — set short cache headers on
  `manifest-*.json` (or version-bust the URL) so users always pick up
  the latest firmware version after a release.
- **R5.** First-time flash UX: users hitting BOOT/RESET wrong is the
  #1 support burden on every web flasher project. Instructions panel
  must be excellent — don't skimp on F3.
- **R6.** Existing releases (v0.3.2, v0.3.3-beta.1) have no assets. F1
  only fires on **future** tags. Either Petar cuts a fresh `v0.3.4` to
  trigger the new pipeline, or we backfill assets manually for
  `v0.3.2`. Recommend cutting fresh — see Q5.

## Open questions for Petar (must be answered before implementation)

- **Q1.** Stable-only flasher, or also a dev/nightly channel? (Plan
  assumes stable-only — `v*` tags excluding `*-beta*`/`*-alpha*`/`*-rc*`.)
- **Q2.** UI shape: one Install button per sensor card (current plan), or
  a single dropdown sensor-selector with one global Install button?
  (Cards scale better as the catalogue grows; happy to flip to dropdown
  if Petar prefers.)
- **Q3.** Brand palette: any colours/gradients you want me to lift from
  the lesson PDFs? If you can drop one PDF or a hex code or two, F3 will
  match it. Default: amber/honey gradient (`#f59e0b → #d97706`) to
  riff on "HiveKit" name. Confirm or override.
- **Q4.** Fonts: Google Fonts (Inter + JetBrains Mono — easy, one
  external request) or self-hosted (zero external runtime requests, more
  work)? Default: Google Fonts.
- **Q5.** Bootstrap release: cut a fresh `v0.3.4` after merge to trigger
  the new pipeline (clean), or backfill assets onto existing `v0.3.2`?
  Default: fresh tag.
- **Q6.** Do you want a screenshot in the README (Task F5)? Default: yes.
- **Q7.** Custom domain (e.g. `flash.hivekit.dev`) now or later?
  Default: later — use `*.github.io` for now.

## Implementation order recommendation

1. Get answers to Q1–Q7 from Petar. (BLOCKER.)
2. F1 (release workflow) → F2 (manifest) → F3 (UI) → F4 (Pages deploy).
3. F6 (Petar enables Pages — can be done in parallel with F1–F3).
4. Cut a fresh `v0.3.4` tag → verify end-to-end flash from
   https://petarivanov-msft.github.io/hivekit/ on Petar's actual XIAO.
5. F5 (README + docs) — last, so the screenshot and live URL are real.

## Cross-references

- mem.py #8427 — prior build/flash workflow conventions (release on
  `v*` tag, merged-bin upload pattern). The new `release.yml` should
  match that mental model.
- mem.py #8439 — known ESP Web Tools failure mode (`Invalid head of
  packet (0x45)`) that bit the v0.3.0 flasher; flag in F1's caveats.
- mem.py #8362 — Petar's flash session standard commands (Win10
  PowerShell, ESP-IDF v5.5.4) — useful for the instructions panel in F3.
- `docs/links.md` → florianL21/zigbee-co2-sensor reference flasher.
  **Inspiration only — do not copy code.**
