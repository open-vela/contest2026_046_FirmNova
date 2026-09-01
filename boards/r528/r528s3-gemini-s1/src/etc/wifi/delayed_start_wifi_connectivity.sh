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

start_wifi
set wifi_ret $?
if [ $wifi_ret -eq 0 ]
then
  set sys_boot_state WIFI_READY
else
  if [ $wifi_ret -eq 2 ]
  then
    set sys_boot_state WIFI_SKIP
  else
    set sys_boot_state WIFI_FAIL
  fi
fi
echo "[sys-sm] state=$sys_boot_state ret=$wifi_ret"
