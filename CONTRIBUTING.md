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

## Cutting a pre-release

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

