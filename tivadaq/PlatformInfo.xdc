package tivadaq;

metaonly module PlatformInfo {

    // Copied from TivaWare 2.0 headers

    readonly config UInt32* UART0_BASE           = 0x4000C000;
    readonly config UInt32* UART1_BASE           = 0x4000D000;
    readonly config UInt32* UART2_BASE           = 0x4000E000;
    readonly config UInt32* UART3_BASE           = 0x4000F000;
    readonly config UInt32* UART4_BASE           = 0x40010000;
    readonly config UInt32* UART5_BASE           = 0x40011000;
    readonly config UInt32* UART6_BASE           = 0x40012000;
    readonly config UInt32* UART7_BASE           = 0x40013000;

    readonly config UInt32* TIMER0_BASE          = 0x40030000;
    readonly config UInt32* TIMER1_BASE          = 0x40031000;
    readonly config UInt32* TIMER2_BASE          = 0x40032000;
    readonly config UInt32* TIMER3_BASE          = 0x40033000;
    readonly config UInt32* TIMER4_BASE          = 0x40034000;
    readonly config UInt32* TIMER5_BASE          = 0x40035000;

    readonly config UInt32* ADC0_BASE            = 0x40038000;
    readonly config UInt32* ADC1_BASE            = 0x40039000;

    readonly config UInt32* GPIO_PORTA_BASE      = 0x40004000;
    readonly config UInt32* GPIO_PORTB_BASE      = 0x40005000;
    readonly config UInt32* GPIO_PORTC_BASE      = 0x40006000;
    readonly config UInt32* GPIO_PORTD_BASE      = 0x40007000;
    readonly config UInt32* GPIO_PORTE_BASE      = 0x40024000;
    readonly config UInt32* GPIO_PORTF_BASE      = 0x40025000;

    // TODO: double check 'Blizzard' vs. 'Snowflake' device revision
    readonly config UInt32 INT_UART0            = 21;
    readonly config UInt32 INT_UART1            = 22;
    readonly config UInt32 INT_UART2            = 49;
    readonly config UInt32 INT_UART3            = 75;
    readonly config UInt32 INT_UART4            = 76;
    readonly config UInt32 INT_UART5            = 77;
    readonly config UInt32 INT_UART6            = 78;
    readonly config UInt32 INT_UART7            = 79;

    readonly config UInt32 INT_ADC0SS0          = 30;
    readonly config UInt32 INT_ADC0SS1          = 31;
    readonly config UInt32 INT_ADC0SS2          = 32;
    readonly config UInt32 INT_ADC0SS3          = 33;

    readonly config UInt32 INT_ADC1SS0          = 64;
    readonly config UInt32 INT_ADC1SS1          = 65;
    readonly config UInt32 INT_ADC1SS2          = 66;
    readonly config UInt32 INT_ADC1SS3          = 67;

    readonly config UInt32 SYSCTL_PERIPH_UART0  = 0xf0001800;
    readonly config UInt32 SYSCTL_PERIPH_UART1  = 0xf0001801;
    readonly config UInt32 SYSCTL_PERIPH_UART2  = 0xf0001802;
    readonly config UInt32 SYSCTL_PERIPH_UART3  = 0xf0001803;
    readonly config UInt32 SYSCTL_PERIPH_UART4  = 0xf0001804;
    readonly config UInt32 SYSCTL_PERIPH_UART5  = 0xf0001805;
    readonly config UInt32 SYSCTL_PERIPH_UART6  = 0xf0001806;
    readonly config UInt32 SYSCTL_PERIPH_UART7  = 0xf0001807;

    readonly config UInt32 SYSCTL_PERIPH_GPIOA  = 0xf0000800;
    readonly config UInt32 SYSCTL_PERIPH_GPIOB  = 0xf0000801;
    readonly config UInt32 SYSCTL_PERIPH_GPIOC  = 0xf0000802;
    readonly config UInt32 SYSCTL_PERIPH_GPIOD  = 0xf0000803;
    readonly config UInt32 SYSCTL_PERIPH_GPIOE  = 0xf0000804;
    readonly config UInt32 SYSCTL_PERIPH_GPIOF  = 0xf0000805;

    readonly config UInt32 SYSCTL_PERIPH_ADC0   = 0xf0003800;
    readonly config UInt32 SYSCTL_PERIPH_ADC1   = 0xf0003801;

    readonly config UInt32 SYSCTL_PERIPH_TIMER0 = 0xf0000400;
    readonly config UInt32 SYSCTL_PERIPH_TIMER1 = 0xf0000401;
    readonly config UInt32 SYSCTL_PERIPH_TIMER2 = 0xf0000402;
    readonly config UInt32 SYSCTL_PERIPH_TIMER3 = 0xf0000403;
    readonly config UInt32 SYSCTL_PERIPH_TIMER4 = 0xf0000404;
    readonly config UInt32 SYSCTL_PERIPH_TIMER5 = 0xf0000405;
    readonly config UInt32 SYSCTL_PERIPH_TIMER6 = 0xf0000406;
    readonly config UInt32 SYSCTL_PERIPH_TIMER7 = 0xf0000407;

    readonly config UInt32 GPIO_PIN_0           = 0x00000001;
    readonly config UInt32 GPIO_PIN_1           = 0x00000002;
    readonly config UInt32 GPIO_PIN_2           = 0x00000004;
    readonly config UInt32 GPIO_PIN_3           = 0x00000008;
    readonly config UInt32 GPIO_PIN_4           = 0x00000010;
    readonly config UInt32 GPIO_PIN_5           = 0x00000020;
    readonly config UInt32 GPIO_PIN_6           = 0x00000040;
    readonly config UInt32 GPIO_PIN_7           = 0x00000080;

    readonly config UInt32 GPIO_PA0_U0RX        = 0x00000001;
    readonly config UInt32 GPIO_PA1_U0TX        = 0x00000401;
    readonly config UInt32 GPIO_PB0_U1RX        = 0x00010001;
    readonly config UInt32 GPIO_PB1_U1TX        = 0x00010401;
    readonly config UInt32 GPIO_PC4_U1RX        = 0x00021002;
    readonly config UInt32 GPIO_PC5_U1TX        = 0x00021402;
    readonly config UInt32 GPIO_PD6_U2RX        = 0x00031801;
    readonly config UInt32 GPIO_PD7_U2TX        = 0x00031C01;
    readonly config UInt32 GPIO_PC6_U3RX        = 0x00021801;
    readonly config UInt32 GPIO_PC7_U3TX        = 0x00021C01;
    readonly config UInt32 GPIO_PC4_U4RX        = 0x00021001;
    readonly config UInt32 GPIO_PC5_U4TX        = 0x00021401;
    readonly config UInt32 GPIO_PE4_U5RX        = 0x00041001;
    readonly config UInt32 GPIO_PE5_U5TX        = 0x00041401;
    readonly config UInt32 GPIO_PD4_U6RX        = 0x00031001;
    readonly config UInt32 GPIO_PD5_U6TX        = 0x00031401;
    readonly config UInt32 GPIO_PE0_U7RX        = 0x00040001;
    readonly config UInt32 GPIO_PE1_U7TX        = 0x00040401;

    readonly config UInt32 UDMA_CHANNEL_UART0RX =  8;
    readonly config UInt32 UDMA_CHANNEL_UART0TX =  9;
    readonly config UInt32 UDMA_CHANNEL_UART1RX = 22;
    readonly config UInt32 UDMA_CHANNEL_UART1TX = 23;
    // TODO: udma.h has only legacy 'secondary' macros (also see ADC below)

    // From TM4C datasheet
    readonly config UInt32 MAX_UDMA_TRANSFER_SIZE = 1024;

    readonly config UInt32 UDMA_CHANNEL_ADC0           = 14;
    readonly config UInt32 UDMA_CHANNEL_ADC1           = 15;
    readonly config UInt32 UDMA_CHANNEL_ADC2           = 16;
    readonly config UInt32 UDMA_CHANNEL_ADC3           = 17;

    readonly config UInt32 UDMA_SEC_CHANNEL_ADC10      = 24;
    readonly config UInt32 UDMA_SEC_CHANNEL_ADC11      = 25;
    readonly config UInt32 UDMA_SEC_CHANNEL_ADC12      = 26;
    readonly config UInt32 UDMA_SEC_CHANNEL_ADC13      = 27;

    // The names in driverlib/udma.h are a bit irregular, so fix them up
    readonly config UInt32 UDMA_CHANNEL_ADC_0_0 = UDMA_CHANNEL_ADC0;
    readonly config UInt32 UDMA_CHANNEL_ADC_0_1 = UDMA_CHANNEL_ADC1;
    readonly config UInt32 UDMA_CHANNEL_ADC_0_2 = UDMA_CHANNEL_ADC2;
    readonly config UInt32 UDMA_CHANNEL_ADC_0_3 = UDMA_CHANNEL_ADC3;
    readonly config UInt32 UDMA_CHANNEL_ADC_1_0 = UDMA_SEC_CHANNEL_ADC10;
    readonly config UInt32 UDMA_CHANNEL_ADC_1_1 = UDMA_SEC_CHANNEL_ADC11;
    readonly config UInt32 UDMA_CHANNEL_ADC_1_2 = UDMA_SEC_CHANNEL_ADC12;
    readonly config UInt32 UDMA_CHANNEL_ADC_1_3 = UDMA_SEC_CHANNEL_ADC13;

    // From TM4C datasheet (p. 586)
    readonly config UInt32 UDMA_CHANNEL_NUM_ADC_0_0 = 14;
    readonly config UInt32 UDMA_CHANNEL_NUM_ADC_0_1 = 15;
    readonly config UInt32 UDMA_CHANNEL_NUM_ADC_0_2 = 16;
    readonly config UInt32 UDMA_CHANNEL_NUM_ADC_0_3 = 17;
    readonly config UInt32 UDMA_CHANNEL_NUM_ADC_1_0 = 24;
    readonly config UInt32 UDMA_CHANNEL_NUM_ADC_1_1 = 25;
    readonly config UInt32 UDMA_CHANNEL_NUM_ADC_1_2 = 26;
    readonly config UInt32 UDMA_CHANNEL_NUM_ADC_1_3 = 27;

    readonly config UInt32 UDMA_CH14_ADC0_0        =  0x0000000E;
    readonly config UInt32 UDMA_CH15_ADC0_1        =  0x0000000F;
    readonly config UInt32 UDMA_CH16_ADC0_2        =  0x00000010;
    readonly config UInt32 UDMA_CH17_ADC0_3        =  0x00000011;
    readonly config UInt32 UDMA_CH24_ADC1_0        =  0x00010018;
    readonly config UInt32 UDMA_CH25_ADC1_1        =  0x00010019;
    readonly config UInt32 UDMA_CH26_ADC1_2        =  0x0001001A;
    readonly config UInt32 UDMA_CH27_ADC1_3        =  0x0001001B;

    readonly config UInt32 UDMA_ARB_1              = 0x00000000;
    readonly config UInt32 UDMA_ARB_2              = 0x00004000;
    readonly config UInt32 UDMA_ARB_4              = 0x00008000;
    readonly config UInt32 UDMA_ARB_8              = 0x0000c000;
    readonly config UInt32 UDMA_ARB_16             = 0x00010000;
    readonly config UInt32 UDMA_ARB_32             = 0x00014000;
    readonly config UInt32 UDMA_ARB_64             = 0x00018000;
    readonly config UInt32 UDMA_ARB_128            = 0x0001c000;
    readonly config UInt32 UDMA_ARB_256            = 0x00020000;
    readonly config UInt32 UDMA_ARB_512            = 0x00024000;
    readonly config UInt32 UDMA_ARB_1024           = 0x00028000;

    readonly config UInt32 TIMER_A                 = 0x000000ff;
    readonly config UInt32 TIMER_B                 = 0x0000ff00;
    readonly config UInt32 TIMER_BOTH              = 0x0000ffff;

    readonly config UInt32 TIMER_CFG_SPLIT_PAIR    = 0x04000000;
    readonly config UInt32 TIMER_CFG_A_ONE_SHOT    = 0x00000021;
    readonly config UInt32 TIMER_CFG_A_ONE_SHOT_UP = 0x00000031;
    readonly config UInt32 TIMER_CFG_A_PERIODIC    = 0x00000022;
    readonly config UInt32 TIMER_CFG_A_PERIODIC_UP = 0x00000032;
    readonly config UInt32 TIMER_CFG_B_ONE_SHOT    = 0x00002100;
    readonly config UInt32 TIMER_CFG_B_ONE_SHOT_UP = 0x00003100;
    readonly config UInt32 TIMER_CFG_B_PERIODIC    = 0x00002200;
    readonly config UInt32 TIMER_CFG_B_PERIODIC_UP = 0x00003200;

    // From driverlib/adc.c
    readonly config UInt32 ADC_O_CTL              = 0x00000038;
    readonly config UInt32 ADC_O_SSMUX0           = 0x00000040;
    readonly config UInt32 ADC_O_SSMUX1           = 0x00000060;

    readonly config UInt32 ADC_O_SSCTL0           = 0x00000044;
    readonly config UInt32 ADC_O_SSFIFO0          = 0x00000048;

    readonly config UInt32 ADC_INT_SS0            = 0x00000001;
    readonly config UInt32 ADC_INT_SS1            = 0x00000002;
    readonly config UInt32 ADC_INT_SS2            = 0x00000004;
    readonly config UInt32 ADC_INT_SS3            = 0x00000008;
    readonly config UInt32 ADC_INT_DMA_SS0        = 0x00000100;
    readonly config UInt32 ADC_INT_DMA_SS1        = 0x00000200;
    readonly config UInt32 ADC_INT_DMA_SS2        = 0x00000400;
    readonly config UInt32 ADC_INT_DMA_SS3        = 0x00000800;

    readonly config UInt32 ADC_CTL_TS             = 0x00000080;
    readonly config UInt32 ADC_CTL_IE             = 0x00000040;
    readonly config UInt32 ADC_CTL_END            = 0x00000020;
    readonly config UInt32 ADC_CTL_D              = 0x00000010;
    readonly config UInt32 ADC_CTL_CH0            = 0x00000000;
    readonly config UInt32 ADC_CTL_CH1            = 0x00000001;
    readonly config UInt32 ADC_CTL_CH2            = 0x00000002;
    readonly config UInt32 ADC_CTL_CH3            = 0x00000003;
    readonly config UInt32 ADC_CTL_CH4            = 0x00000004;
    readonly config UInt32 ADC_CTL_CH5            = 0x00000005;
    readonly config UInt32 ADC_CTL_CH6            = 0x00000006;
    readonly config UInt32 ADC_CTL_CH7            = 0x00000007;
    readonly config UInt32 ADC_CTL_CH8            = 0x00000008;
    readonly config UInt32 ADC_CTL_CH9            = 0x00000009;
    readonly config UInt32 ADC_CTL_CH10           = 0x0000000A;
    readonly config UInt32 ADC_CTL_CH11           = 0x0000000B;
    readonly config UInt32 ADC_CTL_CH12           = 0x0000000C;
    readonly config UInt32 ADC_CTL_CH13           = 0x0000000D;
    readonly config UInt32 ADC_CTL_CH14           = 0x0000000E;
    readonly config UInt32 ADC_CTL_CH15           = 0x0000000F;
    readonly config UInt32 ADC_CTL_CH16           = 0x00000100;
    readonly config UInt32 ADC_CTL_CH17           = 0x00000101;
    readonly config UInt32 ADC_CTL_CH18           = 0x00000102;
    readonly config UInt32 ADC_CTL_CH19           = 0x00000103;
    readonly config UInt32 ADC_CTL_CH20           = 0x00000104;
    readonly config UInt32 ADC_CTL_CH21           = 0x00000105;
    readonly config UInt32 ADC_CTL_CH22           = 0x00000106;
    readonly config UInt32 ADC_CTL_CH23           = 0x00000107;
    readonly config UInt32 ADC_CTL_CMP0           = 0x00080000;
    readonly config UInt32 ADC_CTL_CMP1           = 0x00090000;
    readonly config UInt32 ADC_CTL_CMP2           = 0x000A0000;
    readonly config UInt32 ADC_CTL_CMP3           = 0x000B0000;
    readonly config UInt32 ADC_CTL_CMP4           = 0x000C0000;
    readonly config UInt32 ADC_CTL_CMP5           = 0x000D0000;
    readonly config UInt32 ADC_CTL_CMP6           = 0x000E0000;
    readonly config UInt32 ADC_CTL_CMP7           = 0x000F0000;

    readonly config UInt32 ADC_TRIGGER_PROCESSOR  = 0x00000000;
    readonly config UInt32 ADC_TRIGGER_COMP0      = 0x00000001;
    readonly config UInt32 ADC_TRIGGER_COMP1      = 0x00000002;
    readonly config UInt32 ADC_TRIGGER_COMP2      = 0x00000003;
    readonly config UInt32 ADC_TRIGGER_EXTERNAL   = 0x00000004;
    readonly config UInt32 ADC_TRIGGER_TIMER      = 0x00000005;
    readonly config UInt32 ADC_TRIGGER_PWM0       = 0x00000006;
    readonly config UInt32 ADC_TRIGGER_PWM1       = 0x00000007;
    readonly config UInt32 ADC_TRIGGER_PWM2       = 0x00000008;
    readonly config UInt32 ADC_TRIGGER_PWM3       = 0x00000009;
    readonly config UInt32 ADC_TRIGGER_NEVER      = 0x0000000E;
    readonly config UInt32 ADC_TRIGGER_ALWAYS     = 0x0000000F;
}
