#include "rvt_provisioning_service.h"

#include <unistd.h>

/*
 * 函数描述：stop_provisioning 命令入口，停止当前配网流程并关闭二维码。
 * 入参：
 *   argc - 命令行参数数量，当前未使用。
 *   argv - 命令行参数数组，当前未使用。
 * 返回值：成功返回 0；停止失败返回负 errno。实际会通过 _exit 设置退出码。
 */
int main(int argc, char *argv[])
{
    int ret;

    (void)argc;
    (void)argv;
    ret = rvt_provisioning_stop();
    _exit(ret < 0 ? 1 : 0);
    return ret;
}
