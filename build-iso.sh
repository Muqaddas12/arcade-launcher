#!/bin/bash
# ==============================================================================
# Malik Game OS - Automated ISO Build Script
# Creates a bootable live arcade OS ISO ready for VirtualBox / VMware / QEMU / PC
# ==============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "============================================================"
echo "          Malik Game OS - Bootable ISO Builder"
echo "============================================================"

# Ensure script is run with root privileges
if [ "$EUID" -ne 0 ]; then
    echo "[-] Error: This script must be run with sudo or as root."
    echo "    Please run: sudo ./build-iso.sh"
    exit 1
fi

ROOTFS="$SCRIPT_DIR/os-build/rootfs"
ISO_DIR="$SCRIPT_DIR/os-build/iso"
OUTPUT_DIR="$SCRIPT_DIR/os-build/output"
DEST_OPT="$ROOTFS/opt/malik-game-os"

if [ ! -d "$ROOTFS" ]; then
    echo "[-] Error: $ROOTFS does not exist. Please check your setup."
    exit 1
fi

echo "[+] Step 1: Compiling latest malik-game-os binary..."
mkdir -p build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

echo "[+] Step 2: Preparing /opt/malik-game-os inside rootfs..."
mkdir -p "$DEST_OPT"
mkdir -p "$DEST_OPT/bin"
mkdir -p "$DEST_OPT/config"
mkdir -p "$DEST_OPT/assets"
mkdir -p "$DEST_OPT/games"
mkdir -p "$DEST_OPT/emulators"

# Copy latest binary
cp -f build/malik-game-os "$DEST_OPT/malik-game-os"
chmod +x "$DEST_OPT/malik-game-os"

# Copy configuration and assets
cp -rf config/* "$DEST_OPT/config/"
if [ -d "assets" ]; then
    cp -rf assets/* "$DEST_OPT/assets/" 2>/dev/null || true
fi

# Copy wrapper scripts
cp -f bin/duckstation "$DEST_OPT/bin/duckstation"
cp -f bin/pcsx2 "$DEST_OPT/bin/pcsx2"
chmod +x "$DEST_OPT/bin/duckstation" "$DEST_OPT/bin/pcsx2"

# Symlink to system path inside rootfs
mkdir -p "$ROOTFS/usr/local/bin"
ln -sf /opt/malik-game-os/bin/duckstation "$ROOTFS/usr/local/bin/duckstation"
ln -sf /opt/malik-game-os/bin/pcsx2 "$ROOTFS/usr/local/bin/pcsx2"

# Copy games if present
if [ -d "games" ]; then
    echo "[+] Syncing games..."
    cp -rf games/* "$DEST_OPT/games/" 2>/dev/null || true
fi

# Copy emulators bundle if present
if [ -d "emulators/pcsx2-squashfs-root" ]; then
    echo "[+] Syncing emulator runtime bundle (pcsx2-squashfs-root)..."
    mkdir -p "$DEST_OPT/emulators/pcsx2-squashfs-root"
    rsync -a --delete emulators/pcsx2-squashfs-root/ "$DEST_OPT/emulators/pcsx2-squashfs-root/"
fi

# Setup start script
cat << 'STARTEOF' > "$DEST_OPT/start.sh"
#!/bin/bash
export DISPLAY=:0
export XDG_RUNTIME_DIR="/run/user/$(id -u)"
cd /opt/malik-game-os
exec ./malik-game-os
STARTEOF
chmod +x "$DEST_OPT/start.sh"

# Fix ownership for user 'malik' inside rootfs
if id "malik" &>/dev/null; then
    MALIK_UID=$(id -u malik)
    MALIK_GID=$(id -g malik)
else
    MALIK_UID=1000
    MALIK_GID=1000
fi

chown -R "$MALIK_UID:$MALIK_GID" "$DEST_OPT"
if [ -d "$ROOTFS/home/malik" ]; then
    chown -R "$MALIK_UID:$MALIK_GID" "$ROOTFS/home/malik"
fi

echo "[+] Step 3: Preparing live boot kernel and initrd..."
mkdir -p "$ISO_DIR/live"
mkdir -p "$ISO_DIR/boot/grub"
mkdir -p "$OUTPUT_DIR"

VMLINUZ=$(find "$ROOTFS/boot" -maxdepth 1 -name "vmlinuz*" | head -n 1)
INITRD=$(find "$ROOTFS/boot" -maxdepth 1 -name "initrd.img*" | head -n 1)

if [ -z "$VMLINUZ" ] || [ -z "$INITRD" ]; then
    echo "[-] Error: Kernel or initrd not found in $ROOTFS/boot/"
    exit 1
fi

echo "    Using Kernel: $(basename "$VMLINUZ")"
echo "    Using Initrd: $(basename "$INITRD")"

cp -f "$VMLINUZ" "$ISO_DIR/live/vmlinuz"
cp -f "$INITRD" "$ISO_DIR/live/initrd.img"

echo "[+] Step 4: Configuring GRUB bootloader..."
cat << 'GRUBEOF' > "$ISO_DIR/boot/grub/grub.cfg"
set timeout=3
set default=0

menuentry "Malik Game OS (Arcade Console)" {
    linux /live/vmlinuz boot=live quiet splash components
    initrd /live/initrd.img
}

menuentry "Malik Game OS (Failsafe / Nomodeset)" {
    linux /live/vmlinuz boot=live nomodeset
    initrd /live/initrd.img
}
GRUBEOF

echo "[+] Step 5: Building compressed squashfs filesystem (this may take 1-2 minutes)..."
rm -f "$ISO_DIR/live/filesystem.squashfs"
mksquashfs "$ROOTFS" "$ISO_DIR/live/filesystem.squashfs" \
    -comp xz \
    -e boot \
    -noappend

echo "[+] Step 6: Generating final bootable ISO (MalikGameOS.iso)..."
ISO_OUTPUT="$OUTPUT_DIR/MalikGameOS.iso"
rm -f "$ISO_OUTPUT"
grub-mkrescue -o "$ISO_OUTPUT" "$ISO_DIR"

echo "============================================================"
echo "  SUCCESS! Final Bootable ISO is ready:"
echo "  Location: $ISO_OUTPUT"
ls -lh "$ISO_OUTPUT"
echo "============================================================"
echo "You can now attach this ISO directly to VirtualBox or VMware"
echo "and boot into Malik Game OS!"
