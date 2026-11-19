#!/bin/bash
# Extensive test of the built Elisa flatpak, in DARK MODE, on a virtual screen with a window manager.
set -x
OUT=$PWD/results; mkdir -p $OUT
mkdir -p $HOME/Music
cp -r test-music/* $HOME/Music/
if [ -n "$APPIMAGE_PATH" ]; then
  CONF=$HOME/.config
else
  CONF=$HOME/.var/app/org.kde.elisa/config
  flatpak override --user --filesystem=$HOME/Music org.kde.elisa
  flatpak override --user --env=QSG_INFO=1 org.kde.elisa   # log which renderer is used
  flatpak override --user --env=QT_LOGGING_RULES=org.kde.elisa.views.debug=true org.kde.elisa
fi
mkdir -p $CONF

write_config() {   # $1 = album artist option, $2 = sidebar embed
cat > $CONF/elisarc <<EOC
[ElisaFileIndexer]
RootPath=$HOME/Music

[PlayerSettings]
UseAlbumArtistInArtistsView=$1

[UiSettings]
ColorScheme=BreezeDark

[Views]
InitialView=${3:-AllArtists}
EmbeddedView=${2:-NoView}
EOC
}

N=0
shot() {   # $1 = name (auto-numbered)
  sleep 1; N=$((N+1)); local name=$(printf "%02d-%s" $N "$1")
  python3 -c "
from PIL import Image
import struct
d=open('/tmp/fb/Xvfb_screen0','rb').read()
hs=struct.unpack('>I',d[0:4])[0]; w=struct.unpack('>I',d[16:20])[0]; h=struct.unpack('>I',d[20:24])[0]
ncol=struct.unpack('>I',d[76:80])[0]; bpl=struct.unpack('>I',d[48:52])[0]
off=hs+ncol*12
Image.frombuffer('RGBX',(w,h),d[off:off+bpl*h],'raw','BGRX',bpl,1).convert('RGB').save('$OUT/$name.png')
"
}
click() { xdotool mousemove $1 $2 click 1; sleep ${3:-3}; }
rclick() { xdotool mousemove $1 $2 click 3; sleep ${3:-2}; }
drag() { xdotool mousemove $1 $2 mousedown 1; sleep 0.3; xdotool mousemove $(( ($1+$3)/2 )) $2; sleep 0.2; xdotool mousemove $3 $2; sleep 0.3; xdotool mouseup 1; sleep 0.5; }
check() { if eval "$2"; then echo "PASS $1" >> $OUT/checks.txt; else echo "${3:-FAIL} $1" >> $OUT/checks.txt; fi; }
: > $OUT/checks.txt
hover() { xdotool mousemove $1 $2; sleep ${3:-1}; }
key()   { xdotool key --clearmodifiers "$@"; sleep 3; }

start_elisa() {
  if [ -n "$APPIMAGE_PATH" ]; then
    env $2 QSG_INFO=1 QT_LOGGING_RULES=org.kde.elisa.views.debug=true "$APPIMAGE_PATH" > $OUT/$1.log 2>&1 &
  else
    flatpak run ${2:+--env=$2} org.kde.elisa > $OUT/$1.log 2>&1 &
  fi
  ELISA_PID=$!
  for i in $(seq 1 60); do xdotool search --name Elisa >/dev/null 2>&1 && break; sleep 1; done
  sleep 30
  # the biggest visible Elisa window is the main window
  WID=$(for w in $(xdotool search --onlyvisible --name Elisa); do echo "$(xdotool getwindowgeometry $w | awk '/Geometry/{split($2,a,"x"); print a[1]*a[2]}') $w"; done | sort -n | tail -1 | cut -d' ' -f2)
  echo "main window: $WID"
  xdotool windowmove $WID 0 0 windowsize $WID 900 648 windowactivate $WID
  sleep 3
}
stop_elisa() {
  if [ -n "$APPIMAGE_PATH" ]; then pkill -x elisa; pkill -f Elisa-album-artist; else flatpak kill org.kde.elisa; fi
  sleep 3; kill $ELISA_PID 2>/dev/null; wait $ELISA_PID 2>/dev/null
}

# sidebar rows (y) and grid tiles (x)
NOWPLAYING=198; RECENT=234; FREQUENT=270; ALBUMS=306; ARTISTS=342; TRACKS=378; GENRES=414; FILES=450; RADIOS=486; ADDED=522
TILE1=300; TILE2=466; TILE3=631

mkdir -p /tmp/fb
Xvfb :99 -screen 0 1280x800x24 -fbdir /tmp/fb +extension GLX &
export DISPLAY=:99
sleep 2
# window manager without title bars, so coordinates match the window
mkdir -p ~/.config/openbox
cat > ~/.config/openbox/rc.xml <<'EOX'
<?xml version="1.0"?>
<openbox_config xmlns="http://openbox.org/3.4/rc">
  <applications><application class="*"><decor>no</decor><position force="yes"><x>0</x><y>0</y></position></application></applications>
</openbox_config>
EOX
openbox &
sleep 2

########## RUN 1: album artist option ON, dark mode ##########
write_config true
start_elisa run1
shot first-start-default-playlist-width
# the rest of the test uses the narrowest playlist: drag the divider to the right
for x in $(seq 588 2 618); do drag $x 610 780; done
shot artists-album-artists-only

# every view in the sidebar
click 60 $NOWPLAYING;  shot view-now-playing
click 60 $RECENT;      shot view-recently-played
click 60 $FREQUENT;    shot view-frequently-played
click 60 $ALBUMS;      shot view-albums
click 60 $TRACKS;      shot view-tracks
click 60 $GENRES;      shot view-genres
click 60 $FILES;       shot view-files
click 60 $RADIOS;      shot view-radio-stations
click 60 $ADDED;       shot view-recently-added

# Albums: open "Random Access Memories" (3rd tile), then play it
click 60 $ALBUMS
click $TILE3 320 4;    shot album-page-random-access-memories
key alt+Left;          shot back-to-albums
hover $TILE3 320;      shot albums-hover-card
click $((TILE3-49)) 378 8; shot playing-random-access-memories
hover 450 60 2;        shot header-cover-closeup
click 111 153 5;       shot next-track
click 80 153 3;        shot play-pause-pressed
click 60 $NOWPLAYING 4; shot now-playing-with-song

# Genres -> Disco -> artists (album artist mode) -> Daft Punk
click 60 $GENRES
click $TILE1 320 4;    shot genre-first-tile
click $TILE1 320 4;    shot genre-first-artist

# Artists: search box
click 60 $ARTISTS
click 695 197 2
xdotool type --delay 80 "daft"; sleep 3
shot artists-search-daft
key Escape

# Artists: Solo Guy (no album artist tag) and Various Artists pages
click 60 $ARTISTS
click $TILE2 320 4;    shot artist-solo-guy-page
click 60 $ARTISTS
click $TILE3 320 4;    shot artist-various-artists-page

# keyboard shortcuts work with a window manager
click 60 $ARTISTS
key F9;                shot shortcut-F9-hide-playlist
key F9;                shot shortcut-F9-show-playlist
key ctrl+shift+comma 4; shot shortcut-open-settings
key Escape

# Color Scheme submenu and the Dark Mode toggle
click 880 154 2;       shot menu
hover 646 328 2;       shot menu-sleep-timer-submenu
hover 646 355 2;       shot menu-color-scheme-submenu
key Escape; key Escape

# sleep timer: 15 minutes, check the menu shows the countdown, then switch it off
click 880 154 2; hover 646 328 2; click 520 355 2
click 880 154 2;       shot sleep-timer-running-menu
hover 646 328 2;       shot sleep-timer-running-submenu
click 520 328 2
click 880 154 2;       shot sleep-timer-off-menu
key Escape

# sleep timer: stop at the end of the current track (songs of this album are 12 s long)
click 60 $ALBUMS
click $((TILE3-49)) 378 2
click 880 154 2; hover 646 328 2; shot sleep-timer-end-of-track-choice
click 520 490 2
sleep 16
shot sleep-timer-paused-at-end-of-track
click 60 $NOWPLAYING 3; shot sleep-timer-paused-now-playing

# volume keyboard shortcuts
click 60 $ARTISTS
key ctrl+Down; key ctrl+Down; key ctrl+Down; key ctrl+Down
hover 780 154 2;       shot volume-after-4x-ctrl-down
key ctrl+Up; key ctrl+Up; key ctrl+Up; key ctrl+Up
hover 780 154 2;       shot volume-after-4x-ctrl-up
hover 450 400
click 880 154 2; click 646 301 4; shot dark-mode-off-light-look
click 60 $ALBUMS;      shot albums-in-light-mode
click 880 154 2; click 646 301 4; shot dark-mode-back-on

# switch the album artist option off in Settings, press OK
click 60 $ARTISTS
key ctrl+shift+comma 4
hover 230 221;         shot settings-before-untick
click 230 221 1;       shot settings-unticked
click 423 555 6;       shot option-off-all-artists

# Cancel should not change anything
key ctrl+shift+comma 4
click 230 221 1
click 511 555 4;       shot settings-cancel-keeps-option-off

# narrow and wide windows
xdotool windowsize --sync $WID 560 648; sleep 4; xdotool getwindowgeometry $WID; shot narrow-window
xdotool windowsize --sync $WID 1280 800; sleep 4; xdotool getwindowgeometry $WID; shot wide-window
xdotool windowsize $WID 900 648; sleep 3
stop_elisa

########## RUN 2: restart, option ON + artists in the sidebar ##########
write_config true AllArtists
start_elisa run2
shot restart-option-on-sidebar-artists
click 40 540 4;        shot sidebar-artist-click
stop_elisa

########## RUN 3: names, track order, right-click menus, hidden music ##########
# an album like the reported one: odd track numbers, disc number missing on some songs, messy titles
H="$HOME/Music/Test Band/Human"; mkdir -p "$H"
mk() {  # file, title, track, disc (may be empty)
  ffmpeg -loglevel error -y -f lavfi -i "sine=frequency=$((300+$3*20)):duration=6" -metadata title="$2" -metadata artist="Test Band" \
    -metadata album_artist="Test Band" -metadata album="Human" -metadata track="$3" ${4:+-metadata disc=$4} "$H/$1"
}
mk "02 - Painkiller (Official Video).mp3" "02 - Painkiller (Official Video)" 2 ""
mk "12 The Real You.mp3" "12 The Real You" 12 1
mk "16. Let You Down (Live).mp3" "16. Let You Down (Live)" 16 1
mk "01 Human Race.mp3" "01 Human Race" 1 ""
ls -la "$H"

RC_TRACK_Y=${RC_TRACK_Y:-330}
write_config false NoView AllAlbums
start_elisa run3
shot albums-with-human
click $TILE1 320 4;    shot album-human-adjusted-names-and-order
rclick 450 $RC_TRACK_Y; shot track-right-click-menu-a
key Escape
rclick 450 400;        shot track-right-click-menu-b
key Escape
key alt+Left
rclick $TILE1 320;     shot album-right-click-menu
key Escape
key ctrl+shift+comma 4; shot settings-top
xdotool mousemove 450 400; for i in 1 2 3 4 5 6 7 8 9 10; do xdotool click 5; done; sleep 2
shot settings-scrolled-hidden-music
key Escape
stop_elisa

########## RUN 4: update check at startup (pretends to be build 1) ##########
start_elisa run4 ELISA_FORK_BUILD_NUMBER_OVERRIDE=1
shot update-banner-at-startup
click 880 154 2;       shot menu-with-check-for-updates
key Escape
stop_elisa
find $HOME -name hidden-music.json -exec cp {} $OUT/hidden-music.json \; 2>/dev/null

cat $CONF/elisarc > $OUT/elisarc-after.txt
grep -iE "renderer|GL_RENDERER|software|rhi" $OUT/run1.log | head -20 > $OUT/renderer.txt
echo "embedded cover cache: $(grep -c 'embedded cover cache hit' $OUT/run1.log) hits, $(grep -c 'embedded cover cache miss' $OUT/run1.log) misses (first run)" > $OUT/cover-cache.txt
cat $OUT/cover-cache.txt
grep -iE "qml|failed|error|warning|cannot|undefined|TypeError|ReferenceError|crash|segmentation" $OUT/run*.log | grep -v "org.kde.elisa.views:" > $OUT/problems.txt || echo "none" > $OUT/problems.txt
ls -la $OUT
