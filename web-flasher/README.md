# Browser flasher

Live site: https://john8675309.github.io/NWIA-MeshCore-MQTT/

This buildless site uses a local, pinned ESP Web Tools 10.5.2 distribution and
an exact copy of the public release's merged firmware. All runtime assets are
served on the same origin; no CDN, analytics or operator credentials are used.
ESP Web Tools' Apache-2.0 license and bundled JavaScript notices are retained.

The installer manifest accepts only ESP32-S3, at flash offset zero. Chip
detection cannot distinguish a Heltec V3 from every other ESP32-S3 board, so
the page identifies the supported hardware and requires board confirmation.
The page checks SHA-256 and file size before enabling Connect. The installer
also asks before a full-device erase; the merged image itself overwrites NVS.
Improv provisioning is disabled because this firmware uses USB CLI setup.

## Validate and preview

From the repository root:

```bash
python3 nwia/releases/check-web-flasher.py
python3 -m http.server 8765 --directory web-flasher
```

Open http://localhost:8765 in a supported browser. Localhost is a secure
context for Web Serial; a public deployment must use HTTPS.

Browser QA checked release verification, initial disabled button, confirmation
enabling, checksum failure disabling, and retry recovery. A real USB browser
flash was not performed during page deployment; existing gateway settings
were not altered. Firmware itself has separate hardware bench results.

## Update the selected firmware

For a new release, obtain its merged image and verify it against that release's
SHA256SUMS.txt and BUILD-MANIFEST.json. Copy that exact image into `firmware/`,
update `release.json` (version, path, SHA-256, byte length, source commit and
release URL), and update `manifest.json`. Also update the static version,
download and release links in `index.html` for the no-JavaScript fallback.
Never copy a flash dump or a locally configured device image into this folder.

Run the check script and preview, then commit. Firmware `.bin` files are
ignored by the wider source tree, so explicitly add only the audited public
release image with `git add -f web-flasher/firmware/EXACT_RELEASE_FILE.bin`.

## Publish through GitHub Pages

The repository's Pages source is the root of its `gh-pages` branch. This keeps
all upstream MeshCore docs out of the flasher deployment. Publish from a clean
`main` checkout:

```bash
git push origin main
git subtree push --prefix web-flasher origin gh-pages
```

For first deployment, enable GitHub Pages with source branch `gh-pages` and
path `/`. This repository does not require a deployment workflow or secret.
