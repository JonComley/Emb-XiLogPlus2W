/******************************************************************************
** File:	Ser.h	
**
** Notes:	Header file for MODBUS comms with Krohne
**
** V6.00 300316 PQ first version
*/

#ifndef MODBUS_H_
#define MODBUS_H_


#define  NUM_MOD_CHANNELS				8


#define MOD_FUNC_READ_COIL				1
#define MOD_FUNC_READ_INPUT_STATUS		2
#define MOD_FUNC_READ_HOLDING_REG		3
#define MOD_FUNC_READ_INPUT_REG			4
#define MOD_FUNC_FORCE_SINGLE_COIL		5
#define MOD_FUNC_PRESET_SINGLE_REG		6
#define MOD_FUNC_READ_EXCEPTION			7
#define MOD_FUNC_FORCE_MULTIPLE_COILS	15
#define MOD_FUNC_PRESET_MULTIPLE_REG	16
#define MOD_FUNC_REPORT_SLAVE_ID		17


#define MOD_ADDR_FLOW_SPEED_MPS			0
#define MOD_ADDR_FLOW_RATE_M3S			4
#define MOD_ADDR_TOTAL_FLOW_M3			16
#define MOD_ADDR_FORWARD_VOLUME_M3		20
#define MOD_ADDR_REVERSE_VOLUME_M3		24
#define MOD_ADDR_PRESSURE_MBAR			28
#define MOD_ADDR_TEMPERATURE_K			32

#define MOD_ADDR_PRESSURE_ALARMS		36
#define MOD_ADDR_TEMP_ALARMS			40
#define MOD_ADDR_ERROR_WARNINGS			44
#define MOD_ADDR_BATTERY_TYPE			48
#define MOD_ADDR_BATTERY_CAPCITY_AH		52
#define MOD_ADDR_BATTERY_REMAIN_AH		56
#define MOD_ADDR_FLOW_DIR				60


#define MOD_ALM_BIT_ACTIVE_MIN			_B00000001
#define MOD_ALM_BIT_ACTIVE_MAX			_B00000010
#define MOD_ALM_BIT_UNREAD_MIN			_B00000100
#define MOD_ALM_BIT_UNREAD_MAX			_B00001000

#define MOD_ADDR_RECEPTION_INTERVAL_S	8007



typedef struct {
	uint8 interval;
	uint16 channel_enable_bits;
} MOD_CONFIG_t;

extern float	MOD_channel_mins[NUM_MOD_CHANNELS];
extern RTC_hhmmss_type	MOD_channel_mins_time[NUM_MOD_CHANNELS];
extern float	MOD_channel_maxes[NUM_MOD_CHANNELS];
extern RTC_hhmmss_type	MOD_channel_maxes_time[NUM_MOD_CHANNELS];

extern MOD_CONFIG_t MOD_config;
extern uint32	MOD_wakeup_time;
extern const uint8	MOD_channel_units[];
extern const uint8	MOD_channel_description[];
extern bool	MOD_tx_enable;

extern void MOD_init(void);
extern void MOD_task(void);
extern bool MOD_can_sleep(void);


#endif	/* MODBUS_H_ */
