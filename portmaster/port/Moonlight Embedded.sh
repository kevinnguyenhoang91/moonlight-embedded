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

pm_platform_helper "$MOONLIGHT"

# The launcher (Settings / Play / Connect) sets the PC address, pairs, lists the apps
# and comes back after every stream; stream errors are shown in it.
# Options before -config are defaults, moonlight.conf overrides them and choices made
# in the launcher (keys/launcher.conf) override both. The default stream size is the
# native panel resolution (960x720 on 4:3 panels, 720x720 on square ones).
if ! "$MOONLIGHT" ui -platform sdl -width "$DISPLAY_WIDTH" -height "$DISPLAY_HEIGHT" -keydir "$KEYDIR" -config "$CONF"; then
  pm_show_error "Moonlight could not start, see ports/moonlightembedded/log.txt" --title "Moonlight"
fi

pm_finish
