# NOTE

## rootfs tarball 解压会丢失 file capability

### 现状

找不到 `gxde-rootfs/etc/hostname` 时，`gxde-lsg` 会调用 `Utils::unpackTar()`（`utils.cpp`），把 `gxde-rootfs.tar.xz` 解压到 `gxde-rootfs/`。实际执行的是：

```bash
tar -xf gxde-rootfs.tar.xz -C gxde-rootfs
```

### 问题

`gxde-lsg` 以 root 运行，GNU tar 默认会保留属主和 setuid/setgid 位，但**不会**解出扩展属性（xattr）。file capability 存在 `security.capability` 这个 xattr 里，所以解压后全部丢失。

受影响的程序（原始 rootfs 中 `getcap -r /usr` 的结果）：

| 程序 | capability | 丢失后的表现 |
| --- | --- | --- |
| `/usr/bin/gxde-dock` | `cap_ipc_lock=eip` | 能启动，但无法锁定内存 |
| `/usr/bin/deepin-system-monitor` | `cap_net_raw=ep` | 无法按进程统计网络流量 |
| `/usr/bin/deepin-kwin_wayland` | `cap_sys_resource=ep` | 无法调整资源限制 |
| `.../gstreamer-1.0/gst-ptp-helper` | `cap_net_bind_service,cap_net_admin,cap_sys_nice=ep` | GStreamer PTP 时钟不可用 |

另外，tar 默认按**名字**恢复属主：用宿主机的 `/etc/passwd`、`/etc/group` 查找 tarball 里记录的用户名/组名。宿主机（如 Arch）与 rootfs（Debian）的系统账户 id 不一致时，rootfs 内文件的 uid/gid 会被映射成宿主机上的值，与 rootfs 自己的 `/etc/passwd` 对不上。

> 注：带 capability 的程序要在容器内正常 exec，capability 必须在 nspawn 的 bounding set 内。`gxde-dock` 需要的 `CAP_IPC_LOCK` 已通过 `--capability=CAP_IPC_LOCK` 加上；`CAP_NET_ADMIN` 因容器共享宿主机网络命名空间而**有意不授予**。

### 修复方案

**解压**（`Utils::unpackTar()`）：

```bash
tar -xpf gxde-rootfs.tar.xz -C gxde-rootfs \
    --xattrs --xattrs-include='*' --acls --numeric-owner
```

- `--xattrs --xattrs-include='*'`：恢复包括 `security.capability` 在内的所有 xattr（GNU tar 默认只恢复 `user.*` 命名空间）
- `--acls`：恢复 POSIX ACL
- `--numeric-owner`：按 uid/gid 数值恢复属主，不经宿主机的用户名映射
- `-p`：显式保留权限位（root 下是默认行为，写上更明确）

**打包**（制作 `gxde-rootfs.tar.xz` 时），两边参数要对应，否则 tarball 里本来就没有 xattr：

```bash
tar -cJpf gxde-rootfs.tar.xz -C /path/to/rootfs \
    --xattrs --xattrs-include='*' --acls --numeric-owner .
```

### 验证

```bash
# 解压后检查 capability 是否还在
getcap -r gxde-rootfs/usr 2>/dev/null
# 预期至少包含：
#   gxde-rootfs/usr/bin/gxde-dock cap_ipc_lock=eip

# 检查 setuid 程序
find gxde-rootfs/usr -perm -4000 -type f
```
