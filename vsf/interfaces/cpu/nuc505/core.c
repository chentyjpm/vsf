#include "app_type.h"
#include "compiler.h"
#include "interfaces.h"

#include "NUC505Series.h"
#include "core.h"

static struct nuc505_info_t nuc505_info =
{
	0, CORE_VECTOR_TABLE,
	CORE_CLKEN,
	CORE_HCLKSRC,
	OSC_FREQ_HZ, OSC32_FREQ_HZ, LIRC_FREQ_HZ,
	CORE_PLL_FREQ_HZ, CORE_APLL_FREQ_HZ, CPU_FREQ_HZ, HCLK_FREQ_HZ, PCLK_FREQ_HZ,
};

void HardFault_Handler(void)
{
	while (1);
}

vsf_err_t nuc505_interface_get_info(struct nuc505_info_t **info)
{
	*info = &nuc505_info;
	return VSFERR_NONE;
}

vsf_err_t nuc505_interface_fini(void *p)
{
	return VSFERR_NONE;
}

vsf_err_t nuc505_interface_reset(void *p)
{
	SYS->IPRST0 |= SYS_IPRST0_CHIPRST_Msk;
	return VSFERR_NONE;
}

uint32_t nuc505_interface_get_stack(void)
{
	return __get_MSP();
}

vsf_err_t nuc505_interface_set_stack(uint32_t sp)
{
	__set_MSP(sp);
	return VSFERR_NONE;
}

// sleep will enable interrupt
// for cortex processor, if an interrupt occur between enable the interrupt
// 		and __WFI, wfi will not make the core sleep
void nuc505_interface_sleep(uint32_t mode)
{
	vsf_leave_critical();
	if (mode == nuc505_SLEEP_WFI) // sleep
	{
		SCB->SCR &= ~0x4ul;
	}
	else // power down
	{
		SCB->SCR |= 0x4ul;
		CLK->PWRCTL |= CLK_PWRCTL_PDWKIF_Msk;
	}
	CLK->PWRCTL &= ~CLK_PWRCTL_HXTEN_Msk;
	__WFI();
}

vsf_err_t nuc505_interface_init(void *p)
{
	uint32_t temp32;

	if (p != NULL)
	{
		nuc505_info = *(struct nuc505_info_t *)p;
	}

	if (nuc505_info.osc_freq_hz != (12 * 1000 * 1000))
		return VSFERR_INVALID_PARAMETER;

	CLK->PWRCTL |= CLK_PWRCTL_PDWKIEN_Msk | CLK_PWRCTL_PDWTCPU_Msk;

	// switch HCLK to HXT
	CLK->CLKDIV0 &= ~CLK_CLKDIV0_HCLKSEL_Msk;

	if (nuc505_info.clk_enable & NUC505_CLK_LXT)
	{
		RTC->SET = RTC_SET_CBEN_Msk | RTC_SET_IOMSEL_Msk;
		//RTC->SET = RTC_SET_CBEN_Msk;
		RTC->CLKSRC &= ~RTC_CLKSRC_CKSRC_Msk;
	}
	else if (nuc505_info.clk_enable & NUC505_CLK_LIRC)
	{
		RTC->SET = 0;
		RTC->SET = RTC_CLKSRC_CKSRC_Msk;
	}

	if (nuc505_info.clk_enable & NUC505_CLK_PLL)
	{
		uint32_t n, m, p;

		for (p = 1; p <= 8; p++)
		{
			if ((nuc505_info.pll_freq_hz * p >= (300 * 1000 * 1000)) &&
					(nuc505_info.pll_freq_hz * p < (1000 * 1000 * 1000)))
			break;
		}
		if ((p == 8) &&
			((nuc505_info.pll_freq_hz * 8 < (300 * 1000 * 1000)) ||
				(nuc505_info.pll_freq_hz * 8 >= (1000 * 1000 * 1000))))
			return VSFERR_INVALID_PARAMETER;

		if (nuc505_info.pll_freq_hz * p > (768 * 1000 * 1000))
			m = 1;
		else
			m = 2;

		n = nuc505_info.pll_freq_hz * p * m / nuc505_info.osc_freq_hz;
		if ((n < 1) || (n > 128))
			return VSFERR_INVALID_PARAMETER;

		CLK->PLLCTL = (n - 1) + ((m - 1) << 7) + ((p - 1) << 13);
	}
	else
	{
		CLK->PLLCTL |= CLK_PLLCTL_PD_Msk;
	}

	// TODO
	if (nuc505_info.clk_enable & NUC505_CLK_APLL)
	{

	}
	else
	{
		CLK->APLLCTL |= CLK_APLLCTL_PD_Msk;
	}

    // set pclk
	temp32 = nuc505_info.hclk_freq_hz / nuc505_info.pclk_freq_hz;
	if ((temp32 < 1) || (temp32 > 16))
		return VSFERR_INVALID_PARAMETER;
	CLK->CLKDIV0 = (CLK->CLKDIV0 & (~CLK_CLKDIV0_PCLKDIV_Msk)) |
					((temp32 - 1) << CLK_CLKDIV0_PCLKDIV_Pos);

	// set hclk
	switch (nuc505_info.hclksrc)
	{
	case NUC505_HCLKSRC_PLLFOUT:
		temp32 = nuc505_info.pll_freq_hz / nuc505_info.hclk_freq_hz;
		if ((temp32 < 1) || (temp32 > 16))
			return VSFERR_INVALID_PARAMETER;
		CLK->CLKDIV0 = ((CLK->CLKDIV0 | CLK_CLKDIV0_HCLKSEL_Msk) & ~CLK_CLKDIV0_HCLKDIV_Msk) | (temp32 - 1);
		break;
	case NUC505_HCLKSRC_HXT:
		// do nothing
		break;
	default:
		return VSFERR_INVALID_PARAMETER;
	}

	SCB->VTOR = nuc505_info.vector_table;
	SCB->AIRCR = 0x05FA0000 | nuc505_info.priority_group;
	return VSFERR_NONE;
}

static void (*nuc505_tickclk_callback)(void *param) = NULL;
static void *nuc505_tickclk_param = NULL;
static volatile uint32_t nuc505_tickcnt = 0;

vsf_err_t nuc505_tickclk_start(void)
{
	TIMER2->CTL |= TIMER_CTL_CNTEN_Msk;
	TIMER3->CTL |= TIMER_CTL_CNTEN_Msk;
	nuc505_tickclk_set_interval(1);
	return VSFERR_NONE;
}

vsf_err_t nuc505_tickclk_stop(void)
{
	uint32_t cnt;
	TIMER2->CTL &= ~TIMER_CTL_CNTEN_Msk;
	TIMER3->CTL &= ~TIMER_CTL_CNTEN_Msk;

	cnt = TIMER2->CNT;
	if (cnt > 0)
	{
		nuc505_tickcnt += (cnt * 1000) / 32768;
		TIMER2->CTL |= TIMER_CTL_RSTCNT_Msk;
	}
	return VSFERR_NONE;
}

static uint32_t nuc505_tickclk_get_count_local(void)
{
	uint32_t cnt = TIMER2->CNT;
	return nuc505_tickcnt + ((cnt * 1000) / 32768);
}

uint32_t nuc505_tickclk_get_count(void)
{
	uint32_t count1, count2;

	do {
		count1 = nuc505_tickclk_get_count_local();
		count2 = nuc505_tickclk_get_count_local();
	} while (count1 != count2);
	return count1;
}

vsf_err_t nuc505_tickclk_set_callback(void (*callback)(void*), void *param)
{
	if (TIMER3->CTL & TIMER_CTL_CNTEN_Msk)
	{
		TIMER3->CTL &= ~TIMER_CTL_CNTEN_Msk;
		nuc505_tickclk_callback = callback;
		nuc505_tickclk_param = param;
		TIMER3->CTL |= TIMER_CTL_CNTEN_Msk;
	}
	else
	{
		nuc505_tickclk_callback = callback;
		nuc505_tickclk_param = param;
	}
	return VSFERR_NONE;
}

vsf_err_t nuc505_tickclk_set_interval(uint16_t ms)
{
	if (ms == 0)
		return VSFERR_FAIL;

	TIMER3->CTL &= ~TIMER_CTL_CNTEN_Msk;
	TIMER3->CTL = TIMER_CTL_RSTCNT_Msk;
	TIMER3->CTL = TIMER_CTL_INTEN_Msk | (0x1ul << TIMER_CTL_OPMODE_Pos) |
					TIMER_CTL_WKEN_Msk;
	TIMER3->CMP = (32768 * ms) / 1000;
	TIMER3->CTL |= TIMER_CTL_CNTEN_Msk;

	return VSFERR_NONE;
}

ROOTFUNC void TMR2_IRQHandler(void)
{
	nuc505_tickcnt += 500 * 1000;
	if (nuc505_tickcnt > 0xffffffff - 1200 * 1000)
		nuc505_interface_reset(NULL);
	TIMER2->INTSTS = TIMER_INTSTS_TIF_Msk;
}

ROOTFUNC void TMR3_IRQHandler(void)
{
	if (nuc505_tickclk_callback != NULL)
	{
		nuc505_tickclk_callback(nuc505_tickclk_param);
	}
	//TIMER3->CMP = 32768;
	TIMER3->INTSTS = TIMER_INTSTS_TIF_Msk;
}

vsf_err_t nuc505_tickclk_init(void)
{
	nuc505_tickcnt = 0;
	NVIC_EnableIRQ(TMR2_IRQn);
	NVIC_EnableIRQ(TMR3_IRQn);

	TIMER2->CTL = TIMER_CTL_RSTCNT_Msk;
	TIMER3->CTL = TIMER_CTL_RSTCNT_Msk;
	CLK->APBCLK |= CLK_APBCLK_TMR2CKEN_Msk | CLK_APBCLK_TMR3CKEN_Msk;

	CLK->CLKDIV4 &= ~(CLK_CLKDIV4_TMR2SEL_Msk | CLK_CLKDIV4_TMR2DIV_Msk);
	CLK->CLKDIV5 &= ~(CLK_CLKDIV5_TMR3SEL_Msk | CLK_CLKDIV5_TMR3DIV_Msk);
	TIMER2->CTL = TIMER_CTL_INTEN_Msk | (0x1ul << TIMER_CTL_OPMODE_Pos) |
					TIMER_CTL_WKEN_Msk;

	TIMER2->CMP = 32768 * 500;
	TIMER3->CMP = 32768 / 1000;

	return VSFERR_NONE;
}

vsf_err_t nuc505_tickclk_fini(void)
{
	NVIC_DisableIRQ(TMR2_IRQn);
	NVIC_DisableIRQ(TMR3_IRQn);
	TIMER2->CTL = TIMER_CTL_RSTCNT_Msk;
	TIMER3->CTL = TIMER_CTL_RSTCNT_Msk;
	CLK->APBCLK &= ~(CLK_APBCLK_TMR2CKEN_Msk | CLK_APBCLK_TMR3CKEN_Msk);
	return VSFERR_NONE;
}

static uint32_t get_pc(void)
{
	uint32_t pc;
	asm("MOV	%0,	pc" : "=r" (pc));
	return pc;
}

// special
int32_t nuc505_is_running_on_ram(void)
{
	uint32_t vecmap_addr = SYS->RVMPADDR, vecmap_len = SYS->RVMPLEN >> 14;
	uint32_t pc = get_pc();

	if (((pc >= 0x20000000) && (pc < 0x20020000)) ||
		((pc >= 0x1ff00000) && (pc < 0x1ff20000)) ||
		((vecmap_addr != 0) && (pc < vecmap_len)))
	{
		return 0;
	}
	else
	{
		return -1;
	}
}

vsf_err_t nuc505_code_map(uint8_t en, uint8_t rst, uint8_t len_kb, uint32_t addr)
{
	if (!en)
	{
		SYS->LVMPADDR = 0;
		SYS->LVMPLEN = 0;
	}
	else if ((len_kb > 0) && (len_kb <= 128))
	{
		memcpy((void *)0x20000000, (void *)addr, 1024ul * len_kb);
		SYS->LVMPADDR = 0x20000000;
		SYS->LVMPLEN = len_kb;
	}
	else
	{
		return VSFERR_INVALID_PARAMETER;
	}

	if (rst)
	{
		SYS->IPRST0 |= SYS_IPRST0_CPURST_Msk;
	}
	else
	{
		SYS->RVMPLEN |= 0x1;
	}
	return VSFERR_NONE;
}

