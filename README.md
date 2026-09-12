

## Introduction
OAF is a parental control software based on OpenWrt. It supports popular applications across gaming, video streaming, instant messaging, such as TikTok, YouTube, Facebook. Currently, it supports hundreds of different applications.   
For a detailed introduction, please visit [www.openappfilter.com](http://www.openappfilter.com).

## Features
- DPI-based protocol identification: Supports Layer 7 protocol parsing and HTTPS domain resolution, and operates independently of DNS.
- Industry-standard architecture:  Flow-based identification for high efficiency, with extremely low hardware requirements.
- Supports custom protocol signatures: Offers a high degree of flexibility and customization.
- Supports installation as a plugin on OpenWrt systems: Compatible with all OpenWrt-enabled devices.You can download the plugin package corresponding to your architecture from the releases page.

## 本分支新增内容（相对于上游 destan19/OpenAppFilter）

本分支在上游基础上新增了**自定义规则**、**GitHub Actions 云端编译**，并修复了 **OpenWrt 25.12 SDK 下 Makefile 不兼容**的问题。

### 1. 自定义规则（AdGuard Home 语法）

在应用特征库之外可以自己写规则精确控制域名访问。入口：**服务 → OpenAppFilter → 自定义规则**。
规则保存在 `/etc/appfilter/custom_rules.txt`，保存后几秒内生效，无需更新特征库。

支持的语法：

```
||example.org^           阻止 example.org 及其所有子域名，域名中可用 * 通配
@@||sub.example.org^     放行（例外），优先级高于阻止规则
/ads\d+/                 用正则表达式匹配域名
! 注释 / # 注释           注释行
```

规则约束（内核特征行解析的限制，违反的规则会被跳过并记入日志）：

- 规则中不能包含 `#`、`;`、`,`、`[`、`]`
- 一条规则转换后的正则最长 124 字节（内核 `host_url` 缓冲为 128 字节）
- 匹配顺序：自定义规则优先于特征库；放行规则优先于阻止规则；同组内按规则文件中的顺序
- 自定义规则使用固定 appid `30001`，走与特征库完全相同的 DPI 路径，因此同样不依赖 DNS

三个开关（UCI：`fwx.global.custom_rule_enable` / `custom_rule_follow_appfilter` / `custom_rule_max_num`）：

| 开关 | 说明 |
|---|---|
| 启用自定义规则 | 关闭后全部自定义规则失效 |
| 跟随应用过滤开关 | 开启：自定义规则随应用过滤开关一起启停。关闭：自定义规则独立运行，应用过滤关闭时内核进入「仅自定义规则」模式——挂起特征库规则，自定义规则继续生效 |
| 规则上限 | 页面自动计算，无需手填 |

规则上限按 `floor((磁盘剩余可用字节数 - 规则文件占用字节数) / 80)` 计算（十进制单位，1 MB = 1000 KB），并**夹到 2000 条**。
夹上限是必须的：oafd 会按这个值 `calloc(上限, 264 字节)` 并向内核逐条下发特征，而每条域名规则会展开成 2 条内核特征、匹配时逐包做线性正则，上限到百万级会直接分配失败、自定义规则完全不加载。
计算出的上限发生变化时，页面下方的「保存」等价于常规 LuCI 的「保存并应用」。

### 2. OpenWrt 25.12 SDK 兼容

重写 5 个 Makefile，使其能在 OpenWrt 25.12 SDK 下编译：

- `oaf/Makefile`：补 `PKG_VERSION`、改用 `KERNEL_BUILD_DIR`、新增 `Build/Prepare`、补 `AUTOLOAD`
- `open-app-filter/Makefile`：改用 `TARGET_CC` / `CPPFLAGS` / `LDFLAGS` + `LDLIBS`
- `oaf/src/Makefile`、`open-app-filter/src/Makefile`：改为显式 `$(EXEC): $(OBJS)` 与 `%.o: %.c` 规则，不再依赖 make 隐式规则
- `luci-app-oaf/Makefile`：补 `+luci-base` 依赖与 license 声明
- 新增 `.gitattributes`（`* text=auto eol=lf`），避免 CRLF 混进构建脚本

### 3. GitHub Actions 云端编译

两条手动触发的流水线，不需要本地准备 OpenWrt 源码：

| 流水线 | 说明 |
|---|---|
| `.github/workflows/build-with-openwrt-sdk.yml` | 用 OpenWrt 官方 SDK 编译三个包 |
| `.github/workflows/build-with-immortalwrt-sdk.yml` | 用 ImmortalWrt 官方 SDK 编译（25.12 起产物是 `.apk`） |

用法：**Actions → 选择流水线 → Run workflow → 选版本与 target（如 `rockchip/armv8`）→ 运行结束后下载 artifacts**。
`openwrt_version` / `immortalwrt_version` 必须与设备固件版本一致，详见下面的注意事项。

流水线会：把当前 checkout 作为 `src-link` feed 挂进去（编译的确实是本仓库代码）、编译三个包、断言 `kmod-oaf` / `appfilter` / `luci-app-oaf` 三个产物都存在、检查 `oaf.ko` 真的被打进了 kmod 包、并把 SDK 的内核版本与 vermagic 写进产物里的 `build_info.txt`。

设备侧还提供了诊断脚本 `tools/diag-oaf.sh`：装好包却服务起不来 / 模块加载不了时，把它拷到路由器执行、把输出贴回来即可定位。

```sh
scp tools/diag-oaf.sh root@<router>:/tmp/
ssh root@<router> 'sh /tmp/diag-oaf.sh > /tmp/oaf-diag.txt 2>&1'
```

## 注意事项（本分支）

1. **kmod 必须与设备内核完全匹配。** `kmod-oaf` 打包时硬绑了内核版本与编译指纹（vermagic）。ImmortalWrt 每个小版本的内核都不同，例如 rockchip/armv8：25.12.1 → 6.12.94、25.12.2 → 6.12.103。构建前先在设备上执行 `uname -r`，并选用同版本、同 target 的 SDK。
2. **不要与上游旧版 OAF 混装。** 本分支的内核接口是 `/proc/sys/fwx`、`/etc/config/fwx`、`/etc/fwxd/feature.bin`；上游旧版（含 ImmortalWrt packages feed 里自带的 `net/open-app-filter`）是 `/proc/sys/oaf`、`/etc/config/appfilter`、`/tmp/feature.cfg`。混装会出现「服务看着在跑、引擎却是旧版」。
3. **ImmortalWrt 的 packages feed 自带同名包。** `immortalwrt/packages` 中有 `net/open-app-filter`（上游旧代码），其包名与安装路径与本仓库完全重名，不处理的话会编出「feed 的旧版 kmod/appfilter + 本仓库的新版 luci-app-oaf」。immortalwrt 流水线已自动剔除该 feed 包并强制使用本仓库源码；手动编译时请自行处理。
4. 产物来自自建流水线、未走官方签名，设备上安装需要加 `--allow-untrusted`；如果设备上已装有同名的旧版包，还需加 `--force-downgrade`（apk）或 `--force-reinstall`（opkg），装完建议重启一次以让新 kmod 重新注册 proc 目录。

## How to Compile
1. Prepare a set of OpenWrt source code that has already been successfully compiled into firmware.
(Instructions for compiling OpenWrt source code can be found via independent tutorials and will not be covered here.)
2. Clone the OAF source code.
Navigate to the root directory of your OpenWrt source code and execute the following command:
```
git clone https://github.com/destan19/OpenAppFilter.git package/OpenAppFilter
```
3. Enable the OAF compilation options.
Application Filtering consists of three distinct source packages, corresponding to the LuCI App, the service daemon, and the kernel module.
Before compiling, you must enable the build options for these three packages. You can do this by selecting `luci-app-oaf` via the `make menuconfig` graphical interface.
Alternatively, you can enable them by executing the following commands (run from the source code root directory):
```
echo "CONFIG_PACKAGE_luci-app-oaf=y" >>.config
make defconfig
```
This will automatically enable the compilation options for all three modules.

4. Begin compiling OAF.
If you have previously successfully compiled your OpenWrt source code, you can choose to compile only the individual packages:
```
make package/luci-app-oaf/compile V=s
make package/open-app-filter/compile V=s
make package/oaf/compile V=s
```
Alternatively, you can recompile the entire firmware image; this will integrate the plug-in directly into the firmware build:
```
make V=s
```

## Discussion Group

[https://t.me/openappfilter](https://t.me/openappfilter) (Telegram)

If you encounter some issues during installation or usage, you can join the group for discussion(The group was created only recently).

## License
- Individuals can use this software completely free of charge, and are also permitted to develop upon and redistribute it.
- If you undertake derivative development based on OAF, you must adhere to the GPL 2.0 license and retain references to the OAF repository or website information.
- If a company wishes to use this software, please contact the author for authorization.

## Star
If you find this project helpful, please give it a star.  
[![Stargazers over time](https://starchart.cc/destan19/OpenAppFilter.svg?variant=adaptive)](https://starchart.cc/destan19/OpenAppFilter)

