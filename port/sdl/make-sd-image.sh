#!/usr/bin/env bash
#
# Create sdcard.img: a FAT32 image with the same layout as the real
# Raspberry Pi 4 SD card (386/ folder), for the Linux/SDL2 build.
#
#   ./make-sd-image.sh [image] [size-MB]
#
# Needs: mtools, dosfstools.  Add your own disk images afterwards with
#   mcopy -i sdcard.img mydisk.img ::/386/
#
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
IMG="${1:-sdcard.img}"
SIZE="${2:-512}"
SRC="$HERE/../circle/sdcard/386"

rm -f "$IMG"
truncate -s "${SIZE}M" "$IMG"
mkfs.fat -F 32 -n FRANK386 "$IMG" >/dev/null
export MTOOLS_SKIP_CHECK=1
mmd -i "$IMG" ::/386
mcopy -i "$IMG" "$SRC"/* ::/386/
mdir -i "$IMG" ::/386
echo "Created $IMG"
