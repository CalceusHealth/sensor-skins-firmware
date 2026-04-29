# Sensor Skins Firmware Build Notes

This repository stores the orthotic firmware source, but the current SES
projects are not standalone. They expect to live inside the Nordic nRF5 SDK
15.3.0 tree.

The latest orthotic application source in this repository is:

- `firmware/ble_app_firmware_v2_R7`

The R6 tree also contains:

- Secure bootloader source
- Existing DFU artefacts

## Linux Mint host setup

Install general packages:

```bash
sudo apt update
sudo apt install -y wget curl tar xz-utils unzip rsync libusb-1.0-0 udev
```

Install SEGGER Embedded Studio:

```bash
cd ~/Downloads
wget https://www.segger.com/downloads/embedded-studio/Setup_EmbeddedStudio_linux_x64.tar.gz
tar -xzf Setup_EmbeddedStudio_linux_x64.tar.gz
cd setup_embeddedstudio_linux_x64
./install_segger_embedded_studio_linux
```

Install SEGGER J-Link tools:

```bash
cd ~/Downloads
wget -O JLink_Linux_x86_64.deb https://www.segger.com/downloads/jlink/JLink_Linux_V810_x86_64.deb
sudo dpkg -i JLink_Linux_x86_64.deb || sudo apt -f install -y
```

Install Nordic nRF Command Line Tools if you want `nrfjprog` and `mergehex`:

```bash
cd ~/Downloads
wget -O nrf-command-line-tools.tar.gz https://nsscprodmedia.blob.core.windows.net/prod/software-and-other-downloads/desktop-software/nrf-command-line-tools/sw/versions-10-x-x/10-11-1/nrfcommandlinetools10111linuxamd64.tar.gz
tar -xzf nrf-command-line-tools.tar.gz
```

Install Nordic `nrfutil` and the nRF5 SDK plugin:

```bash
cd ~/Downloads
wget -O nrfutil https://files.nordicsemi.com/ui/api/v1/download?repoKey=swtools&path=nrfutil/x86_64-unknown-linux-gnu/nrfutil
chmod +x nrfutil
sudo mv nrfutil /usr/local/bin/
nrfutil install nrf5sdk-tools
```

Download the Nordic SDK used by this project:

```bash
mkdir -p ~/sdk
cd ~/sdk
wget https://developer.nordicsemi.com/nRF5_SDK/nRF5_SDK_v15.x.x/nRF5_SDK_15.3.0_59ac345.zip
unzip nRF5_SDK_15.3.0_59ac345.zip
```

## Command-line workflow

The easiest CLI build path is to keep using the SES project, but build it with
`emBuild` instead of the GUI. The scripts in `tools/` stage the repo files into
the SDK tree, select LHS or RHS in the staged copy, then invoke `emBuild`.

Stage sources into the SDK tree:

```bash
./tools/stage_sdk_sources.sh ~/sdk/nRF5_SDK_15.3.0_59ac345 lhs
```

Build the application:

```bash
./tools/build_app.sh ~/sdk/nRF5_SDK_15.3.0_59ac345 lhs
```

Build the bootloader:

```bash
./tools/build_bootloader.sh ~/sdk/nRF5_SDK_15.3.0_59ac345
```

Build both:

```bash
./tools/build_all.sh ~/sdk/nRF5_SDK_15.3.0_59ac345 lhs
```

Expected application output:

```text
$SDK_ROOT/examples/ble_peripheral/reid_ble_aginic_v2/pca10040/s112/ses/Output/Release/Exe/ble_app_aginic_v2.hex
```

Expected bootloader output:

```text
$SDK_ROOT/examples/dfu/secure_bootloader_calceus/pca10040_ble/ses/Output/Release/Exe/secure_bootloader_ble_s112_calceus.hex
```

## GUI fallback

If you want to inspect or debug in SES, stage first and then open:

```text
$SDK_ROOT/examples/ble_peripheral/reid_ble_aginic_v2/pca10040/s112/ses/ble_app_aginic_v2.emProject
```

Build configuration:

- Project: `ble_app_aginic_v2`
- Configuration: `Release`

## Package a BLE DFU zip

The repo contains the private key used in the R6 source tree:

- `firmware/ble_app_firmware_v2_R6/reid_ble_aginic_v2.06_source/calceus_private.key`

After building the application, generate a DFU package:

```bash
./tools/package_dfu.sh ~/sdk/nRF5_SDK_15.3.0_59ac345 lhs
```

Or:

```bash
./tools/package_dfu.sh ~/sdk/nRF5_SDK_15.3.0_59ac345 rhs
```

The script writes a zip under:

```text
artefacts/out/
```

Filename format:

```text
stream_lhs_v####_sensorskins.zip
stream_rhs_v####_sensorskins.zip
nostream_lhs_v####_sensorskins.zip
nostream_rhs_v####_sensorskins.zip
```

The `stream` / `nostream` prefix is derived from the staged
`configure_firmware.h` build flags, and `v####` comes from `DEVICE_FW_VERSION`.

## Flash directly over SWD

If the device already has the expected bootloader and SoftDevice, flash the app:

```bash
./tools/flash_app.sh ~/sdk/nRF5_SDK_15.3.0_59ac345
```

If you want to flash the full OTA-capable stack after building both app and
bootloader:

```bash
./tools/flash_full_stack.sh ~/sdk/nRF5_SDK_15.3.0_59ac345
```

If the device is blank or in a bad state, you will usually need to flash the
SoftDevice first:

```bash
nrfjprog --family NRF52 --program ~/sdk/nRF5_SDK_15.3.0_59ac345/components/softdevice/s112/hex/s112_nrf52_6.1.1_softdevice.hex --sectorerase --verify
nrfjprog --family NRF52 --program ~/sdk/nRF5_SDK_15.3.0_59ac345/examples/ble_peripheral/reid_ble_aginic_v2/pca10040/s112/ses/Output/Release/Exe/ble_app_aginic_v2.hex --sectorerase --verify --reset
```

If readback protection is enabled:

```bash
nrfjprog --family NRF52 --recover
```

Then reflash SoftDevice and application.

## Left vs right builds

The build scripts accept `lhs` or `rhs` and switch the staged copy of
`configure_firmware.h` automatically. You do not need to edit the repo just to
change sides.

Examples:

```bash
./tools/build_app.sh ~/sdk/nRF5_SDK_15.3.0_59ac345 lhs
./tools/build_app.sh ~/sdk/nRF5_SDK_15.3.0_59ac345 rhs
```

## Notes

- This repository is not yet converted to a standalone GCC or CMake build.
- The command-line scripts still rely on SES `emBuild`, which is the shortest
  path to a reliable build on Linux Mint for this codebase.
- OTA DFU requires the secure bootloader already present on the device.
