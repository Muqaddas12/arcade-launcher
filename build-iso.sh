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

echo "[+] Step 3: Configuring X11, autologin, and console permissions..."
# Xwrapper config - allow non-console startx with root rights
cat << 'XWREOF' > "$ROOTFS/etc/X11/Xwrapper.config"
allowed_users=anybody
needs_root_rights=yes
XWREOF

# tty1 autologin for user malik
mkdir -p "$ROOTFS/etc/systemd/system/getty@tty1.service.d"
cat << 'AUTOLOGINEOF' > "$ROOTFS/etc/systemd/system/getty@tty1.service.d/autologin.conf"
[Service]
ExecStart=
ExecStart=-/sbin/agetty --autologin malik --noclear %I $TERM
Type=idle
AUTOLOGINEOF

# Configure malik shell profile to auto-launch X on tty1
mkdir -p "$ROOTFS/home/malik"
cat << 'PROFILESCRIPTEOF' > "$ROOTFS/home/malik/.bash_profile"
if [ -z "$DISPLAY" ] && [ "$(tty)" = "/dev/tty1" ]; then
    exec startx -- -keeptty
fi
PROFILESCRIPTEOF

cat << 'XINITEOF' > "$ROOTFS/home/malik/.xinitrc"
#!/bin/bash
# Start Openbox window manager to manage fullscreen, window raising & focus
openbox &
xset s off -dpms 2>/dev/null || true
xsetroot -cursor_name left_ptr 2>/dev/null || true
exec /opt/malik-game-os/start.sh
XINITEOF
chmod +x "$ROOTFS/home/malik/.xinitrc"

# Disable legacy service so tty1 autologin cleanly owns the display
rm -f "$ROOTFS/etc/systemd/system/multi-user.target.wants/malik-game-os.service" \
      "$ROOTFS/etc/systemd/system/graphical.target.wants/malik-game-os.service" \
      "$ROOTFS/etc/systemd/system/malik-game-os.service" 2>/dev/null || true

# Fix ownership for user 'malik' inside rootfs
MALIK_UID=1000
MALIK_GID=1000
chown -R "$MALIK_UID:$MALIK_GID" "$DEST_OPT"
chown -R "$MALIK_UID:$MALIK_GID" "$ROOTFS/home/malik"

echo "[+] Step 4: Installing openbox and regenerating initramfs with live-boot hooks..."
cp -f /etc/resolv.conf "$ROOTFS/etc/resolv.conf" 2>/dev/null || true
mount --bind /dev "$ROOTFS/dev" 2>/dev/null || true
mount --bind /dev/pts "$ROOTFS/dev/pts" 2>/dev/null || true
mount -t proc proc "$ROOTFS/proc" 2>/dev/null || true
mount -t sysfs sysfs "$ROOTFS/sys" 2>/dev/null || true

# Install openbox if not already present
chroot "$ROOTFS" apt update
chroot "$ROOTFS" apt install -y --no-install-recommends openbox

# Update initramfs inside rootfs so live-boot hooks are embedded
chroot "$ROOTFS" update-initramfs -u -k all

umount -l "$ROOTFS/sys" 2>/dev/null || true
umount -l "$ROOTFS/proc" 2>/dev/null || true
umount -l "$ROOTFS/dev/pts" 2>/dev/null || true
umount -l "$ROOTFS/dev" 2>/dev/null || true

echo "[+] Step 5: Preparing live boot kernel and initrd..."
mkdir -p "$ISO_DIR/live"
mkdir -p "$ISO_DIR/boot/grub"
mkdir -p "$OUTPUT_DIR"

VMLINUZ=$(find "$ROOTFS/boot" -maxdepth 1 -name "vmlinuz*" | sort -V | tail -n 1)
INITRD=$(find "$ROOTFS/boot" -maxdepth 1 -name "initrd.img*" | sort -V | tail -n 1)

if [ -z "$VMLINUZ" ] || [ -z "$INITRD" ]; then
    echo "[-] Error: Kernel or initrd not found in $ROOTFS/boot/"
    exit 1
fi

echo "    Using Kernel: $(basename "$VMLINUZ")"
echo "    Using Initrd: $(basename "$INITRD")"

cp -f "$VMLINUZ" "$ISO_DIR/live/vmlinuz"
cp -f "$INITRD" "$ISO_DIR/live/initrd.img"

echo "[+] Step 6: Configuring GRUB bootloader..."
cat << 'GRUBEOF' > "$ISO_DIR/boot/grub/grub.cfg"
set timeout=3
set default=0

menuentry "Malik Game OS (Arcade Console)" {
    linux /live/vmlinuz boot=live components
    initrd /live/initrd.img
}

menuentry "Malik Game OS (Safe Graphics / Nomodeset)" {
    linux /live/vmlinuz boot=live nomodeset components
    initrd /live/initrd.img
}

menuentry "Malik Game OS (Debug Shell)" {
    linux /live/vmlinuz boot=live components debug
    initrd /live/initrd.img
}
GRUBEOF

echo "[+] Step 7: Building compressed squashfs filesystem (this may take 1-2 minutes)..."
rm -f "$ISO_DIR/live/filesystem.squashfs"
mksquashfs "$ROOTFS" "$ISO_DIR/live/filesystem.squashfs" \
    -comp xz \
    -e boot \
    -noappend

echo "[+] Step 8: Generating final bootable ISO (MalikGameOS.iso)..."
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
