#define APP_BOARD_NAME				"VersaloonProR3"

struct app_hwcfg_t
{
	char *board;
	struct
	{
		uint8_t port;
		uint8_t pin;
	} key;
	struct
	{
		struct
		{
			uint8_t port;
			uint8_t pin;
		} usb_pullup;
	} usbd;
	struct led_hw_t
	{
		uint8_t hport;
		uint8_t hpin;
		uint8_t lport;
		uint8_t lpin;
	} led[24];
	struct
	{
		int type;
		union
		{
			struct
			{
				uint8_t port;
				uint8_t cs_port;
				uint8_t cs_pin;
				uint32_t freq_khz;
			} spi;
		} comm;
		uint8_t rst_port;
		uint8_t rst_pin;
		uint8_t eint_port;
		uint8_t eint_pin;
		uint8_t eint;
	} bcm_wifi_port;
};
