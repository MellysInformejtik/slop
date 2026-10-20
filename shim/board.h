#pragma once
/*
 * Per-board constants, selected at build time (build.sh BOARD=...).
 *
 *   sweet   Redmi Note 10 Pro, SM7150 (default; values from the live FDT)
 *   negroni OnePlus 10 Pro 5G, SM8450 (values from the mainline device tree
 *           arch/arm64/boot/dts/qcom/sm8450{.dtsi,-oneplus-negroni.dts};
 *           not yet confirmed on hardware, see docs/negroni.md)
 */

#if defined(BOARD_NEGRONI)

#define BOARD_NAME		"negroni"
/* reserved-memory splash_region; ABL keeps the display alive only under that name */
#define FB_BASE			0xb8000000UL
#define FB_W			1080
#define FB_H			2412
#define GICD_BASE		0x17100000UL
#define GICR_BASE		0x17180000UL
#define GICR_SIZE		0x200000UL
#define GICR_PE_SIZE		0x40000UL	/* GICv4: RD, SGI, VLPI, reserved frames */
#define VTIMER_INTID		27		/* <GIC_PPI 11 ...>: the usual numbering */
#define PTIMER_INTID		30		/* <GIC_PPI 14 ...> */
/* Downstream waipio imem restart_reason@65c (inside mainline sram@146a8000). Unverified. */
#define IMEM_RESTART_REASON	0x146aa65cUL
#define PSHOLD			0		/* not in the mainline DT: PSCI reset only */

#else

#define BOARD_NAME		"sweet"
#define FB_BASE			0x9c000000UL	/* reserved-memory cont_splash_region */
#define FB_W			1080
#define FB_H			2400
#define GICD_BASE		0x17a00000UL
#define GICR_BASE		0x17a60000UL
#define GICR_SIZE		0x100000UL
#define GICR_PE_SIZE		0x20000UL
#define VTIMER_INTID		19	/* DT: <GIC_PPI 3 ...>, not the usual 27 */
#define PTIMER_INTID		18	/* DT: <GIC_PPI 2 ...> */
#define IMEM_RESTART_REASON	0x146aa65cUL	/* qcom,msm-imem restart_reason@65c */
#define PSHOLD			0x0c264000UL

#endif

#define FB_STRIDE		(FB_W * 4)	/* a8r8g8b8, matches mainline simplefb */
/* ufshc@1d84000 on both SoCs */
#define UFS_BASE		0x01d84000UL
