#!/bin/bash

# Copyright (C) 2026 CharOfString
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

# 生成 gxde-lsg 使用的 rootfs 压缩包 gxde-rootfs.tar.xz（debian/rules 与 rpm spec 会把它打进安装包）。
#
# 流程：debootstrap 出 Debian trixie 基础系统 → 从 repo.gxde.top 安装 gxde-source
# （GXDE 软件源与签名密钥），可选 gxde-testing-source（测试源 zhuangzhuang）→ 安装 GXDE 桌面 → 清理 → 打包。
#
# 打包时保留 xattr（file capability，如 gxde-dock 的 cap_ipc_lock）、ACL 与数值属主，
# 解压时需使用对应参数，见 NOTE.md。
#
# 需要 root 运行，依赖 debootstrap、debian-archive-keyring、curl、xz、tar；
# 宿主机可以不是 Debian（如 Arch Linux）。只支持构建与宿主机相同架构的 rootfs。
#
# 用法: sudo ./build-rootfs.sh [选项]
#
# 选项:
#    -o, --output FILE     输出文件（默认: 脚本所在目录/gxde-rootfs.tar.xz）
#    -m, --mirror URL      Debian 镜像（默认: https://deb.debian.org/debian）
#    -w, --workdir DIR     工作目录，需在支持 xattr 的磁盘文件系统上（默认: /var/tmp 下新建）
#        --no-testing      不加入 GXDE 测试源（zhuangzhuang），只用稳定源（lizhi）
#        --keep            构建完成后保留工作目录中的 rootfs，便于检查
#    -h, --help            打印帮助信息

set -eo pipefail

PROJ_ROOT="$(cd "$(dirname "$0")" && pwd)"

SUITE=trixie
MIRROR=https://deb.debian.org/debian
SECURITY_MIRROR=https://security.debian.org/debian-security
GXDE_REPO=https://repo.gxde.top/gxde-os
OUTPUT="$PROJ_ROOT/gxde-rootfs.tar.xz"
WORKDIR=""
WITH_TESTING=true
KEEP=false

# 与现有 rootfs 手动安装的软件包一致；后几项是容器运行所需（sudo、locale 生成、用户会话总线），
# 显式列出以免桌面元包的依赖变化后缺失
PACKAGES=(
	gxde-desktop gxde-desktop-extra gxde-flake-session aptss
	ca-certificates curl sudo locales dbus-user-session libpam-systemd
)

print_help() {
	sed -n '/^# 用法/,/^# *-h/p' "$0" | sed 's/^# \{0,1\}//'
	exit 0
}

die() {
	echo "错误: $*" >&2
	exit 1
}

log() {
	echo "==> $*"
}

parse_args() {
	while [[ $# -gt 0 ]]; do
		case $1 in
			-o|--output) OUTPUT="$(realpath -m "$2")"; shift 2 ;;
			-m|--mirror) MIRROR="$2"; shift 2 ;;
			-w|--workdir) WORKDIR="$2"; shift 2 ;;
			--no-testing) WITH_TESTING=false; shift ;;
			--keep) KEEP=true; shift ;;
			-h|--help) print_help ;;
			*) die "未知选项: $1（--help 查看用法）" ;;
		esac
	done
}

check_env() {
	[[ $EUID -eq 0 ]] || die "需要 root 运行：sudo $0"
	local tool missing=()
	for tool in debootstrap curl xz tar chroot mount umount; do
		command -v "$tool" >/dev/null || missing+=("$tool")
	done
	[[ ${#missing[@]} -eq 0 ]] || die "缺少工具: ${missing[*]}"
	[[ -f /usr/share/keyrings/debian-archive-keyring.gpg ]] ||
		die "缺少 /usr/share/keyrings/debian-archive-keyring.gpg（安装 debian-archive-keyring）"

	case "$(uname -m)" in
		x86_64) ARCH=amd64 ;;
		aarch64) ARCH=arm64 ;;
		mips64) ARCH=mips64el ;;
		loongarch64) ARCH=loong64 ;;
		riscv64) ARCH=riscv64 ;;
		*) die "不支持的架构: $(uname -m)" ;;
	esac
}

# ---- chroot 挂载与清理 ----

MOUNTS=()

mount_chroot() {
	local dir
	for dir in proc sys dev dev/pts; do
		mkdir -p "$ROOTFS/$dir"
	done
	# 挂载失败时 set -e 直接退出，cleanup 只卸载已记录的挂载点
	mount -t proc proc "$ROOTFS/proc"
	MOUNTS+=("$ROOTFS/proc")
	mount -t sysfs sysfs "$ROOTFS/sys"
	MOUNTS+=("$ROOTFS/sys")
	mount --bind /dev "$ROOTFS/dev"
	MOUNTS+=("$ROOTFS/dev")
	mount --bind /dev/pts "$ROOTFS/dev/pts"
	MOUNTS+=("$ROOTFS/dev/pts")
}

umount_chroot() {
	local i
	for ((i = ${#MOUNTS[@]} - 1; i >= 0; i--)); do
		umount -l "${MOUNTS[i]}" 2>/dev/null || true
	done
	MOUNTS=()
}

cleanup() {
	umount_chroot
	if [[ -n "$ROOTFS" && "$KEEP" != true && -d "$WORKDIR" ]]; then
		rm -rf --one-file-system "$WORKDIR"
	fi
}

in_chroot() {
	chroot "$ROOTFS" /usr/bin/env -i \
		PATH=/usr/sbin:/usr/bin:/sbin:/bin HOME=/root LANG=C.UTF-8 \
		DEBIAN_FRONTEND=noninteractive "$@"
}

# ---- 构建步骤 ----

bootstrap() {
	log "debootstrap $SUITE ($ARCH) from $MIRROR"
	debootstrap --arch="$ARCH" \
		--components=main,contrib,non-free,non-free-firmware \
		--include=ca-certificates \
		"$SUITE" "$ROOTFS" "$MIRROR"
}

write_debian_sources() {
	log "写入 Debian 软件源"
	local components="main contrib non-free non-free-firmware"
	echo "deb $MIRROR $SUITE $components" > "$ROOTFS/etc/apt/sources.list"
	cat > "$ROOTFS/etc/apt/sources.list.d/debian-extra.list" <<EOF
deb $MIRROR $SUITE-updates $components
deb $SECURITY_MIRROR $SUITE-security $components
deb $MIRROR $SUITE-backports $components
EOF
}

# 从 GXDE 平铺仓库的索引中查出软件包文件，下载并校验 SHA256 后安装
install_from_gxde_repo() {
	local repo="$1" package="$2"
	local index entry filename sha256 deb
	log "从 $GXDE_REPO/$repo 安装 $package"
	index="$(curl -fsSL --retry 3 "$GXDE_REPO/$repo/Packages.xz" | xz -dc)"
	entry="$(awk -v p="$package" 'BEGIN { RS = "" } $0 ~ "^Package: " p "\n" { print; exit }' <<<"$index")"
	filename="$(sed -n 's/^Filename: \(\.\/\)\{0,1\}//p' <<<"$entry")"
	sha256="$(sed -n 's/^SHA256: //p' <<<"$entry")"
	[[ -n "$filename" && -n "$sha256" ]] || die "$GXDE_REPO/$repo 的索引中找不到 $package"

	deb="$ROOTFS/tmp/$(basename "$filename")"
	curl -fsSL --retry 3 -o "$deb" "$GXDE_REPO/$repo/$filename"
	echo "$sha256  $deb" | sha256sum -c --quiet - || die "$package 校验失败"
	in_chroot dpkg -i "/tmp/$(basename "$filename")"
	rm -f "$deb"
}

install_gxde() {
	# 构建期间禁止 chroot 内的软件包启动服务
	printf '#!/bin/sh\nexit 101\n' > "$ROOTFS/usr/sbin/policy-rc.d"
	chmod 755 "$ROOTFS/usr/sbin/policy-rc.d"
	cp -L /etc/resolv.conf "$ROOTFS/etc/resolv.conf"

	install_from_gxde_repo lizhi gxde-source
	if [[ "$WITH_TESTING" == true ]]; then
		# 测试源的源包本身发布在稳定源中
		install_from_gxde_repo lizhi gxde-testing-source
	fi

	log "安装 GXDE 桌面: ${PACKAGES[*]}"
	in_chroot apt-get update
	in_chroot apt-get -y full-upgrade
	in_chroot apt-get -y --install-recommends install "${PACKAGES[@]}"
	in_chroot apt-get -y autoremove --purge
	in_chroot apt-get clean
}

finalize() {
	log "清理 rootfs"
	rm -f "$ROOTFS/usr/sbin/policy-rc.d"
	# 容器启动时由 gxde-lsg 写入
	rm -f "$ROOTFS/etc/resolv.conf"
	# 每个容器首次启动时生成自己的 machine-id
	: > "$ROOTFS/etc/machine-id"
	rm -f "$ROOTFS/var/lib/dbus/machine-id"
	# gxde-lsg 以 etc/hostname 是否存在判断 rootfs 是否已解压，必须保留；
	# 实际主机名由 gxde-lsg 在容器启动前写入
	echo gxde > "$ROOTFS/etc/hostname"
	rm -rf "$ROOTFS"/var/lib/apt/lists/* "$ROOTFS"/var/cache/apt/*.bin "$ROOTFS"/tmp/* "$ROOTFS"/var/tmp/*
	find "$ROOTFS/var/log" -type f -exec truncate -s 0 {} +

	if command -v getcap >/dev/null; then
		log "rootfs 中的 file capability:"
		getcap -r "$ROOTFS/usr" 2>/dev/null | sed "s|$ROOTFS||;s/^/    /"
	fi
	[[ -u "$ROOTFS/usr/bin/sudo" ]] || die "rootfs 中的 sudo 不是 setuid"
}

pack() {
	log "打包到 $OUTPUT"
	mkdir -p "$(dirname "$OUTPUT")"
	# 参数与 NOTE.md 一致：保留 xattr（含 security.capability）、ACL、数值属主
	XZ_OPT="${XZ_OPT:--T0 -6}" tar -cJpf "$OUTPUT.tmp" -C "$ROOTFS" \
		--xattrs --xattrs-include='*' --acls --numeric-owner \
		--one-file-system .
	mv -f "$OUTPUT.tmp" "$OUTPUT"
	log "完成: $OUTPUT ($(du -h "$OUTPUT" | cut -f1))"
}

main() {
	parse_args "$@"
	check_env

	if [[ -z "$WORKDIR" ]]; then
		WORKDIR="$(mktemp -d /var/tmp/gxde-rootfs.XXXXXX)"
	else
		mkdir -p "$WORKDIR"
		[[ -z "$(ls -A "$WORKDIR")" ]] || die "工作目录不为空: $WORKDIR"
	fi
	ROOTFS="$WORKDIR/rootfs"
	trap cleanup EXIT

	bootstrap
	write_debian_sources
	mount_chroot
	install_gxde
	umount_chroot
	finalize
	pack

	if [[ "$KEEP" == true ]]; then
		log "rootfs 保留在 $ROOTFS"
	fi
}

main "$@"
