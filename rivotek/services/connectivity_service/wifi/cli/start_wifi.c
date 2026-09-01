#include "rvt_wifi_autoconnect.h"

#include <errno.h>
#include <unistd.h>

/*
 * 函数描述：start_wifi 命令入口，触发保存 WiFi 配置的 STA 回连流程。
 * 入参：
 *   argc - 命令参数数量，当前未使用。
 *   argv - 命令参数列表，当前未使用。
 * 返回值：成功返回 0；无可用配置退出码为 2；其他失败退出码为 1。
 */
int main(int argc, char *argv[])
{
    int ret;

    (void)argc;
    (void)argv;

    ret = rvt_wifi_autoconnect_start();
    if (ret == -ENOENT || ret == -ENODATA) {
        _exit(2);
    }
    _exit(ret < 0 ? 1 : 0);
    return ret;
}
