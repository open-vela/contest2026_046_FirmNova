# Rivotek WiFi Display 移植说明

本目录按“公共业务 + 平台 port”的方式组织。目标是同一套 `wifi_display_service`/`map_service` 公共代码在目标车机平台复用，平台差异只放在构建宏和 `port/*` 目录中。

## 公共目录

- `include/`
  - `rvt_wifi_display_api.h`: 对外投屏 API。
  - `rvt_wifi_display_service.h`: TCP/UDP/discovery 内部接口。
  - `rvt_wifi_display_osal.h`: 线程、互斥锁、定时器、socket close、socket 非阻塞等 OSAL 抽象。
  - `rvt_display_port.h`: 平台解码/渲染抽象。
  - `rvt_wifi_display_network_control.h`: 平台 WiFi STA/SAP 控制抽象。
- `src/`
  - `rvt_tcp_channel_impl.c`: TCP 控制通道公共实现，包含连接后命令收发、心跳 ACK、手机端地图/导航回调解析；该文件不直接加入构建。
  - `rvt_tcp_server.c`: TCP server 角色入口，只在 `RVT_TCP_ROLE_CLIENT=0` 时加入构建。
  - `rvt_tcp_client.c`: TCP client 角色入口，只在 `RVT_TCP_ROLE_CLIENT=1` 时加入构建。
  - `rvt_udp_receiver.c`: UDP 自定义包接收、乱序组帧、JPEG/H264 RAW 轻量校验、完整帧提交。
  - `rvt_discovery_server.c`: UDP discovery 服务。
  - `rvt_wifi_display_api.c`: 对外 start/stop/参数设置接口。

公共 `src/` 不应直接依赖 RT-Thread、NuttX/openVela、具体解码器或具体显示驱动。

## 平台目录

- `port/openvela_allwinner_r528/`
  - R528 openVela OSAL。
  - R528 WiFi STA 控制。
  - R528 V4L2/CedarC/libuapi 解码渲染适配框架。
- `port/stub/`
  - 无平台适配时的空实现，只保证公共业务可编译，解码/渲染能力为 0。

新增平台时建议只新增 `port/<os>_<vendor>_<chip>/`，不要修改公共 `src/`。

## TCP 角色

- R528: Android APP 作为 TCP server，R528 车机代码作为 TCP client，主动连接手机 `6004`，避开 R528 NuttX/openVela 上 TCP server 阻塞 `accept()` 风险。
- 手机开启 WiFi 热点只表示链路形态，不等于 TCP 角色。R528 当前需要 APP server/车机 client。

## 关键宏

- `RVT_TCP_ROLE_CLIENT`
  - 默认 0，车机作为 TCP server。
  - R528/openVela 置 1，车机作为 TCP client。
- `RVT_SOCKET_AVOID_POLL`
  - 默认跟随 `RVT_TCP_ACCEPT_NONBLOCK`。
  - R528/openVela 置 1，TCP 接收和 discovery 避免 socket `select/poll`，用 `SO_RCVTIMEO` 超时接收。
- `RVT_TCP_CLIENT_ASYNC_CONNECT`
  - R528/openVela 当前置 1，用单独线程执行阻塞 `connect()`，主 TCP 线程轮询连接状态。
  - 这是当前 R528 已验证更稳定的建联方式；不要改回非阻塞 `connect()` 作为默认路径。
- `RVT_TCP_CLIENT_NONBLOCK_CONNECT`
  - 默认 0，仅保留为后续实验开关。
  - 之前在 R528 上打开后出现“不发起连接/建联不稳定”的现象，不作为主线默认配置。
- `RVT_TCP_ACCEPT_NONBLOCK`
  - R528 不走 TCP server 路径，当前不依赖该宏。
- `RVT_WIFI_DISPLAY_RX_H264_RAW`
  - 默认 0，UDP 完整帧按 JPEG/MJPEG 头尾做轻量校验。
  - R528 CedarC-H264 调试时置 1，UDP 完整帧按 Annex-B 起始码做轻量校验。这个校验只判断 `00 00 01` 或 `00 00 00 01`，不能区分 H264/H265。
  - 若 APP 发送 H264/H265 Annex-B，而 R528 日志出现 `discard invalid frame ... h264=0 first2=0000`，通常表示 UDP 已收到并组出完整帧，但当前镜像仍在按 JPEG/MJPEG 校验，不应优先怀疑收包链路。
  - MJPEG 路径保留，后续拿到支持 MJPEG 插件的 CedarC 库后可继续恢复验证。
- `RVT_WIFI_DISPLAY_MAX_FRAME_SIZE` / `RVT_WIFI_DISPLAY_UDP_RECV_BUF_SIZE`
  - 公共默认 256KB。
  - R528/openVela 构建中置 512KB，用于容纳 H264 IDR 大帧和较大的 UDP 接收缓冲。

## R528 当前结论

- TCP 控制通道已经验证到心跳/ACK 连续稳定交互，R528 侧角色为 TCP client。
- UDP 接收路径已经具备自定义包头解析、乱序组帧、完整帧提交能力；APP 侧 H264/H265 `sendCustom(data, len)` 的私有协议数据可进入车机 UDP 接收逻辑。
- 当前 `scooterdemo_exbt` 主线默认只打开 `CONFIG_RIVOTEK_WIFI_DISPLAY=y`，不打开 `CONFIG_RIVOTEK_WIFI_DISPLAY_R528_CEDARC_MJPEG`：
  - 运行日志应表现为 `codec=0x0 v4l2=0 cedarc=0` 和 `no usable decoder, render disabled`。
  - 这种镜像不会初始化 CedarC，也不会真正解码 H264/H265/MJPEG。
  - 因为 `RVT_WIFI_DISPLAY_RX_H264_RAW` 默认是 0，UDP 完整帧仍按 JPEG/MJPEG 校验；APP 发 H265 时看到 `discard invalid frame ... h264=0 first2=0000` 是当前配置下的预期现象，只能说明码流格式和当前校验模式不一致。
- 解码/渲染暂时阻塞，不是 TCP/UDP 问题：
  - 现有 CedarC 库未提供可用 MJPEG 插件。
  - H264/H265 也阻塞或失败在 `InitializeVideoDecoder()` / VE 初始化链路。
  - 替换单个 `libcedarc.a` 后出现过 `VideoEngineCreate -> VeInitialize -> pthread_mutex_timedlock` 等崩溃，说明还缺匹配的 `multimedia/libcedarc.a`、VE HAL、OSAL、display/libuapi 配套。
- `scooterdemo_exbt` 当前只默认打开 `CONFIG_RIVOTEK_WIFI_DISPLAY=y`，不默认打开 `CONFIG_RIVOTEK_WIFI_DISPLAY_R528_CEDARC_MJPEG`。在解码库配套未确认前，不应让开机或 `start_wifi_display` 默认进入 CedarC 解码。
- `components/multimedia` 已接入 Kconfig，只有打开 R528 CedarC 配置时才应参与链接；不能把“能看到 multimedia 目录”理解为“当前解码能力已经可用”。

## R528 解码/渲染后续

当前保留过的 R528 解码调试路径有三类，需要区分使用：

- 默认主线：不启用 CedarC，车机只验证 TCP/UDP/discovery，视频帧不会解码。APP 发送 H264/H265 时，R528 侧 `discard invalid frame ... h264=0` 不代表 UDP 异常。
- CedarC-H264 调试：打开 `CONFIG_RIVOTEK_WIFI_DISPLAY_R528_CEDARC_MJPEG=y` 后，当前 Makefile 会定义 `RVT_R528_HAS_CEDARC_H264=1` 和 `RVT_WIFI_DISPLAY_RX_H264_RAW=1`；虽然 Kconfig 名称里仍带 `MJPEG`，实际 CedarC 调试主路径会按 `VIDEO_CODEC_FORMAT_H264` 初始化，并向手机声明 H264 能力。
- H265 初始化探针：`win_share/R528_20260528-201337_cedarc_h265_debug_probe.diff` 不是完整 H265 解码/渲染实现。它新增 `RVT_R528_CEDARC_H265_INIT_PROBE=1` 和 `r528_h265_init_probe` 命令，参考 `awh265dec.c` 只验证 `VIDEO_CODEC_FORMAT_H265` 的 `InitializeVideoDecoder()` 是否能返回；探针结束后会关闭 decoder，不会继续喂 H265 码流，也不会渲染画面。

恢复 R528 解码调试前，先确认以下条件：

1. `components/multimedia/lib/libcedarc/libcedarc.a` 与当前 R528 openVela、VE HAL、OSAL ABI 匹配。
2. H264/H265 插件注册不再打印 `register h264/h265 decoder failure`。
3. `InitializeVideoDecoder()` 能正常返回。
4. `CreateVideoOutport(0)`、`DestroyVideoOutport()`、`writeData()` 等 libuapi 显示接口和目标 MIPI 7 寸屏对应关系确认无误。

确认后再打开 `CONFIG_RIVOTEK_WIFI_DISPLAY_R528_CEDARC_MJPEG=y`，并按需要打入 `win_share` 下保存的 CedarC/VE 调试 patch。

## mcpserver / NuttX 网络栈

投屏主链路已经通过 R528 TCP client 和 `RVT_SOCKET_AVOID_POLL` 绕开 NuttX TCP server `accept()` 与 socket poll 风险，因此主线不需要修改 `mcpserver` 或 NuttX `net/*`。

如果后续其他服务仍需要在 R528 上作为 TCP server，或再次遇到 NuttX socket poll/recvfrom stale callback 崩溃，可从 `win_share` 中的独立调试 patch 回打验证，不要直接混入投屏主线。

## 移植步骤

1. 将 `wifi_display_service`、`map_service`、`rivotek/include` 复制到目标工程。
2. 在目标工程构建系统中加入公共 `src/rvt_discovery_server.c`、`src/rvt_udp_receiver.c`、`src/rvt_wifi_display_api.c`。
3. 根据 TCP 角色只选择一个入口加入编译：server 角色加入 `src/rvt_tcp_server.c`，client 角色加入 `src/rvt_tcp_client.c`；不要直接编译 `src/rvt_tcp_channel_impl.c`。
4. 根据平台选择一个 `port/*` 目录加入编译。
5. 如果没有解码/渲染适配，先编译 `port/stub` 或让平台 port 返回 `codec_mask=0`，此时投屏控制通道可用，但手机端不应发送视频码流。
6. 在目标平台实现：
   - `rvt_wifi_display_osal.c`
   - `rvt_wifi_display_network_control.c`
   - `rvt_display_port_*`
7. 若目标系统有 shell/CLI，将 `rvt_wifi_display_cmd_*`、`rvt_map_cmd_*` 包装成目标系统命令；不要把 CLI 逻辑写进公共业务。

## R528 状态

- R528 openVela:
  - TCP/UDP/discovery 已接入。
  - TCP 角色保持车机 client、Android APP server。
  - 调试时，先打开boards/r528/r528s3-gemini-s1/configs/scooterdemo_exbt/defconfig 中的CONFIG_RIVOTEK_WIFI_DISPLAY
  - 解码/渲染代码已封装在 R528 port 中，但默认不启用 CedarC，等待匹配 multimedia/VE/OSAL 配套后继续调试。
