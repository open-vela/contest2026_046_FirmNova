set +e

echo "[wifi] start_wifi"

if [ ! -s /data/etc/wifi/wapi.conf ]
then
  echo "[wifi] missing wapi config"
  exit 2
fi

wapi disconnect wlan0
sleep 1

echo "[wifi] disable adaptive mode"
wapi private wlan0 adaptivity 0

echo "[wifi] disable power save"
wapi power_save wlan0 off

echo "[wifi] reconnecting from wapi.conf"
wapi reconnect wlan0
set wifi_ret $?
if [ $wifi_ret -ne 0 ]
then
  echo "[wifi] reconnect failed ret=$wifi_ret"
  wapi show wlan0
  exit 1
fi

sleep 5

echo "[wifi] DHCP renew try 1"
renew wlan0
set wifi_ret $?
if [ $wifi_ret -ne 0 ]
then
  sleep 2
  echo "[wifi] DHCP renew try 2"
  renew wlan0
  set wifi_ret $?
fi
if [ $wifi_ret -ne 0 ]
then
  sleep 2
  echo "[wifi] DHCP renew try 3"
  renew wlan0
  set wifi_ret $?
fi

wapi show wlan0
if [ $wifi_ret -ne 0 ]
then
  echo "[wifi] DHCP renew failed ret=$wifi_ret"
  exit 1
fi

echo "[wifi] wifi startup done"
exit 0
