/****************************************************************************
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

#include <nuttx/config.h>
#ifdef CONFIG_GEMINI_S1_NSH

#if !(defined(CONFIG_BOARDCTL_RESET_CAUSE) && !defined(CONFIG_NSH_DISABLE_RESET_CAUSE))
#error "Deps: defined(CONFIG_BOARDCTL_RESET_CAUSE) && !defined(CONFIG_NSH_DISABLE_RESET_CAUSE)"
#endif
#if !defined(CONFIG_KVDB)
#error "Deps: defined(CONFIG_KVDB)"
#endif

echo "You're running an nsh image."

set sys_boot_state BOOT_INIT
echo "[sys-sm] state=$sys_boot_state"

mount -t romfs /dev/res /resource
if [ ! -f /data/log/0_syslog.txt ]
then
  mount -t yaffs /dev/usrdata /data
  set errcode $?
  if [ $errcode -ne 0 ]
  then
    echo "/dev/usrdata mount failed, errcode is" $errcode
  fi
else
  echo "data mount by file log,skip nsh mount data."
fi

#ifdef CONFIG_SYSTEM_ADBD
adbd &
#endif

#ifdef CONFIG_KVDB_SERVER
kvdbd &
#endif

#ifdef CONFIG_MEDIA_SERVER
mediad &
#endif

set resetcause `resetcause`
echo $resetcause

if [ "$resetcause" == "cpu_soft_reset(restore)" ]
then
  echo "recovery reset system"
  sh /etc/factory.sh
  echo "reboot system now"
  reboot
fi

#ifdef CONFIG_SYSTEM_NTPC
ntpcstart &
#endif

#ifdef CONFIG_SHOW_LOGO
showlogo -n /resource/logo/logo1.bin &
#endif

set sys_boot_state APP_LAUNCH
echo "[sys-sm] state=$sys_boot_state"

#ifdef CONFIG_SCOOTERDEMO_APP
scooterdemo &
#endif

#ifdef CONFIG_DOUBAO_DEMO
doubao_demo --mode audio &
#endif

#ifdef CONFIG_LUNCHER_MINI_APP
luncher_mini &
#endif

#ifdef CONFIG_DEMO_APP
demo &
#endif

set sys_boot_state WIFI_DEFER
echo "[sys-sm] state=$sys_boot_state"

#ifdef CONFIG_RIVOTEK_DEBUG_PRESET_WAPI_CONF
set wifi_cfg /data/etc/wifi/wapi.conf
set wifi_dbg /resource/etc/wifi/wapi_debug.conf
set wifi_bad /data/etc/wifi/wapi.con

if [ ! -d /data/etc ]
then
  mkdir /data/etc
fi
if [ ! -d /data/etc/wifi ]
then
  mkdir /data/etc/wifi
fi
if [ -f $wifi_bad ]
then
  rm $wifi_bad
fi
if [ -s $wifi_dbg ]
then
  cp $wifi_dbg $wifi_cfg
  echo "[wifi] debug preset wapi config copied"
fi

#endif

set sys_boot_state BT_START
echo "[sys-sm] state=$sys_boot_state"

echo "Starting bluetoothd..."
sleep 6
bluetoothd &

#ifdef CONFIG_SYSTEM_NTPC
ntpcstart &
#endif

#ifdef CONFIG_BT_A2DP
sleep 12
bt_a2dp &
#endif

set sys_boot_state BOOT_DONE
echo "[sys-sm] state=$sys_boot_state"

echo "Boot nsh ok"

#ifdef CONFIG_RIVOTEK_DEBUG_PRESET_WAPI_CONF
sh /etc/wifi/delayed_start_wifi_debug.sh &
#else
#ifdef CONFIG_RIVOTEK_CONNECTIVITY_SERVICE
sh /etc/wifi/delayed_start_wifi_connectivity.sh &
#else
sh /etc/wifi/delayed_start_wifi.sh &
#endif
#endif
#endif

#ifdef CONFIG_AW_RPTUN
sleep 30
rptun start /dev/rptun/dsp &
#endif
