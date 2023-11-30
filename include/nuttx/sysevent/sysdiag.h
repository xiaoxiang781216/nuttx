/****************************************************************************
 * include/nuttx/sysevent/sysdiag.h
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

#ifndef __INCLUDE_NUTTX_SYSEVENT_SYSDIAG_H
#define __INCLUDE_NUTTX_SYSEVENT_SYSDIAG_H

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define CORE_NAME_MAX (16)
#define EVENT_MAX_FORMART_SIZE (48)
#define DIAGNOSIS_EVENT_MAX_LEN (256)

/****************************************************************************
 * Public Types
 ****************************************************************************/

typedef enum diagnosis_tag_e
{
  DIAGNOSIS_TAG_ALWAYS                              = 0,
  POWER_AOD_STANDBY_EXCEPTION                       = 903111101,
  POWER_STANDBY_EXCEPTION_BY_LOG_ENABLED            = 903111201,
  POWER_ALARM_WAKEUP_EXCEPTION                      = 903111311,
  POWER_PARTIAL_WAKELOCK_EXCEPTION                  = 903111401,
  POWER_KERNEL_WAKELOCK_EXCEPTION                   = 903111402,
  POWER_FLASHLIGHT_EXCEPTION                        = 903111410,
  POWER_DOZE_EXCEPTION                              = 903111412,
  POWER_SUBCLASS_EXCEPTION                          = 903111601,
  THERMAL_ENVIRONMENT_REPORT                        = 905001000,
  THERMAL_EQUIPMENT_REPORT,
  THERMAL_INITIAL_DEGREE_HIGH,
  THERMAL_HIGH_DEGREE_REPORT,
  THERMAL_SHELL_DEGREE_EXCEPTION                    = 905001010,
  THERMAL_SHELL_DEGREE_MISCALCULATION,
  THERMAL_LIMIT_CPU_DEGREE_FAIL                     = 905001020,
  THERMAL_LIMIT_GPU_DEGREE_FAIL,
  THERMAL_LIMIT_MODEM_DEGREE_FAIL,
  THERMAL_LIMIT_LIGHT_DEGREE_FAIL,
  THERMAL_REFRESH_RATE_FAIL,
  THERMAL_PROCESS_CONTROL_FAIL                      = 905001030,
  THERMAL_DISABLE_WIFI_HOTPOT_FAIL,
  THERMAL_FORGROUND_PROCESS_CONTROL_FAIL,
  THERMAL_FLASHLIGHT_CONTROL_FAIL,
  THERMAL_CAMERA_CONTROL_FAIL,
  THERMAL_SCENARIO_EXCEPTION                        = 905003050,
  THERMAL_CALL_SCENARIO_MISMATCH,
  THERMAL_POWER_SCENARIO_MISMATCH,
  THERMAL_IEC_SCENARIO_MISMATCH,
  THERMAL_CLOUD_EXCEPTION                           = 905003060,
  THERMAL_SUB_INFO_REPORT                           = 905004000,
  AUDIO_NO_SOUND_CARD                               = 906001001,
  AUDIO_SETUP_PATH_EXCEPTION                        = 906001051,
  AUDIO_ADSP_RPC_EXCEPTION                          = 906001301,
  AUDIO_PA_CHIP_INTER_EXCEPTION                     = 906001351,
  AUDIO_PA_DETECTION_EXCEPTION,
  AUDIO_PA_BUS_EXCEPTION,
  AUDIO_PA_DATA_TRANSFER_EXCEPTION,
  AUDIO_MODULE_LOAD_EXCEPTION                       = 906002001,
  AUDIO_MODULE_UNLOAD_EXCEPTION,
  AUDIO_MODULE_IDENTIFY_START_EXCEPTION,
  AUDIO_MODULE_IDENTIFY_STOP_EXCEPTION,
  AUDIO_MODULE_REGISTER_SCENARIO_EXCEPTION,
  VEDIO_DECODE_MODULE_EXCEPTION                     = 906100001,
  VEDIO_CREATE_CONVERSATION_EXCEPTION,
  VEDIO_HARDWARE_OVERLOAD,
  VEDIO_RECORDER_TRACK_FAIL                         = 906121001,
  VEDIO_DECODE_DELAY                                = 906130001,
  VEDIO_HVX_OPEN_FAIL                               = 906140001,
  VIBRATOR_DRIVER_REGISTER_EXCEPTION                = 906201001,
  VIBRATOR_DRIVER_HBOOST_EXCEPTION,
  VIBRATOR_DRIVER_CONTROL_EXCEPTION,
  VIBRATOR_AGM_OPEN_EXCEPTION,
  VIBRATOR_HAL_SERVICE_EXCEPTION,
  VIBRATOR_DRIVER_F0_PROTECT_EXCEPTION              = 906202001,
  VIBRATOR_DRIVER_F0_CALIBRATION_EXCEPTION,
  WIRE_CHARGER_SLOW_BY_HIGH_THERMAL                 = 909001001,
  WIRE_CHARGER_SLOW_BY_STRICT_CONTROL,
  WIRE_CHARGER_SLOW_BY_HIGH_LOAD_APP,
  WIRE_CHARGER_SLOW_BY_FAIL_PD_QUICK_CHARGE,
  WIRE_CHARGER_SLOW_BY_NONSTANDARD_BATTERY,
  WIRE_CHARGER_UNFULL_BY_AGED_BATTERY               = 909003001,
  WIRE_CHARGER_UNFULL_BY_HIGH_THERMAL,
  WIRE_CHARGER_UNFULL_BY_NONQUICK_HIGHLOAD,
  WIRELESS_CHARGER_SLOW_BY_HIGH_THERMAL             = 909011001,
  WIRELESS_CHARGER_SLOW_BY_THIRD_PART_CHARGER       = 909011003,
  WIRELESS_CHARGER_SLOW_BY_NONSTANDARD_UNIT,
  WIRELESS_CHARGER_FAIL_ON_WIRE_CHARGING            = 909013002,
  WIRELESS_CHARGER_UI_EXCEPTION_ON_NONSTANDARD_UNIT = 909014001,
  WIRELESS_CHARGER_UI_EXCEPTION_ON_THIRD_CHARGE,
  MEAN_CURRENT_SCREEN_ON                            = 909015001,
  MEAN_CURRENT_SCREEN_OFF,
  SENSOR_ACCELEROMETER_NOT_POSITION                 = 913001001,
  SENSOR_GYRO_NOT_POSITION,
  SENSOR_LIGHT_NOT_POSITION,
  SENSOR_DISTANCE_NOT_POSITION,
  SENSOR_EARTH_INDUCTOR_NOT_POSITION,
  SENSOR_DATA_HALL_NOT_POSITION,
  SENSOR_ACCELEROMETER_NO_DATA                      = 913001050,
  SENSOR_GYRO_NO_DATA,
  SENSOR_EARTH_INDUCTOR_NO_DATA,
  SENSOR_HALL_NO_DATA,
  SENSOR_DROP_BEHAVIOR                              = 913002001,
  NET_TX_HANG                                       = 916011001,
  NET_TX_FAIL,
  NET_WIFI_TIME_SHORT,
  NET_TX_DROP_STALL,
  NET_FCS_ERROR,
  NET_RX_RECORDER_ABNORMAL,
  NET_ARP_NO_RESPONSE,
  NET_DNS_FAIL                                      = 916012001,
  NET_TCP_PACKAGE_LOSS,
  NET_TCP_RESEND,
  NET_IP_CONFLICT                                   = 916013001,
  NET_MULTI_GW_MAC_FAIL,
  NET_MULTI_DHCP_FAIL,
  NET_HTTP_PROXY_FAIL                               = 916014002,
  NET_PRIVATE_DNS_FAIL,
  NET_VPN_CREATE_FAIL                               = 916015001,
  NET_VPN_CONNECTION_FAIL,
  NET_LOGIN_AUTHENTICATION_FAIL                     = 916016101,
  NET_CAPTIVE_PORTAL_LOGIN_FAIL,
  NET_SECURITY_FORBIT                               = 916017001,
  NET_PROCESS_FROZEN,
  NET_WIFI_PING_DELAY                               = 916021001,
  NET_WIFI_DNS_ANALYSIS_DELAY                       = 916022001,
  NET_WIFI_PROTOCAL_ANALYSIS_DELAY                  = 916023001,
  NET_WIFI_DRIVER_DATASTALL                         = 916024001,
  NET_LINK_QOS_BAD,
  NET_PING_SERVER_HIGH_DELAY                        = 916025001,
  NET_AUTO_CONNECTION_FAIL                          = 916031001,
  NET_WIFI_CONNECTION_FAIL                          = 916032001,
  NET_WIFI_SIGNAL_WEAK                              = 916041001,
  NET_WIFI_OFFLINE                                  = 916051001,
  NET_WIFI_NIGHT_CLOSE                              = 916053001,
  NET_WIFI_BUTTON_GRAY                              = 916061001,
  NET_WIFI_OPNE_CLOSE_EXCEPTION,
  NET_WIFI_SCAN_FAIL,
  NET_WIFI_CLOSED_BY_THERMAL_CONTROL                = 916064001,
  NET_WIFI_CLOSED_BY_SAVE_POWER,
  BT_OPEN_FAIL                                      = 917011001,
  BT_CLOSE_FAIL,
  BT_DRIVER_EXCEPTION                               = 917012001,
  BT_THRID_PART_CALL_EXCEPTION,
  BT_FIRMWARE_DUMP,
  BT_UI_LAUNCH_SCAN_FAIL                            = 917021001,
  BT_SCAN_NO_DEVICE,
  BT_CANCEL_CONNECTION                              = 917031001,
  BT_PIN_CODE_INPUT_FAIL,
  BT_AIR_PORT_INTERACTION_FAIL,
  BT_HFP_FAIL                                       = 917041001,
  BT_AVDTP_FAIL,
  BT_MMA_CONNECT_EXCEPTION                          = 917041101,
  BT_MMA_POWER_SHOW_EXCEPTION,
  BT_MMA_OTA,
  BT_CALL_RING_IN                                   = 917042001,
  BT_CALL_RING_OUT,
  BT_SCO_SETUP_FAIL                                 = 917042101,
  BT_VGS_VGM_VOLUME_EXCEPTION,
  BT_SCO_ONE_SIDE_NO_SOUND,
  BT_SCO_TWO_SIDES_NO_SOUND,
  BT_SCO_ENV_JUNK,
  BT_UNEXPECTED_SELECT_SCO,
  BT_SCO_USAGE_EXCEPTION,
  BT_A2DP_SWITCH_SCO,
  BT_SCO_PLAY,
  BT_MULTI_VIRTUAL_CALLS_SWITCH_HANGUP             = 917072201,
  BT_MULTI_CALLS_SWITCH_HANGUP,
  BT_CALL_AND_VIRTUAL_CALLS_SWITH_HANGUP,
  BT_MULTI_DEVICES_NOTIFY                          = 917042301,
  BT_SWITCH_ON_OR_HANG_UP,
  BT_A2DP_SOUND_START_PAUSE_FAIL                   = 917043001,
  BT_A2DP_PLAY_LAUNCH_SLOW                         = 917043101,
  BT_SCO_SWITCH_A2DP,
  BT_A2DP_SOUND_AFTER_PICTURE,
  BT_TWS_TRANSFER_SINGLE_TRACK                     = 917043201,
  BT_TWS_SELF_SYNC_EXCEPTION,
  BT_AIR_PORT_NO_SOUND,
  BT_NO_SOUND_BY_BUS,
  BT_NO_SOUND_BY_AUDIO_NO_DATA,
  BT_A2DP_JUNK_BY_HIGH_COED_RATE                  = 917043301,
  BT_A2DP_JUNK_ON_BTC,
  BT_A2DP_JUNK_BY_DECODE_PARA,
  BT_A2DP_CODEC_SWITCH_ON_GAME                    = 917043401,
  BT_A2DP_CODEC_SWITCH_ON_SETTING,
  BT_A2DP_CODEC_SWITCH_ON_DEVELOPER,
  BT_MULTI_DEVICES_ACTIVE_SWITCH                  = 917043501,
  BT_SHARE_AUDIO_EXCEPTION                        = 917043601,
  BT_SOUND_INFO_EXCEPTION                         = 917044001,
  BT_PLAY_PAUSE_EXCEPTION                         = 917044101,
  BT_VOLUME_EXCEPTION_WITHOUT_ABSOLUTE_VOLUME     = 917044201,
  BT_VOLUME_EXCEPTION_WITH_ABSOLUTE_VOLUME,
  BT_VOLUME_EXCEPTION_BY_SOUND_OFF,
  BT_LEA_EXCEPTION                                = 917045000,
  BT_OPP_CONNECTION_FAIL                          = 917051001,
  BT_OPP_THROUGHPUT_EXCEPTION,
  BT_PAN_OPEN_SHARE                               = 917052001,
  BT_PAN_CLOSE_SHARE,
  BT_PAN_CONNECTION_FAIL_BY_NETWORK_SETTING,
  BT_PAN_THROUGHPUT_EXCEPTION,
  BT_PBAP_CONNECTION_FAIL                         = 917053001,
  BT_PBAP_SYNC_SEARCH_EXCEPTION,
  BT_MAP_CONNECTION_FAIL                          = 917054001,
  BT_MAP_SYNC_SEARCH_EXCEPTION,
  BT_MAP_TRANSCEIVER_EXCEPTION,
  BT_HID_HOGP_CONNECTION_FAIL                     = 917061001,
  BT_HID_HOGP_KEY_INPUT_NO_WORK,
  BT_HID_HOGP_RECONNECTION_FAIL,
  BT_KEY_INPUT_SPEED_UNEXPECTED                   = 917061005,
  BT_SCAN_DEVICE_EXCEPTION                        = 917071001,
  BT_CONNECT_DEVICE_EXCEPTION,
  BT_DISCONNECT_DEVICE_EXCEPTION                  = 917091001,
  BT_TWS_SYNC_EXCEPTION,
  BT_NO_SOUND_JUNK,
  BT_WEAR_DETECTION,
  BT_MMA_INTERATION_EXCEPTION,
  BT_CAR_PLAY_PAUSE_EXCEPTION,
  BT_CAR_VOLUME_SETTING_EXCEPTION,
  GPS_SEARCH_SATELLITES_NOT_ENOUGH_FOR_LOCATION  = 919012101,
  GPS_SIGNAL_WEAK_LOCATION_FAIL                  = 919021001,

  /* Always last */

  DIAGNOSIS_TAG_LAST                             = 1000000000,
  DIAGNOSIS_TAG_MAX = DIAGNOSIS_TAG_LAST + 16
} diagnosis_tag_t;

typedef enum
{
  EVENT_TYPE_FORMAT = 0,
  EVENT_TYPE_JSON,
  EVENT_TYPE_KERNEL
} event_type_t;

typedef struct diag_header_s
{
  uint64_t        time;
  uint16_t        event_index;
  uint16_t        len;
  int             pid;
  int             tid;
  diagnosis_tag_t id;
  event_type_t    type;
  char            format[EVENT_MAX_FORMART_SIZE];
  char            core[CORE_NAME_MAX];
} diag_header_t;

typedef struct diag_event_s
{
  diag_header_t header;
  char payload[DIAGNOSIS_EVENT_MAX_LEN];
} diag_event_t;

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: sysdiag_write
 *
 * Description:
 *   Write a diagnosis event to a general buffer for later analysis and
 *   further recovery or monitor.
 *
 ****************************************************************************/

int sysdiag_write(enum diagnosis_tag_e diag_id, FAR const char *fmt, ...);
#endif  //__INCLUDE_NUTTX_SYSEVENT_SYSDIAG_H
