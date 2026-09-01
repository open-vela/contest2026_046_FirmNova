#include <nuttx/config.h>

#include <stdio.h>

#ifdef CONFIG_IEEE80211_REALTEK_WIFI
int realtek_wlan_bringup(void);
#endif

int main(int argc, char *argv[])
{
#ifdef CONFIG_IEEE80211_REALTEK_WIFI
    int ret;

    (void)argc;
    (void)argv;

    printf("[wifi] realtek wlan bringup start\n");
    ret = realtek_wlan_bringup();
    printf("[wifi] realtek wlan bringup ret=%d\n", ret);
    return ret;
#else
    printf("[wifi] realtek wlan bringup unsupported\n");
    return 1;
#endif
}
