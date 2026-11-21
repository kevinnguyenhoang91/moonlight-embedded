## Notes

Thanks to the [Moonlight Embedded](https://github.com/moonlight-stream/moonlight-embedded)
developers and the [Sunshine](https://github.com/LizardByte/Sunshine) project.

- Opens a launcher modelled on the Moonlight New port: **Settings / Play /
  Connect** menu, on-screen PIN pairing, number pad for the PC's IP address,
  app carousel, four animated themes and UI sounds. It comes back after every
  stream.
- Streams at the device's native resolution by default: 960x720 (4:3) on the
  MiniLoong Pocket 1, 720x720 on the Powkiddy RGB20SX.
- Video is decoded in software (FFmpeg H.264) and drawn through the system SDL2;
  the RK3566 hardware decoder (MPP) is not used.
- Pairing, app listing and stream start were verified against Sunshine
  2026.1120 inside the ROCKNIX 20261101 RK3566 rootfs (qemu). On-device video,
  audio and input have not been verified yet.

## Install guide (RGB20SX / MiniLoong Pocket 1)

### 1. PC

1. Install [Sunshine](https://github.com/LizardByte/Sunshine/releases).
2. Open `https://localhost:47990` on the PC, accept the certificate warning and
   create a username/password.
3. Put the PC and the handheld on the same network and note the PC's IP address
   (Windows: `ipconfig`, Linux: `ip a`), e.g. `192.168.1.50`.

### 2. Firmware with PortMaster

**Powkiddy RGB20SX:** [ROCKNIX](https://rocknix.org/devices/powkiddy/rgb20sx) is
officially supported; ArkOS, dArkOS or Knulli also ship PortMaster. If you
already run one of these, skip ahead.

**MiniLoong Pocket 1:** stock LoongOS has no PortMaster. Use AURKNIX (a ROCKNIX
fork that boots from the SD card; the stock OS stays untouched):

1. Download the latest `…specific.img.gz` from
   [AveyondFly/distribution-nightly](https://github.com/AveyondFly/distribution-nightly/releases).
2. Flash it to the SD card (e.g. Balena Etcher).
3. On the card's **ROCKNIX** partition open `extlinux/*.conf` and change
   `Powkiddy-55.dtb` to `miniloong.dtb`. Save.
4. Boot the Pocket 1 from the card. Constant vibration means the `.dtb` name is
   wrong; redo step 3.

Source: [Retro Handhelds guide](https://retrohandhelds.gg/rocknix-on-the-miniloong-pocket-1-transformative/).
dArkOS is an alternative.

### 3. Copy the port to the SD card

Copy `moonlightembedded.zip` **without extracting it** into the autoinstall
folder (create `autoinstall` if missing):

| Firmware | Folder |
|--|--|
| ROCKNIX / AURKNIX | `ports/autoinstall/` on the **GAMES** partition (`/roms/ports/autoinstall/` on device) |
| ArkOS / dArkOS | `ports/autoinstall/` on the roms partition |
| Knulli | `/userdata/roms/ports/autoinstall/` |

### 4. Install on the device

1. Insert the card and boot.
2. Open PortMaster (ROCKNIX/AURKNIX: **Tools → PortMaster**; ArkOS:
   **Options → Tools → PortMaster**; Knulli: **Ports → PortMaster**). It installs
   the zip automatically.
3. Exit PortMaster. **Moonlight Embedded** appears under **Ports** (restart or
   refresh the game list if not).

### 5. Connect and pair (first launch only)

1. Launch **Ports → Moonlight Embedded**. The launcher first looks for your PC
   on the network (mDNS).
2. If it can't find it: **Connect → IP Address**, enter the PC's IP with the
   number pad (e.g. `192.168.1.50`) and pick **Done**.
3. **Connect → Pair**. The handheld shows **PIN 1234** (random 4 digits).
4. On the PC open `https://localhost:47990` → **PIN**, select the pending
   request, enter the PIN and a device name (e.g. `Pocket1`), submit. The
   handheld reports "Paired" and lists the PC's apps.

### 6. Play

**Play**, pick an app with left/right (default Sunshine apps: `Desktop`,
`Low Res Desktop`, `Steam Big Picture`) and press A. When the stream ends you
are back in the launcher.

## Controls

Launcher:

| Button | Action |
|--|--|
| D-pad / left stick | Navigate; up/down changes a setting |
| A | Select |
| B | Back / skip the splash screen |
| Y | Reload apps (Play) / delete (number pad) |
| Start | Done (number pad) |
| L1 + R1 | Quit the launcher |

Stream:

| Button | Action |
|--|--|
| All buttons and sticks | Sent to the PC as an Xbox controller |
| Start + Select + L1 + R1 | Quit the stream (back to the launcher) |

## Settings

Launcher **Settings** (saved in `moonlightembedded/keys/launcher.conf`):

| Setting | Meaning |
|--|--|
| Resolution | Native = the panel's resolution; on the 720x720 RGB20SX, 4:3 sizes get stretched |
| Bitrate | Auto = `bitrate` from `moonlight.conf`, else ≈10000 kbps at 960x720/60; lower if Wi-Fi stutters |
| Framerate | Frames per second |
| Codec | Keep H.264: cheapest to decode in software on RK3566 |
| Remote | Optimizations for streaming over the internet |
| Sound FX / Theme | Launcher sounds and background |

`moonlightembedded/moonlight.conf` holds the defaults and options the launcher
doesn't show:

| Setting | Meaning |
|--|--|
| `quitappafter = true` | Close the app on the PC when the stream ends |
| `localaudio = true` | Play audio on the PC instead of the handheld |
| `address`, `width`/`height`, `fps`, `bitrate`, `codec` | Initial launcher values until changed in the launcher |

`moonlight.conf`, `launcher.conf` and the pairing keys survive port updates; only
`moonlight.conf.default` is overwritten.

## Troubleshooting

| Problem | Fix |
|--|--|
| "Can't connect to …" | Check the IP address, that Sunshine is running, and the PC firewall |
| "No PC found on the network" | Set the IP in **Connect → IP Address** |
| Pairing failed | **Connect → Pair** again and enter the newly shown PIN |
| "The stream ended with an error" | The message shows the reason; try a lower bitrate or resolution |
| Re-pair from scratch | Delete `ports/moonlightembedded/keys/` and unpair the device in Sunshine |
| Anything else | See `ports/moonlightembedded/log.txt` |

## Compile

```shell
./portmaster/build.sh   # Docker cross-build, outputs portmaster/dist/moonlightembedded.zip
```
