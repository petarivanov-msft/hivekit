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

