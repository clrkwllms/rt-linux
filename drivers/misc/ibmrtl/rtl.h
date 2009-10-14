#include <linux/io.h>

/* The RTL table looks something like
	u8 signature[5];
	u8 version;
	u8 RT_Status;
	u8 Command;
	u8 CommandStatus;
	u8 CMDAddressType;
	u8 CmdGranularity;
	u8 CmdOffset;
	u16 Reserve1;
	u8 CmdPortAddress[4];
	u8 CmdPortValue[4];
*/
#define RTL_TABLE_SIZE 0x16
#define RTL_MAGIC_IDENT (('L'<<24)|('T'<<16)|('R'<<8)|'_')
#define RTL_VERSION 0x5
#define RTL_STATE 0x6
#define RTL_CMD 0x7
#define RTL_CMD_STATUS 0x8
#define RTL_CMD_PORT_ADDR 0xE
#define RTL_CMD_PORT_VALUE 0x12

/*needed to decode CmdPortAddress and CmdPortValue*/
#define bios_to_value(rtl, offset) (u32)((readb(rtl+(offset+1)) << 8) + readb(rtl+offset))
