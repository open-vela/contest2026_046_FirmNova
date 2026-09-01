#ifndef __RVT_PROVISIONING_QRCODE_BRIDGE_H
#define __RVT_PROVISIONING_QRCODE_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 函数描述：注册 provisioning_service 到 qrcode_service 的事件桥接。
 * 入参：无。
 * 返回值：成功返回 0；注册失败返回负 errno。
 */
int rvt_provisioning_qrcode_bridge_init(void);

#ifdef __cplusplus
}
#endif

#endif
