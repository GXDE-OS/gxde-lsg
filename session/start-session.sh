#!/bin/sh
# Linux Subsystem for GXDE 沉浸式登录会话
# 安装为 /opt/apps/top.gxde.gxde-lsg/files/gxde-lsg-session，由显示管理器通过
# /usr/share/wayland-sessions/gxde-lsg.desktop 启动。
#
# 宿主机上的 FlakeWM 以 --gxde-lsg-immersive 运行（不响应任何快捷键），
# 其中只运行 LSG；容器内嵌套的 FlakeWM 通过 dde_shell 请求去掉标题栏和边框并全屏。
# FlakeWM 需由宿主机自行安装，这里不做检查。

APP_DIR=/opt/apps/top.gxde.gxde-lsg/files

if [ "$1" = --in-flakewm ]; then
	# 已在宿主机 FlakeWM 内。会话里没有终端，sudo 通过 askpass 弹出图形密码框
	# （zenity 或 kdialog）。取消或三次输错时 sudo 失败，会话结束，回到显示管理器。
	# WAYLAND_DISPLAY 由 FlakeWM 设置，sudo 会清掉它，用参数传入；
	# 语言变量显式保留，容器内桌面沿用宿主机语言
	export SUDO_ASKPASS="$APP_DIR/gxde-lsg-askpass"
	exec sudo -A -p "启动 GXDE 兼容层需要 %p 的密码：" --preserve-env=LANG,LANGUAGE \
		"$APP_DIR/gxde-lsg" --immersive --wayland-display="$WAYLAND_DISPLAY"
fi

export XDG_SESSION_TYPE=wayland
# gxde-lsg 退出（容器内注销）时 FlakeWM 随之退出，回到显示管理器。
# FlakeWM 把会话进程的输出丢弃，改写到 journal（journalctl -t gxde-lsg-session）
exec flakewm --gxde-lsg-immersive -s "systemd-cat -t gxde-lsg-session $APP_DIR/gxde-lsg-session --in-flakewm"
