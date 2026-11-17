#!/bin/bash
# Launches the installed Elisa flatpak on a virtual screen with test music and takes
# screenshots. All runs use DARK MODE (Breeze Dark) unless a step switches it.
set -x
OUT=$PWD/results; mkdir -p $OUT
CONF=$HOME/.var/app/org.kde.elisa/config
mkdir -p $CONF $HOME/Music
cp -r test-music/* $HOME/Music/
# let the sandboxed app read the test music folder (no XDG Music dir on this machine)
flatpak override --user --filesystem=$HOME/Music org.kde.elisa

write_config() {   # $1 = album artist option true/false, $2 = sidebar embed (NoView/AllArtists)
cat > $CONF/elisarc <<EOC
[ElisaFileIndexer]
RootPath=$HOME/Music

[PlayerSettings]
UseAlbumArtistInArtistsView=$1

[UiSettings]
ColorScheme=BreezeDark

[Views]
InitialView=AllArtists
EmbeddedView=${2:-NoView}
EOC
}

shot() {   # $1 = name
  sleep 1
  python3 -c "
from PIL import Image
import struct
d=open('/tmp/fb/Xvfb_screen0','rb').read()
hs=struct.unpack('>I',d[0:4])[0]; w=struct.unpack('>I',d[16:20])[0]; h=struct.unpack('>I',d[20:24])[0]
ncol=struct.unpack('>I',d[76:80])[0]; bpl=struct.unpack('>I',d[48:52])[0]
off=hs+ncol*12
Image.frombuffer('RGBX',(w,h),d[off:off+bpl*h],'raw','BGRX',bpl,1).convert('RGB').crop((0,0,900,648)).save('$OUT/$1.png')
"
}

start_elisa() {   # $1 = log name
  flatpak run org.kde.elisa > $OUT/$1.log 2>&1 &
  ELISA_PID=$!
  for i in $(seq 1 60); do xdotool search --name Elisa >/dev/null 2>&1 && break; sleep 1; done
  sleep 30   # give it time to scan the music
  WID=$(xdotool search --name Elisa | head -1)
}

stop_elisa() {
  flatpak kill org.kde.elisa; sleep 3; kill $ELISA_PID 2>/dev/null; wait $ELISA_PID 2>/dev/null
}

menu()      { xdotool mousemove 880 154 click 1; sleep 2; }
menu_item() { xdotool mousemove 646 $1 click 1; sleep 4; }   # y of the item
DARK_Y=301; CONFIGURE_Y=355

mkdir -p /tmp/fb
Xvfb :99 -screen 0 1280x800x24 -fbdir /tmp/fb &
export DISPLAY=:99
sleep 2

# 1) album artist option OFF
write_config false
start_elisa run1
shot 01-option-off-artists

# 2) switch it ON while Elisa is running
write_config true
sleep 8
shot 02-option-on-live

# 3) Daft Punk's page
xdotool mousemove 300 320 click 1; sleep 4
shot 03-daft-punk-page
xdotool mousemove 57 342 click 1; sleep 2

# 3b) Albums view (rounded covers) and playing a song (header cover)
xdotool mousemove 60 306 click 1; sleep 4
shot 03b-albums-view
xdotool mousemove 300 330; sleep 1
shot 03c-album-hover
xdotool mousemove 251 378 click 1; sleep 6   # hover "play" button on the first album
shot 03d-playing
xdotool mousemove 57 342 click 1; sleep 3

# 4) the Dark Mode toggle: menu, switch to light, switch back to dark
menu
shot 04a-menu-dark-mode-ticked
menu_item $DARK_Y
shot 04b-dark-mode-off
menu
shot 04c-menu-dark-mode-unticked
menu_item $DARK_Y
shot 04d-dark-mode-on-again

# 5) settings page
menu
menu_item $CONFIGURE_Y
shot 05-settings-page
xdotool key Escape
stop_elisa

# 6) start fresh with the option ON and artists embedded in the sidebar
write_config true AllArtists
start_elisa run2
shot 06-option-on-at-startup-with-sidebar

# 7) untick the checkbox in Settings and press OK
menu
menu_item $CONFIGURE_Y
shot 07a-settings-before-click
xdotool mousemove 230 221 click 1; sleep 1
shot 07b-checkbox-unticked
xdotool mousemove 423 555 click 1; sleep 6
shot 07c-after-ok
stop_elisa

cat $CONF/elisarc > $OUT/elisarc-after.txt
grep -iE "qml|error|warning|cannot|undefined|TypeError|ReferenceError|dark" $OUT/run1.log $OUT/run2.log > $OUT/problems.txt || echo "no QML errors or warnings found" > $OUT/problems.txt
ls -la $OUT
