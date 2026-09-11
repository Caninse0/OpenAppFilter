#!/bin/sh
# diag-oaf.sh - OpenAppFilter (OAF) 在 OpenWrt / ImmortalWrt 上的安装与启动诊断
#
# 用法：
#   1) 把本文件传到路由器：  scp diag-oaf.sh root@<router>:/tmp/
#   2) 运行：                sh /tmp/diag-oaf.sh > /tmp/oaf-diag.txt 2>&1
#   3) 把 /tmp/oaf-diag.txt 的内容贴回来
#
# 脚本是只读诊断，唯一有副作用的动作是「尝试加载 oaf 模块」和「尝试启动服务」，
# 这两步正是你要排查的现象本身。如不希望尝试，先执行： TRY_LOAD=0 TRY_START=0 sh /tmp/diag-oaf.sh
#
# 注意：请在「装好 OAF 包、但服务起不来 / 模块加载不了」的那台设备上运行。

TRY_LOAD=${TRY_LOAD:-1}
TRY_START=${TRY_START:-1}

if [ -t 1 ]; then
	echo "提示：建议重定向到文件，例如 sh /tmp/diag-oaf.sh > /tmp/oaf-diag.txt 2>&1"
fi

sec() {
	echo ""
	echo "==================== $1 ===================="
}

run() {
	echo "--- \$ $*"
	"$@" 2>&1
	echo "(exit=$?)"
}

echo "OAF 诊断报告"
echo "生成时间: $(date 2>/dev/null)"
echo "设备: $(uname -a 2>/dev/null)"

########################################################################
sec "1. 系统与版本（决定 kmod 必须匹配的内核）"
########################################################################
for f in /etc/os-release /etc/openwrt_release /etc/immortalwrt_release; do
	if [ -f "$f" ]; then
		echo "--- $f"
		cat "$f" 2>&1
	fi
done
echo "--- uname -r (运行中的内核版本，kmod 的 vermagic 必须与它一致)"
	uname -r 2>&1
echo "--- /proc/version"
	cat /proc/version 2>&1
if command -v ubus >/dev/null 2>&1; then
	echo "--- ubus call system board"
	ubus call system board 2>&1
fi

########################################################################
sec "2. 包管理器与已安装的 OAF 相关包"
########################################################################
if command -v apk >/dev/null 2>&1; then
	echo "包管理器: apk"
	echo "--- apk info -e appfilter"
	apk info -e appfilter 2>&1
	echo "--- 已安装的 oaf 相关包"
	apk list -I 2>/dev/null | grep -i oaf 2>&1
	echo "--- kmod-oaf 的文件清单（关键：必须包含 oaf.ko）"
	apk info -L kmod-oaf 2>&1
	apk info -L appfilter 2>&1
	apk info -L luci-app-oaf 2>&1
elif command -v opkg >/dev/null 2>&1; then
	echo "包管理器: opkg"
	echo "--- 已安装的 oaf 相关包"
	opkg list-installed 2>/dev/null | grep -i oaf 2>&1
	opkg files kmod-oaf 2>&1
	opkg files appfilter 2>&1
else
	echo "未找到 apk 或 opkg"
fi

########################################################################
sec "3. 关键文件是否就位"
########################################################################
for p in \
	/etc/init.d/appfilter \
	/usr/bin/oafd \
	/etc/appfilter \
	/etc/appfilter/feature.cfg \
	/etc/appfilter/feature_cn.cfg \
	/etc/config/appfilter \
	/etc/config/user_info \
	/etc/config/fwx \
	/usr/bin/oaf_rule \
	/usr/bin/rule_manager \
	/usr/bin/hnat.sh \
	/etc/oaf_version \
	/tmp/feature.cfg
do
	if [ -e "$p" ]; then
		echo "[OK ] $p"
		ls -l "$p" 2>&1 | sed 's/^/       /'
	else
		echo "[缺失] $p"
	fi
done

echo "--- /etc/modules.d/ 中的 oaf 自动加载项"
ls -l /etc/modules.d/ 2>&1 | grep -i oaf 2>&1
cat /etc/modules.d/*oaf* 2>&1

echo "--- /etc/rc.d/ 中的 appfilter 开机启动链接"
ls -l /etc/rc.d/ 2>&1 | grep -i appfilter 2>&1

echo "--- 已安装的 oaf.ko 位置"
find /lib/modules -name 'oaf*.ko*' 2>&1

########################################################################
sec "4. oafd 可执行文件与动态库依赖"
########################################################################
if [ -f /usr/bin/oafd ]; then
	echo "--- file /usr/bin/oafd"
	file /usr/bin/oafd 2>&1
	echo "--- ldd /usr/bin/oafd   (出现 'not found' 就是起不来的直接原因)"
	ldd /usr/bin/oafd 2>&1
	if command -v opkg >/dev/null 2>&1; then
		opkg whatdepends /usr/bin/oafd 2>&1 | head -20
	fi
else
	echo "--- /usr/bin/oafd 不存在：appfilter 包没有被正确安装"
fi

########################################################################
sec "5. 内核模块能否加载"
########################################################################
if command -v lsmod >/dev/null 2>&1; then
	echo "--- lsmod 中的 oaf"
	lsmod 2>&1 | grep -i oaf 2>&1
fi

KO=$(find /lib/modules -name 'oaf*.ko*' 2>/dev/null | head -n1)
if [ -n "$KO" ]; then
	echo "--- modinfo $KO"
	modinfo "$KO" 2>&1
	echo "--- 注意对比 modinfo 的 vermagic 与上面的 uname -r"
else
	echo "--- 没有找到 oaf.ko：kmod-oaf 包内容为空或未安装"
fi

echo "--- ls -l /lib/modules/\$(uname -r)/ 中 oaf 相关"
ls -l "/lib/modules/$(uname -r)/" 2>&1 | grep -i oaf 2>&1

if [ "$TRY_LOAD" = "1" ]; then
	echo "--- 尝试 modprobe oaf"
	modprobe oaf 2>&1
	echo "(exit=$?)"
	if [ -n "$KO" ] && ! lsmod 2>/dev/null | grep -q '^oaf'; then
		echo "--- 尝试 insmod $KO（可直接看到 vermagic / Unknown symbol 报错）"
		insmod "$KO" 2>&1
		echo "(exit=$?)"
	fi
	echo "--- 模块相关内核日志（最后 40 行）"
	dmesg 2>/dev/null | grep -iE 'oaf|vermagic|Unknown symbol|invalid module|disagrees about version' | tail -n 40 2>&1
fi

########################################################################
sec "6. proc 接口是否存在"
########################################################################
for d in /proc/sys/oaf /proc/sys/fwx; do
	if [ -d "$d" ]; then
		echo "[存在] $d"
		ls -l "$d" 2>&1 | sed 's/^/       /'
	else
		echo "[不存在] $d"
	fi
done

########################################################################
sec "7. 服务状态与启动日志"
########################################################################
if [ -f /etc/init.d/appfilter ]; then
	echo "--- /etc/init.d/appfilter enabled?"
	/etc/init.d/appfilter enabled 2>&1
	echo "(exit=$?)"

	if [ "$TRY_START" = "1" ]; then
		echo "--- /etc/init.d/appfilter restart"
		/etc/init.d/appfilter restart 2>&1
		echo "(exit=$?)"
		sleep 3
		echo "--- procd 视角的实例状态"
		if command -v ubus >/dev/null 2>&1; then
			ubus call service list "{'name':'appfilter'}" 2>&1
			ubus call service list 2>&1 | grep -i appfilter 2>&1
		fi
		echo "--- oafd 进程"
		ps w 2>/dev/null | grep -iE 'oafd|oaf_rule|rule_manager' | grep -v grep 2>&1
	fi
else
	echo "--- /etc/init.d/appfilter 不存在：appfilter 包没有装进去"
fi

echo "--- logread 中 appfilter / oafd 相关（最后 60 行）"
logread 2>/dev/null | grep -iE 'appfilter|oafd|oaf' | tail -n 60 2>&1

echo "--- oafd 自己的日志"
tail -n 60 /tmp/log/fwxd.log 2>&1

########################################################################
sec "8. 结论速查（人工比对）"
########################################################################
cat <<'EOF'
对照下面几点即可定位：

A. 报告 3/4 里 /usr/bin/oafd 或 /etc/init.d/appfilter 缺失
   -> 包没装上。多为 apk/opkg 因 kmod-oaf 的内核依赖不满足而整批回滚。

B. 报告 5 里 insmod 报 "version magic '6.12.xx ...' should be '6.12.yy ...'"
   -> kmod 与设备内核版本不一致。必须用与设备完全同版本同 target 的 SDK 重新编译。

C. 报告 5 里报 "Unknown symbol ..."
   -> 内核 config 不一致（例如 nf_conntrack accounting 未开），同样是 SDK 与固件不匹配。

D. 报告 3 里 /lib/modules/<ver>/oaf.ko 不存在，但包已安装
   -> kmod-oaf 包内容为空（打包环节丢了 .ko）。

E. 报告 4 里 ldd 出现 "not found"
   -> 缺运行时库，补齐对应包（libsqlite3 / libcurl / libuci-lua 等）。

F. 报告 6 里 /proc/sys/oaf 不存在但模块已 lsmod 成功
   -> 内核模块注册的 proc 目录名与 oafd 期望的不一致。

G. 报告 7 里 ubus call service list 显示 appfilter 实例反复 respawn 后停止
   -> 看 oafd 日志的第一条错误，通常是 ubus 连接失败或配置缺失。
EOF
