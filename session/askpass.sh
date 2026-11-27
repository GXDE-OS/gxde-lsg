#!/bin/sh
# sudo -A 的图形密码框（SUDO_ASKPASS），安装为
# /opt/apps/top.gxde.gxde-lsg/files/gxde-lsg-askpass，供沉浸式登录会话使用：
# 会话里没有终端，也不一定有 polkit 认证代理。
#
# sudo 把提示语作为第一个参数传入，从标准输出读取密码；
# 用户取消时不输出、返回非 0，sudo 随之失败。

TITLE="Linux Subsystem for GXDE"

if command -v zenity >/dev/null 2>&1; then
	exec zenity --password --title="$TITLE"
fi
if command -v kdialog >/dev/null 2>&1; then
	exec kdialog --title "$TITLE" --password "${1:-Password:}"
fi
echo "gxde-lsg-askpass: neither zenity nor kdialog is installed, cannot ask for the password" >&2
exit 1
