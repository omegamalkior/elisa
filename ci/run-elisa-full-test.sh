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

HIDDEN() { find $HOME -name hidden-music.json 2>/dev/null | head -n 1; }
MENUDIR=$HOME/.local/share/kio/servicemenus
check "file manager action was installed at startup" '[ -x "$MENUDIR/elisa-import-to-music.desktop" ] && [ -x "$MENUDIR/elisa-import-to-music.sh" ]'
cp $MENUDIR/elisa-import-to-music.desktop $OUT/ 2>/dev/null

# album page rows (playlist at its default width): 1st song y=252, then every 46
ROW1=252
# the right-click menu opens at the pointer: entries below it
M_NAMES=180; M_HIDE=212; M_DELETE=239
write_config false NoView AllAlbums
start_elisa run3
shot albums-with-human
click $TILE1 320 4;    shot album-human-adjusted-names-and-order

# names: file names, then back to adjusted names
rclick 450 $ROW1;      shot song-right-click-menu
click 530 $((ROW1+M_NAMES)) 3; shot song-list-file-names
check "right-click > Show File Names is remembered" 'grep -q "ShowFileNames=true" $CONF/elisarc'
rclick 450 $ROW1;      shot song-right-click-menu-file-names-mode
click 530 $((ROW1+M_NAMES)) 3; shot song-list-adjusted-names-again
check "right-click > Show Adjusted Names switches back" '! grep -q "ShowFileNames=true" $CONF/elisarc'

# hide the first song (Human Race)
rclick 450 $ROW1
click 530 $((ROW1+M_HIDE)) 4; shot song-hidden-human-race
check "hidden song is in the hidden list" 'grep -q "Human Race" "$(HIDDEN)"'
check "hidden song file is still on disk" '[ -f "$H/01 Human Race.mp3" ]'

# delete the song that is first now (Painkiller): confirmation, then Delete
rclick 450 $ROW1
click 530 $((ROW1+M_DELETE)) 3; shot delete-song-confirmation
click ${DELETE_BTN_X:-560} ${DELETE_BTN_Y:-400} 4; shot after-delete-song
check "deleted song file is gone from disk" '[ ! -e "$H/02 - Painkiller (Official Video).mp3" ]'
check "other songs are still on disk" '[ -f "$H/12 The Real You.mp3" ] && [ -f "$H/16. Let You Down (Live).mp3" ]'
key Escape

# hide the whole album from the Albums grid
click 60 $ALBUMS 4
rclick $TILE1 320;     shot album-right-click-menu
click 380 ${ALBUM_HIDE_Y:-420} 5; shot album-human-hidden
check "hidden album is in the hidden list" 'grep -q "\"Human\"" "$(HIDDEN)" || grep -q "Human\"" "$(HIDDEN)"'
cp "$(HIDDEN)" $OUT/hidden-music-after-hiding.json 2>/dev/null

# Settings > Hidden music: switch the first entry off, then remove the other one
key ctrl+shift+comma 4; shot settings-hidden-music-list
click ${HM_SWITCH_X:-228} ${HM_ROW1_Y:-343} 4; shot settings-hidden-first-switched-off
cp "$(HIDDEN)" $OUT/hidden-music-after-switch.json 2>/dev/null
check "switching an entry off keeps it in the list, not hidden" 'grep -q "\"hidden\": *false" "$(HIDDEN)"'
click ${HM_REMOVE_X:-590} ${HM_ROW2_Y:-380} 4; shot settings-hidden-second-removed
cp "$(HIDDEN)" $OUT/hidden-music-after-remove.json 2>/dev/null
check "removing an entry leaves one entry" '[ "$(grep -c "\"title\"" "$(HIDDEN)")" = "1" ]'
key Escape
click 60 $ALBUMS 5;    shot albums-after-unhiding

# "Elisa: Import to Music", run the way the file manager runs it, while Elisa is open
DL=/tmp/downloads; rm -rf $DL; mkdir -p "$DL/zip/Imported LP"
for n in 1 2; do ffmpeg -loglevel error -y -f lavfi -i "sine=frequency=$((500+n*40)):duration=5" -metadata title="Zip Song $n" -metadata artist="Zip Band" -metadata album_artist="Zip Band" -metadata album="Imported LP" -metadata track=$n "$DL/zip/Imported LP/0$n Zip Song $n.mp3"; done
(cd $DL/zip && python3 -m zipfile -c "../Imported LP.zip" "Imported LP")
ffmpeg -loglevel error -y -f lavfi -i "sine=frequency=700:duration=5" -metadata title="Loose Import" -metadata artist="Zip Band" -metadata album="Loose Singles" -metadata track=1 "$DL/Loose Import.mp3"
ELISA_IMPORT_QUIET=1 $MENUDIR/elisa-import-to-music.sh "$DL/Imported LP.zip" "$DL/Loose Import.mp3" > $OUT/import-output.txt 2>&1
cat $OUT/import-output.txt
check "import: archive unpacked into the Music folder" '[ -f "$HOME/Music/Imported LP/01 Zip Song 1.mp3" ] && [ -f "$HOME/Music/Imported LP/02 Zip Song 2.mp3" ]'
check "import: single file moved into the Music folder" '[ -f "$HOME/Music/Loose Import.mp3" ] && [ ! -e "$DL/Loose Import.mp3" ]'
check "import: archive no longer in the source folder" '[ ! -e "$DL/Imported LP.zip" ]'
sleep 12
click 60 $ARTISTS 3; click 60 $ALBUMS 5; shot albums-after-import-without-restart
stop_elisa

########## RUN 4: update check at startup (pretends to be build 1) ##########
BEFORE=$(flatpak info --user org.kde.elisa 2>/dev/null | grep -i commit | head -n 1)
start_elisa run4 ELISA_FORK_BUILD_NUMBER_OVERRIDE=1
shot update-banner-at-startup
if [ -z "$APPIMAGE_PATH" ]; then
  # Flatpak: let Elisa install the published build by itself
  click ${UPDATE_BTN_X:-790} 207 60; shot update-after-update-now
  AFTER=$(flatpak info --user org.kde.elisa 2>/dev/null | grep -i commit | head -n 1)
  echo "before: $BEFORE / after: $AFTER" >> $OUT/checks.txt
  check "updater installed the published build (Flatpak)" '[ -n "$AFTER" ] && [ "$BEFORE" != "$AFTER" ]' WARN
else
  click 880 204 2;       shot menu-with-check-for-updates
  key Escape
fi
stop_elisa
find $HOME -name hidden-music.json -exec cp {} $OUT/hidden-music.json \; 2>/dev/null

cat $CONF/elisarc > $OUT/elisarc-after.txt
grep -iE "renderer|GL_RENDERER|software|rhi" $OUT/run1.log | head -20 > $OUT/renderer.txt
echo "embedded cover cache: $(grep -c 'embedded cover cache hit' $OUT/run1.log) hits, $(grep -c 'embedded cover cache miss' $OUT/run1.log) misses (first run)" > $OUT/cover-cache.txt
cat $OUT/cover-cache.txt
grep -iE "qml|failed|error|warning|cannot|undefined|TypeError|ReferenceError|crash|segmentation" $OUT/run*.log | grep -v "org.kde.elisa.views:" > $OUT/problems.txt || echo "none" > $OUT/problems.txt
ls -la $OUT
