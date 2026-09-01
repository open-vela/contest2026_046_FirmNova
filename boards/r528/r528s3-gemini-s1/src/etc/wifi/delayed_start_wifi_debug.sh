set +e

echo "[wifi] delayed wifi start scheduled"
sleep 15

echo "[wifi] delayed realtek wlan bringup"
wifi_bringup
set wifi_bringup_ret $?
echo "[wifi] delayed realtek wlan bringup ret=$wifi_bringup_ret"

echo "[wifi] delayed ifup wlan0"
ifup wlan0
set wifi_ifup_ret $?
echo "[wifi] delayed ifup wlan0 ret=$wifi_ifup_ret"

set wifi_cfg /data/etc/wifi/wapi.conf

if [ -s $wifi_cfg ]
then
  sh /etc/wifi/start_wifi.sh
  set wifi_ret $?
  if [ $wifi_ret -eq 0 ]
  then
    set sys_boot_state WIFI_READY
  else
    set sys_boot_state WIFI_FAIL
  fi
  echo "[sys-sm] state=$sys_boot_state ret=$wifi_ret"
else
  set sys_boot_state WIFI_SKIP
  echo "[sys-sm] state=$sys_boot_state"
fi
