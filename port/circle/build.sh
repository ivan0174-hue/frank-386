#!/usr/bin/env bash
#
# FRANK 386 – Raspberry Pi 4 bare-metal build script
#
# Downloads (once) the Arm GNU AArch64 bare-metal toolchain and
# circle-stdlib (Circle + newlib), builds kernel8-rpi4.img and assembles
# a ready-to-copy SD card folder in port/circle/dist/sdcard.
#
# Usage:   ./build.sh            build everything
#          ./build.sh clean      remove build outputs (keeps toolchain/Circle)
#
# Requirements on the host (Debian/Ubuntu/Raspberry Pi OS):
#   sudo apt install build-essential git curl xz-utils texinfo
#
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
TOOLS="${FRANK_TOOLS:-$HERE/.tools}"
TC_VER="13.3.rel1"
CIRCLE_STDLIB_COMMIT="4fbab627a86f2bfba1697c1b4d3f310c48be27aa"   # Circle Step 51.1.1
FW_COMMIT="0641c5bf25d185d5bf25d8dcafd1da6dc8fdbf68"
FW_URL="https://raw.githubusercontent.com/raspberrypi/firmware/$FW_COMMIT/boot"
JOBS="$(nproc 2>/dev/null || echo 4)"

if [ "${1:-}" = "clean" ]; then
	make -C "$HERE" CIRCLE_STDLIB="$TOOLS/circle-stdlib" clean distclean || true
	rm -rf "$HERE/dist"
	exit 0
fi

mkdir -p "$TOOLS"

# --- 1. toolchain ---------------------------------------------------------
case "$(uname -m)" in
	x86_64)  HOSTARCH=x86_64 ;;
	aarch64) HOSTARCH=aarch64 ;;
	*) echo "Unsupported build host $(uname -m)"; exit 1 ;;
esac
TC_DIR="$TOOLS/arm-gnu-toolchain-$TC_VER-$HOSTARCH-aarch64-none-elf"
if [ ! -x "$TC_DIR/bin/aarch64-none-elf-gcc" ]; then
	echo ">>> Downloading Arm GNU toolchain $TC_VER ($HOSTARCH)"
	curl -L --fail -o "$TOOLS/tc.tar.xz" \
	  "https://developer.arm.com/-/media/Files/downloads/gnu/$TC_VER/binrel/arm-gnu-toolchain-$TC_VER-$HOSTARCH-aarch64-none-elf.tar.xz"
	tar -C "$TOOLS" -xf "$TOOLS/tc.tar.xz"
	rm -f "$TOOLS/tc.tar.xz"
fi
export PATH="$TC_DIR/bin:$PATH"

# --- 2. circle-stdlib -----------------------------------------------------
CS="$TOOLS/circle-stdlib"
if [ ! -f "$CS/install/aarch64-none-circle/lib/libc.a" ] || [ ! -f "$CS/libs/circle/lib/libcircle.a" ]; then
	if [ ! -d "$CS/.git" ]; then
		echo ">>> Cloning circle-stdlib"
		git clone https://github.com/smuehlst/circle-stdlib.git "$CS"
		git -C "$CS" checkout "$CIRCLE_STDLIB_COMMIT"
		git -C "$CS" submodule update --init --recursive libs/circle libs/circle-newlib
	fi
	echo ">>> Building Circle + newlib for Raspberry Pi 4 (AArch64)"
	( cd "$CS" &&
	  ./configure -r 4 -p aarch64-none-elf- --kernel-max-size 16 \
	              -o ARM_ALLOW_MULTI_CORE -o SAVE_VFP_REGS_ON_IRQ &&
	  make -j"$JOBS" || true )   # the optional WLAN firmware step may fail; harmless
	test -f "$CS/libs/circle/lib/libcircle.a"
	test -f "$CS/install/aarch64-none-circle/lib/libc.a"
fi

# --- 3. kernel ------------------------------------------------------------
echo ">>> Building kernel8-rpi4.img"
make -C "$HERE" -j"$JOBS" CIRCLE_STDLIB="$CS"

# --- 4. SD card folder ----------------------------------------------------
echo ">>> Assembling SD card folder"
OUT="$HERE/dist/sdcard"
rm -rf "$OUT"
mkdir -p "$OUT"
cp -r "$HERE/sdcard/." "$OUT/"
cp "$HERE/kernel8-rpi4.img" "$OUT/"

if [ ! -f "$CS/libs/circle/boot/armstub8-rpi4.bin" ]; then
	make -C "$CS/libs/circle/boot" armstub64
fi
cp "$CS/libs/circle/boot/armstub8-rpi4.bin" "$OUT/"

FW_CACHE="$TOOLS/firmware-$FW_COMMIT"
mkdir -p "$FW_CACHE"
for f in start4.elf fixup4.dat bcm2711-rpi-4-b.dtb bcm2711-rpi-400.dtb bcm2711-rpi-cm4.dtb LICENCE.broadcom COPYING.linux; do
	[ -s "$FW_CACHE/$f" ] || curl -L --fail -o "$FW_CACHE/$f" "$FW_URL/$f"
	cp "$FW_CACHE/$f" "$OUT/"
done

echo
echo "Done. Copy the contents of"
echo "    $OUT"
echo "to the root of a FAT32-formatted micro-SD card."
