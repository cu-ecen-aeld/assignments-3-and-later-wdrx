#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.

set -e
set -u

OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.15.163
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64

if [ $(which aarch64-linux-gnu-gcc) ]; then
    CROSS_COMPILE=aarch64-linux-gnu-
elif [ $(which aarch64-none-linux-gnu-gcc) ]; then
    CROSS_COMPILE=aarch64-none-linux-gnu-
else
    echo "No cross compiler found. Please install aarch64-linux-gnu-gcc or aarch64-none-linux-gnu-gcc."
    exit 1
fi


if [ $# -lt 1 ]
then
	echo "Using default directory ${OUTDIR} for output"
else
	OUTDIR=$1
	echo "Using passed directory ${OUTDIR} for output"
fi

mkdir -p ${OUTDIR}

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/linux-stable" ]; then
    #Clone only if the repository does not exist.
	echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
	git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION}
fi
if [ ! -e ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ]; then
    cd linux-stable
    echo "Checking out version ${KERNEL_VERSION}"
    git checkout ${KERNEL_VERSION}

    echo "Configuring kernel build"
    make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE -j$(nproc) defconfig
    make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE -j$(nproc) all
    make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE -j$(nproc) modules
    make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE -j$(nproc) dtbs
fi

echo "Adding the Image in outdir"
cp ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ${OUTDIR}

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]
then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm  -rf ${OUTDIR}/rootfs
fi

# Create necessary base directories
echo "Creating the root filesystem itself"
mkdir ${OUTDIR}/rootfs && cd ${OUTDIR}/rootfs
mkdir -p bin dev etc home lib lib64 proc sbin sys tmp usr var
mkdir -p usr/bin usr/lib usr/sbin
mkdir -p var/log

echo "Back to ${OUTDIR}"
echo "Now creating BusyBox"

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]
then
    echo "Cloning BusyBox"
    git clone git://busybox.net/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
    echo "Checked out BusyBox version ${BUSYBOX_VERSION}"
    echo "Cleaning and configuring BusyBox"
    make distclean
    make defconfig
else
    echo "Busybox directory exists. Skipping clone and checkout. Delete it if you want to re-clone and checkout."
    cd busybox
fi

# Make busybox
if [ -f "${OUTDIR}/busybox/busybox" ]
then
    echo "BusyBox already built. Skipping build. Make clean in ${OUTDIR}/busybox if you want to re-build."
else
    echo "Building BusyBox"
    make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE
fi

# Install BusyBox
echo "Installing BusyBox"
make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE CONFIG_PREFIX=${OUTDIR}/rootfs install

echo "Library dependencies"
${CROSS_COMPILE}readelf -a ${OUTDIR}/rootfs/bin/busybox | grep "program interpreter"
${CROSS_COMPILE}readelf -a ${OUTDIR}/rootfs/bin/busybox | grep "Shared library"

# Add library dependencies to rootfs
# /usr/lib/aarch64-linux-gnu/ld-linux-aarch64.so.1    
# /usr/lib/aarch64-linux-gnu/libm.so.6
# /usr/lib/aarch64-linux-gnu/libresolv.so.2
# /usr/lib/aarch64-linux-gnu/libc.so.6
SYSROOT=$(${CROSS_COMPILE}gcc -print-sysroot)

find $SYSROOT -name ld-linux-aarch64.so.1 -exec cp {} ${OUTDIR}/rootfs/lib \;
find $SYSROOT -name libc.so.6 -exec cp {} ${OUTDIR}/rootfs/lib \;
find $SYSROOT -name libm.so.6 -exec cp {} ${OUTDIR}/rootfs/lib \;
find $SYSROOT -name libresolv.so.2 -exec cp {} ${OUTDIR}/rootfs/lib \;

# Make device nodes
sudo mknod -m 666 ${OUTDIR}/rootfs/dev/null c 1 3
sudo mknod -m 600 ${OUTDIR}/rootfs/dev/console c 5 1

# Clean and build the writer utility
echo "Back to $FINDER_APP_DIR to build the writer utility"
cd "$FINDER_APP_DIR"
make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE clean
make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE

# Copy the finder related scripts and executables to the /home directory
# on the target rootfs
echo "Copying scripts and executables to ${OUTDIR}/rootfs/home"
cp -v writer "${OUTDIR}/rootfs/home/"
cp -v finder.sh "${OUTDIR}/rootfs/home/"
cp -v finder-test.sh "${OUTDIR}/rootfs/home/"
cp -v autorun-qemu.sh "${OUTDIR}/rootfs/home/"
mkdir -p "${OUTDIR}/rootfs/home/conf"
cp -v ../conf/* "${OUTDIR}/rootfs/home/conf/"

# Chown the root directory
sudo chown -R root:root "${OUTDIR}/rootfs"

# Create initramfs.cpio.gz
echo "Creating initramfs.cpio.gz"
cd "$OUTDIR/rootfs"
find . -print0 | cpio --null -ov --format=newc | gzip -9 > "$OUTDIR/initramfs.cpio.gz"
