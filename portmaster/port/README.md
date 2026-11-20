## Notes

Thanks to the [Moonlight Embedded](https://github.com/moonlight-stream/moonlight-embedded)
developers and the [Sunshine](https://github.com/LizardByte/Sunshine) project.

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

### 5. Point it at your PC

In `ports/moonlightembedded/` copy `moonlight.conf.default` to `moonlight.conf`
and edit it:

```
address = 192.168.1.50     # your PC's IP (remove the leading #)
app = Desktop              # exact app name in Sunshine
```

Default Sunshine apps: `Desktop`, `Low Res Desktop`, `Steam Big Picture`.
If you skip this step, the first launch creates `moonlight.conf` and tries mDNS
auto-discovery; if that fails it tells you to set `address`.

### 6. Pair (first launch only)

1. Launch **Ports → Moonlight Embedded**.
2. The handheld shows **PIN: 1234** (random 4 digits).
3. On the PC open `https://localhost:47990` → **PIN**, select the pending
   request, enter the PIN and a device name (e.g. `Pocket1`), submit.
4. The handheld reports "Paired" and the stream starts.

Later launches go straight to the stream.

## Controls

| Button | Action |
|--|--|
| All buttons and sticks | Sent to the PC as an Xbox controller |
| Start + Select + L1 + R1 | Quit the stream |

## Settings (`moonlightembedded/moonlight.conf`)

| Setting | Meaning |
|--|--|
| `address = …` | PC IP or hostname; unset = mDNS auto-discovery |
| `app = Desktop` | App to launch, exactly as named in Sunshine |
| `fps = 60` | Frame rate |
| `bitrate = 8000` | kbps; lower if Wi-Fi stutters (default ≈10000 at 960x720/60) |
| `codec = h264` | Keep: cheapest to decode in software on RK3566 |
| `960x720 = true` or `width`/`height` | Override the native resolution (on the 720x720 RGB20SX, 960x720 gets stretched) |
| `quitappafter = true` | Close the app on the PC when the stream ends |

`moonlight.conf` and the pairing keys survive port updates; only
`moonlight.conf.default` is overwritten.

## Troubleshooting

| Problem | Fix |
|--|--|
| "Could not reach …" | Check `address`, that Sunshine is running, and the PC firewall |
| "App … not found" | The message lists the PC's apps; copy one into `app =` exactly |
| Pairing failed | Re-launch and enter the newly shown PIN; select the right pending request |
| Re-pair from scratch | Delete `ports/moonlightembedded/keys/` and unpair the device in Sunshine |
| Anything else | See `ports/moonlightembedded/log.txt` |

## Compile

```shell
./portmaster/build.sh   # Docker cross-build, outputs portmaster/dist/moonlightembedded.zip
```
