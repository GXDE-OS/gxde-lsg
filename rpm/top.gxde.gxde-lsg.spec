Summary: 利用 systemd-nspawn 在其它 Linux 发行版（包括非 Debian 发行版）运行 Debian + GXDE 桌面环境
Name: top.gxde.gxde-lsg
Version: 1.0.1
Release: 0
License: GPLv3+
URL: https://gitee.com/GXDE-OS/gxde-lsg

Requires: systemd-container

%define _source_payload w0.ufdio
%define _binary_payload w0.ufdio

%define __os_install_post %{nil}
%description
利用 systemd-nspawn 在其它 Linux 发行版（包括非 Debian 发行版）运行 Debian + GXDE 桌面环境

%prep
%build
cd /tmp/gxde-lsg-source
make -j$(nproc)
%install
cd /tmp/gxde-lsg-source
mkdir -pv build-resources/opt/apps/top.gxde.gxde-lsg/files/
cp build-resources/* ~/rpmbuild/BUILD/*/BUILDROOT/ -rv
cp gxde-lsg ~/rpmbuild/BUILD/*/BUILDROOT/opt/apps/top.gxde.gxde-lsg/files/
cp gxde-rootfs.tar.xz ~/rpmbuild/BUILD/*/BUILDROOT/opt/apps/top.gxde.gxde-lsg/files/

#fakeroot chown root:root ~/rpmbuild/BUILDROOT/*/usr -Rv
#%dir %attr(0755, root, root) "/usr"

%files
/usr/
/opt/

