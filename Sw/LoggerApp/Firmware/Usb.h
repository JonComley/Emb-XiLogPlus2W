/******************************************************************************
** File:	USB.h
**
** Notes:	
*/

// Local USB states:
#define USB_DISCONNECTED			0
#define USB_EXT_BATTERY				1		// external battery or power connected
#define USB_LOW_VOLTAGE_EXT_BATTERY	2		// external battery similar to USB voltage
#define USB_PWR_DETECTED			3		// 5V on USB, but no comms with PC yet
#define USB_PC_DETECTED				4
#define USB_RX_COMMAND				5
#define USB_TX_RESPONSE				6
#define USB_READ_FILE				7
#define USB_WRITE_FILE				8
#define USB_DIR						9
#define USB_MONITOR					10

#define USB_SUB_TASK()	if (USB_active) USBDeviceTasks()

extern bool USB_echo;
extern bool USB_active;

extern int USB_state;

extern uint32 USB_wakeup_time;

void USB_task(void);
void USBDeviceTasks(void);
void USB_transfer_file(char *path, char *filename, bool write);
void USB_dir(char *path);
void USB_monitor_string(char *s);
void USB_monitor_prompt(char *s);

