# When you get home: enable cloud firmware builds

One-time setup so that every commit to `tim` automatically builds the signed
Sensor Skins DFU zips in GitHub Actions, ready to download and flash from your
phone in the field.

Do these on your laptop, in order. Should take ~5 minutes.

---

## 1. Find the signing key and copy its contents

The cloud build needs the same signing key your laptop already uses. It lives
at this path in your local checkout (it is intentionally NOT in git):

```bash
cd ~/path/to/sensor-skins-firmware
cat "firmware/ble_app_firmware_v2_R6/reid_ble_aginic_v2.06_source/calceus_private.key"
```

- If it prints a key block (e.g. `-----BEGIN EC PRIVATE KEY-----` ... `-----END EC PRIVATE KEY-----`),
  select and copy the **entire** output, including the BEGIN/END lines.
- If you get "No such file or directory", the key is not on this machine. Find
  your backup of `calceus_private.key`, copy it to that exact path, then re-run
  the `cat` command. (Without this key, builds still produce `.hex` files but
  cannot sign the DFU zips, and the devices' bootloader will reject unsigned zips.)

> Keep this key private. Do not paste it into chats, commits, or anywhere public.
> The only place it goes is the GitHub secret in step 2.

---

## 2. Add the key as a GitHub Actions secret

In a browser:

1. Go to the repo on GitHub:
   `https://github.com/CalceusHealth/sensor-skins-firmware`
2. **Settings** -> **Secrets and variables** -> **Actions**.
3. Click **New repository secret**.
4. Name (exactly): `CALCEUS_DFU_PRIVATE_KEY`
5. Secret: paste the full key contents you copied in step 1.
6. Click **Add secret**.

---

## 3. Get the build workflow onto the `tim` branch

The workflow currently lives on the branch `firmware-build-github-action`. It
only starts running on `tim` commits once it is merged into `tim`.

Easiest (web): open a Pull Request from `firmware-build-github-action` into
`tim` and merge it:

- `https://github.com/CalceusHealth/sensor-skins-firmware/compare/tim...firmware-build-github-action`
- Review (it adds one file, `.github/workflows/firmware-build.yml`, plus a
  small `BUILDING.md` doc fix), then **Merge**.

Or from the command line:

```bash
cd ~/path/to/sensor-skins-firmware
git fetch origin
git checkout tim
git merge --no-ff origin/firmware-build-github-action
git push origin tim
```

---

## 4. Confirm it works

1. On GitHub, open the **Actions** tab. After the merge (or your next push to
   `tim`), you should see a **"Firmware Build (Sensor Skins DFU)"** run start.
2. Wait for it to finish (first run is slower; later runs are cached).
3. Open the **Releases** page:
   `https://github.com/CalceusHealth/sensor-skins-firmware/releases`
   The newest release will have the signed DFU zips attached, e.g.
   `stream_sleep_lhs_v00020026_sensorskins.zip`. These are byte-for-byte the
   same kind of package your laptop produces.

You can also trigger a build by hand anytime: **Actions** tab ->
**Firmware Build (Sensor Skins DFU)** -> **Run workflow**.

---

## In the field, from your phone

1. Describe the firmware issue to Claude Code; it edits the code and commits to
   `tim`.
2. The Action builds and publishes a new release automatically.
3. Open the repo's **Releases** page, download the right `*_sensorskins.zip`
   (`lhs`/`rhs` for the side, `stream` is the currently-deployed line).
4. Flash it over Nordic DFU.

---

## If the first build fails

The most likely cause is the SEGGER Embedded Studio version. In
`.github/workflows/firmware-build.yml`, the `SES_URL` env var uses SEGGER's
"latest" download. If `emBuild` complains the project format is wrong, pin a
specific version by editing `SES_URL` to a versioned tarball and bumping
`SES_CACHE_KEY` to force a fresh download. Then push again. Everything else
(SDK, nrfutil, the build scripts) mirrors your laptop steps exactly.
