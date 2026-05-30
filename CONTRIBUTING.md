# Contributing / Branching Model

## Branches

- **`main`** — stable. Tagged releases (`v0.3.2`, `v0.3.3`, ...) cut from here are marked **Latest** on GitHub. Always safe to flash.
- **`dev`** — integration branch. Experimental commits land here first and are tagged as **Pre-release** (`v0.3.3-beta.1`, `v0.3.3-rc.1`, ...). Soak here for a few days / real-world use before promoting.
- **`experiment/*`** — throwaway feature branches. Merge into `dev` once proven; delete after merge.

## Release flow

```
experiment/foo  ── merge ──▶  dev  ── tag vX.Y.Z-beta.N (Pre-release)
                                │
                            soak / test
                                ▼
                              main  ── tag vX.Y.Z (Latest)
```

## Adding new flasher assets

> **WARNING: keep in sync with `.github/workflows/release.yml` deploy steps.**
>
> If you add a new HTML file, JS module, CSS file, or other static asset to
> `flasher/`, you MUST also add it to the explicit file-copy lists in the
> "Substitute version placeholders and stage flasher (stable)" and
> "Substitute version placeholders and stage flasher (dev)" steps of
> `release.yml`. Otherwise the new asset will not be deployed to GitHub Pages.

## Firmware binary serving (same-origin CORS fix)

Firmware `.bin` files are served directly from GitHub Pages so the ESP Web
Tools flasher can fetch them **same-origin**, avoiding CORS errors that occur
when fetching from `github.com/…/releases/download/…`.

The release workflow copies each merged binary under `firmware/` in the Pages
artifact before upload:

| Channel | URL pattern | Example |
|---------|-------------|---------|
| Stable  | `https://petarivanov-msft.github.io/hivekit/firmware/hivekit-<SENSOR>-<VERSION>.bin` | `…/firmware/hivekit-scd40-c6-v0.3.5.bin` |
| Dev     | `https://petarivanov-msft.github.io/hivekit/dev/firmware/hivekit-<SENSOR>-dev-latest.bin` | `…/dev/firmware/hivekit-scd40-c6-dev-latest.bin` |

Manifest `parts[0].path` values use relative paths (`firmware/<name>.bin`)
so ESP Web Tools resolves them same-origin.

**GitHub Releases** (`releases/download/…`) still host the same binaries as a
downloadable archive; they are NOT used by the web flasher.

Cross-channel Pages coherence (dev deploy preserving stable content and vice
versa) is handled automatically by the workflow's fetch-then-merge steps. If
you add a new sensor, add its binary to the `SENSORS` env var in `release.yml`
— the loop-driven firmware staging and cross-channel preservation steps will
pick it up automatically.


```bash
# after merging experiment/foo into dev
git checkout dev && git pull
git tag -a vX.Y.Z-beta.1 -m "Pre-release: <summary>"
git push origin vX.Y.Z-beta.1
gh release create vX.Y.Z-beta.1 --prerelease --title "vX.Y.Z-beta.1" --notes "..."
```

## Promoting to stable

```bash
git checkout main && git pull
git merge --ff-only dev    # fast-forward only — no merge commit clutter
git push origin main
git tag -a vX.Y.Z -m "Release: <summary>"
git push origin vX.Y.Z
gh release create vX.Y.Z --latest --title "vX.Y.Z" --notes "..."
```

