#!/bin/sh
# "Elisa: Import to Music" - the file manager's right-click action added by Elisa.
# Moves the selected files and folders into the Music folder; archives are unpacked there.
# SPDX-License-Identifier: LGPL-3.0-or-later

dest=${ELISA_IMPORT_DEST:-}
if [ -z "$dest" ]; then
    dest=$(xdg-user-dir MUSIC 2>/dev/null)
    if [ -z "$dest" ] || [ "$dest" = "$HOME" ] || [ "$dest" = "$HOME/" ]; then
        dest="$HOME/Music"
    fi
fi
mkdir -p "$dest" || exit 1

moved=0
failed=0
problems=""

# a path that does not exist yet: "Name", "Name (2)", "Name (3)"...
unique() {
    p=$1
    if [ ! -e "$p" ]; then printf '%s' "$p"; return; fi
    dir=${p%/*}; name=${p##*/}
    case "$name" in
        ?*.*) [ -d "$p" ] && { stem=$name; ext=; } || { stem=${name%.*}; ext=.${name##*.}; } ;;
        *) stem=$name; ext= ;;
    esac
    n=2
    while [ -e "$dir/$stem ($n)$ext" ]; do n=$((n + 1)); done
    printf '%s' "$dir/$stem ($n)$ext"
}

have() { command -v "$1" >/dev/null 2>&1; }

# unpack archive $1 into the empty folder $2
unpack() {
    case "$3" in
        zip)
            if have unzip; then unzip -q -o "$1" -d "$2"
            elif have bsdtar; then bsdtar -xf "$1" -C "$2"
            elif have 7z; then 7z x -y -o"$2" "$1" >/dev/null
            elif have python3; then python3 -m zipfile -e "$1" "$2"
            else return 1; fi ;;
        tar)
            if have bsdtar; then bsdtar -xf "$1" -C "$2"; else tar -xf "$1" -C "$2"; fi ;;
        other)
            if have bsdtar; then bsdtar -xf "$1" -C "$2"
            elif have 7z; then 7z x -y -o"$2" "$1" >/dev/null
            elif have unar; then unar -q -o "$2" "$1" >/dev/null
            else return 1; fi ;;
    esac
}

remove_original() {
    if have gio && gio trash "$1" 2>/dev/null; then return; fi
    rm -f -- "$1"
}

import_archive() {   # $1 = archive, $2 = kind, $3 = name without extension
    tmp="$dest/.elisa-import-$$"
    rm -rf "$tmp"; mkdir -p "$tmp" || return 1
    if ! unpack "$1" "$tmp" "$2"; then rm -rf "$tmp"; return 1; fi
    count=$(ls -A "$tmp" | wc -l)
    only=$(ls -A "$tmp" | head -n 1)
    if [ "$count" -eq 0 ]; then rm -rf "$tmp"; return 1; fi
    if [ "$count" -eq 1 ] && [ -d "$tmp/$only" ]; then
        # the archive holds one folder: keep that folder, not a folder in a folder
        mv -- "$tmp/$only" "$(unique "$dest/$only")" && rmdir "$tmp"
    else
        mv -- "$tmp" "$(unique "$dest/$3")"
    fi
}

for item in "$@"; do
    [ -e "$item" ] || continue
    item=$(realpath -- "$item" 2>/dev/null || printf '%s' "$item")
    name=${item##*/}
    lower=$(printf '%s' "$name" | tr '[:upper:]' '[:lower:]')

    # already in the Music folder: nothing to move (archives there are still unpacked)
    inside=0
    case "$item" in "$dest"/*) inside=1 ;; esac

    kind=
    if [ -f "$item" ]; then
        case "$lower" in
            *.zip) kind=zip; stem=${name%.*} ;;
            *.tar.gz|*.tar.bz2|*.tar.xz|*.tar.zst) kind=tar; stem=${name%.*}; stem=${stem%.*} ;;
            *.tar|*.tgz|*.tbz2|*.txz) kind=tar; stem=${name%.*} ;;
            *.7z|*.rar) kind=other; stem=${name%.*} ;;
        esac
    fi

    if [ -n "$kind" ]; then
        if import_archive "$item" "$kind" "$stem"; then
            remove_original "$item"
            moved=$((moved + 1))
        else
            failed=$((failed + 1)); problems="$problems
$name"
        fi
    elif [ "$inside" -eq 1 ]; then
        continue
    elif mv -- "$item" "$(unique "$dest/$name")"; then
        moved=$((moved + 1))
    else
        failed=$((failed + 1)); problems="$problems
$name"
    fi
done

if [ "$failed" -gt 0 ]; then
    message="$moved imported into $dest, $failed could not be imported:$problems"
else
    message="$moved imported into $dest"
fi
echo "$message"
if [ -z "${ELISA_IMPORT_QUIET:-}" ]; then
    if have notify-send; then notify-send -a Elisa -i folder-music "Elisa: Import to Music" "$message"
    elif have kdialog; then kdialog --title "Elisa: Import to Music" --passivepopup "$message" 6
    fi
fi
[ "$failed" -eq 0 ]
