#!/bin/bash
# Moonlight Embedded launcher for PortMaster.
# Settings live in ports/moonlightembedded/moonlight.conf (created on first launch).

XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}

if [ -d "/opt/system/Tools/PortMaster/" ]; then
  controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then
  controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then
  controlfolder="$XDG_DATA_HOME/PortMaster"
else
  controlfolder="/roms/ports/PortMaster"
fi

source $controlfolder/control.txt
[ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"
get_controls

GAMEDIR="/$directory/ports/moonlightembedded"
CONF="$GAMEDIR/moonlight.conf"
KEYDIR="$GAMEDIR/keys"
MOONLIGHT="$GAMEDIR/moonlight.${DEVICE_ARCH}"

cd "$GAMEDIR"
> "$GAMEDIR/log.txt" && exec > >(tee "$GAMEDIR/log.txt") 2>&1

# Keep user settings across port updates: the zip only ships the default file.
[ -f "$CONF" ] || cp "$GAMEDIR/moonlight.conf.default" "$CONF"
mkdir -p "$KEYDIR"
chmod +x "$MOONLIGHT"

export LD_LIBRARY_PATH="$GAMEDIR/libs.${DEVICE_ARCH}:$LD_LIBRARY_PATH"
export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"

conf_value() {
  sed -n "s/^[[:space:]]*$1[[:space:]]*=[[:space:]]*\(.*[^[:space:]]\)[[:space:]]*$/\1/p" "$CONF" | tail -n 1
}

# Options placed before -config are defaults; anything set in moonlight.conf wins.
moonlight() {
  local action="$1"
  shift
  "$MOONLIGHT" "$action" "$@" -keydir "$KEYDIR" -config "$CONF"
}

# A wrong address otherwise blocks for minutes (no HTTP timeout in libgamestream).
list_apps() {
  local out rc
  out="$(timeout 30 "$MOONLIGHT" list -keydir "$KEYDIR" -config "$CONF" 2>&1)"
  rc=$?
  if [ $rc -eq 124 ] || [ $rc -eq 143 ]; then
    out="$(printf '%s\nNo answer within 30 seconds.' "$out")"
  fi
  echo "$out"
}

ADDRESS="$(conf_value address)"
TARGET="${ADDRESS:-your PC (auto-discovery)}"
CONF_HINT="Set your PC's IP address in\nports/moonlightembedded/moonlight.conf:\naddress = 192.168.1.50"

pm_message "Connecting to $TARGET..." --title "Moonlight"
APPS="$(list_apps)"
echo "$APPS"

if echo "$APPS" | grep -q "You must pair with the PC first"; then
  PIN=$(( RANDOM % 9000 + 1000 ))
  pm_message "PIN: $PIN\n\nOn the PC open the Sunshine web UI (https://localhost:47990) > PIN, enter $PIN and a device name.\n\nWaiting for the PC..." --title "Pair with $TARGET"
  PAIR="$(moonlight pair -pin "$PIN" 2>&1)"
  echo "$PAIR"
  if ! echo "$PAIR" | grep -q "Succesfully paired"; then
    pm_show_error "Pairing failed:\n$(echo "$PAIR" | tail -n 1)" --title "Moonlight"
    pm_finish
    exit 1
  fi
  pm_message "Paired. Connecting..." --title "Moonlight"
  APPS="$(list_apps)"
  echo "$APPS"
fi

if ! echo "$APPS" | grep -q "^1\. "; then
  REASON="$(echo "$APPS" | grep -vE "^(Searching for server|Connecting to|Generating certificate)" | tail -n 1)"
  pm_show_error "Could not reach $TARGET:\n$REASON\n\n$CONF_HINT" --title "Moonlight"
  pm_finish
  exit 1
fi

APP="$(conf_value app)"
if ! echo "$APPS" | sed -n 's/^[0-9]*\. //p' | grep -qxF -- "$APP"; then
  pm_show_error "App \"$APP\" not found on the PC.\nSet app = <name> in moonlight.conf. Available:\n$(echo "$APPS" | sed -n 's/^[0-9]*\. //p' | head -n 8)" --title "Moonlight"
  pm_finish
  exit 1
fi

pm_platform_helper "$MOONLIGHT"

# Default stream size is the native panel resolution (960x720 on 4:3 panels, 720x720 on square ones).
# stdout and stderr both go to the log as they arrive and are kept for the error card,
# minus the "Stopping/Cleaning up ...done" teardown lines that follow the real error.
{ STREAM_OUT="$(set -o pipefail; moonlight stream -platform sdl -width "$DISPLAY_WIDTH" -height "$DISPLAY_HEIGHT" 2>&1 | tee /dev/fd/3)"; STATUS=$?; } 3>&1
if [ $STATUS -ne 0 ]; then
  pm_show_error "Stream ended with an error:\n$(echo "$STREAM_OUT" | grep -vE '\.\.\.(done)?$' | tail -n 2)" --title "Moonlight"
fi

pm_finish
