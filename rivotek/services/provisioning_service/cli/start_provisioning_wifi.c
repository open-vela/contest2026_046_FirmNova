#include "rvt_provisioning_service.h"

#include <errno.h>
#include <stdio.h>
#include <unistd.h>

#define PROV_LOG(...) dprintf(STDOUT_FILENO, __VA_ARGS__)

/*
 * 函数描述：按 NuttX 命令习惯把业务返回值转换成进程退出码。
 * 入参：
 *   ret - 业务函数返回值。
 * 返回值：理论上返回原始 ret；实际会先调用 _exit 退出。
 */
static int provisioning_cli_exit(int ret)
{
    _exit(ret < 0 ? 1 : 0);
    return ret;
}

/*
 * 函数描述：start_provisioning_wifi 命令入口，启动当前车机 SoftAP WiFi 配网主流程。
 * 入参：
 *   argc - 命令行参数数量，必须为 1。
 *   argv - 命令行参数数组，当前未使用。
 * 返回值：成功返回 0；参数非法或启动失败返回负 errno。实际会通过 _exit 设置退出码。
 */
int main(int argc, char *argv[])
{
    int ret;

    (void)argv;

    if (argc != 1) {
        PROV_LOG("usage: start_provisioning_wifi\n");
        return provisioning_cli_exit(-EINVAL);
    }

    ret = rvt_provisioning_start(RVT_PROVISIONING_TYPE_WIFI);
    return provisioning_cli_exit(ret);
}
