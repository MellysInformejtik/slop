/*
 * SM8450 RPMh VRM vote (apps RSC, DRV2, one ACTIVE TCS, polled), for the touch supplies L6C (1.8 V)
 * and L3C (3.0 V) of the S3908. Port of ios-sweet xnu patch 0062 bsd/dev/sweet_rpmh.c (SM7150 L11C).
 *
 * Layout from Linux v6.12 sm8450.dtsi apps_rsc: drv-2 at 0x17a20000, qcom,tcs-offset 0xd00,
 * tcs-config ACTIVE 3 / SLEEP 2 / WAKE 2 / CONTROL 0 (ACTIVE = TCS 0..2), pre-v3 register layout
 * (drivers/soc/qcom/rpmh-rsc.c); cmd-db reserved memory aop_cmd_db_mem at 0x80860000.
 * Resource names (ldoc3, ldoc6) and voltages from the negroni downstream DT (rpmh-regulator-ldoc*,
 * synaptics20031@4B vdd_2v8_volt = 3008000).
 *
 * The vote is an ACTIVE-only AMC request: it stays in force after ExitBootServices until the same
 * DRV votes again, so an OS without an RPMh driver (WinPE) finds the rails on.
 * Freestanding: no allocation, MMIO through physical addresses (identity mapped in UEFI).
 */
#ifndef NEGRONI_RPMH_H
#define NEGRONI_RPMH_H

#include <stdint.h>

#define RH_DRV_BASE	0x17a20000u
#define RH_TCS_BASE	(RH_DRV_BASE + 0xd00u)
#define RH_DRV		2u
#define RH_ACTIVE_N	3u
#define RH_CMD_DB	0x80860000u
#define RH_CMD_DB_SIZE	0x20000u
#define RH_TCS_STRIDE	0x2a0u
#define RH_CMD_STRIDE	0x14u
#define RH_IRQ_EN	0x00u
#define RH_IRQ_STATUS	0x04u
#define RH_IRQ_CLEAR	0x08u
#define RH_WAIT		0x10u
#define RH_CONTROL	0x14u
#define RH_STATUS	0x18u
#define RH_ENABLE	0x1cu
#define RH_MSGID	0x30u
#define RH_ADDR		0x34u
#define RH_DATA		0x38u
#define RH_CMD_STATUS	0x3cu
#define RH_AMC_EN	(1u << 16)
#define RH_TRIGGER	(1u << 24)
#define RH_ISSUED	(1u << 8)
#define RH_COMPLETE	(1u << 16)
#define RH_WRITE_ACK	(8u | (1u << 8) | (1u << 16))
#define RH_DB_MAGIC	0x0c0330dbu
#define RH_DB_HEADER	144u
#define RH_DB_ENTRY	24u
#define RH_VRM		4u
#define RH_PMIC5_HPM	7u
#define RH_TIMEOUT_US	100000u

/* results: 0 ok; negative = stage that refused (no write was made before RH_E_SEND) */
#define RH_E_GEOMETRY	-1	/* RSC id/config not a pre-v3 DRV2 with 3 ACTIVE TCS */
#define RH_E_CMDDB	-2	/* cmd-db header (magic/version) */
#define RH_E_LOOKUP	-3	/* resource not found exactly once as a VRM entry */
#define RH_E_BUSY	-4	/* no free ACTIVE TCS, or the resource is already in flight */
#define RH_E_SEND	-5	/* register readback mismatch while programming */
#define RH_E_TIMEOUT	-6	/* AMC did not complete; the TCS is left as is */

struct rh_diag {
	uint32_t id, config, tcs_count, ncpt, db_version, address, tcs, irq, idle, status[3];
};

static inline uint32_t rh_rd(uint32_t pa)
{
	uint32_t v = *(volatile uint32_t *)(uintptr_t)pa;
	__asm__ volatile("dmb oshld" ::: "memory");
	return v;
}

static inline void rh_wr(uint32_t pa, uint32_t v)
{
	__asm__ volatile("dmb oshst" ::: "memory");
	*(volatile uint32_t *)(uintptr_t)pa = v;
	__asm__ volatile("dsb osh" ::: "memory");
}

static inline int rh_sync(uint32_t pa, uint32_t v, void (*udelay)(unsigned))
{
	rh_wr(pa, v);
	for (unsigned i = 0; i < 1000; i++) {
		if (rh_rd(pa) == v)
			return 1;
		udelay(1);
	}
	return 0;
}

static inline uint16_t rh_db16(uint32_t off)
{
	uint32_t v = rh_rd(RH_CMD_DB + (off & ~3u));
	return (uint16_t)(v >> (8u * (off & 3u)));
}

static inline int rh_lookup(const char *res, struct rh_diag *d)
{
	uint32_t key[2] = { 0, 0 };
	unsigned matches = 0;

	for (unsigned i = 0; i < 8 && res[i]; i++)
		key[i / 4] |= (uint32_t)(uint8_t)res[i] << (8u * (i % 4));
	for (unsigned i = 0; i < 8; i++) {
		uint32_t h = 8u + 16u * i;
		unsigned slave = rh_db16(h);
		if (!slave)
			break;
		uint32_t entries = RH_DB_HEADER + rh_db16(h + 2);
		unsigned count = rh_db16(h + 6);
		if (entries + count * RH_DB_ENTRY > RH_CMD_DB_SIZE)
			return RH_E_CMDDB;
		for (unsigned j = 0; j < count; j++) {
			uint32_t e = RH_CMD_DB + entries + RH_DB_ENTRY * j;
			if (rh_rd(e) != key[0] || rh_rd(e + 4) != key[1])
				continue;
			uint32_t addr = rh_rd(e + 16);
			if (slave != RH_VRM || (addr >> 16) != RH_VRM || (addr & 15u))
				return RH_E_LOOKUP;
			d->address = addr;
			matches++;
		}
	}
	return matches == 1 ? 0 : RH_E_LOOKUP;
}

static inline int rh_tcs_free(unsigned t)
{
	uint32_t b = RH_TCS_BASE + t * RH_TCS_STRIDE;
	uint32_t idle = rh_rd(b + RH_STATUS);
	return idle != 0 && idle != 0xffffffffu && rh_rd(b + RH_CONTROL) == 0 &&
	       rh_rd(b + RH_ENABLE) == 0 && rh_rd(b + RH_WAIT) == 0 &&
	       !(rh_rd(RH_TCS_BASE + RH_IRQ_STATUS) & (1u << t));
}

/* Vote resource res (e.g. "ldoc3") on at mv millivolts, PMIC5 LDO high-power mode. */
static inline int rh_vreg_enable(const char *res, unsigned mv, void (*udelay)(unsigned), struct rh_diag *d)
{
	d->id = rh_rd(RH_DRV_BASE);
	d->config = rh_rd(RH_DRV_BASE + 0x0c);
	d->tcs_count = (d->config >> (6u * RH_DRV)) & 0x3fu;
	d->ncpt = (d->config >> 27) & 0x1fu;
	unsigned major = (d->id >> 16) & 0xffu;
	if ((major != 1 && major != 2) || d->config == 0xffffffffu || d->tcs_count < RH_ACTIVE_N ||
	    d->tcs_count > 0x3000u / RH_TCS_STRIDE || d->ncpt < 3 || d->ncpt > 16)
		return RH_E_GEOMETRY;

	d->db_version = rh_rd(RH_CMD_DB);
	if (rh_rd(RH_CMD_DB + 4) != RH_DB_MAGIC)
		return RH_E_CMDDB;
	int rc = rh_lookup(res, d);
	if (rc)
		return rc;

	int chosen = -1;
	for (unsigned t = 0; t < RH_ACTIVE_N; t++) {
		uint32_t b = RH_TCS_BASE + t * RH_TCS_STRIDE;
		uint32_t enabled = rh_rd(b + RH_ENABLE);
		for (unsigned c = 0; c < d->ncpt; c++)
			if ((enabled & (1u << c)) && (rh_rd(b + RH_ADDR + c * RH_CMD_STRIDE) & ~15u) == d->address)
				return RH_E_BUSY;
		if (chosen < 0 && rh_tcs_free(t))
			chosen = (int)t;
	}
	if (chosen < 0)
		return RH_E_BUSY;

	unsigned t = (unsigned)chosen;
	uint32_t b = RH_TCS_BASE + t * RH_TCS_STRIDE, bit = 1u << t;
	uint32_t irq_en = rh_rd(RH_TCS_BASE + RH_IRQ_EN);
	d->tcs = t;
	if (!rh_sync(RH_TCS_BASE + RH_IRQ_EN, irq_en & ~bit, udelay))
		return RH_E_SEND;

	/* voltage, mode, then enable (rpmh-regulator order) */
	const uint32_t offsets[3] = { 0, 8, 4 };
	const uint32_t values[3] = { mv, RH_PMIC5_HPM, 1 };
	for (unsigned c = 0; c < 3; c++) {
		uint32_t cmd = b + c * RH_CMD_STRIDE;
		if (!rh_sync(cmd + RH_MSGID, RH_WRITE_ACK, udelay) ||
		    !rh_sync(cmd + RH_ADDR, d->address + offsets[c], udelay) ||
		    !rh_sync(cmd + RH_DATA, values[c], udelay))
			return RH_E_SEND;
	}
	if (!rh_sync(b + RH_WAIT, 7, udelay) || !rh_sync(b + RH_ENABLE, 7, udelay) ||
	    !rh_sync(b + RH_CONTROL, 0, udelay) || !rh_sync(b + RH_CONTROL, RH_AMC_EN, udelay))
		return RH_E_SEND;
	rh_wr(b + RH_CONTROL, RH_AMC_EN | RH_TRIGGER);

	int done = 0, progress = 0;
	for (unsigned us = 0; us < RH_TIMEOUT_US; us++) {
		int complete = 1;
		d->irq = rh_rd(RH_TCS_BASE + RH_IRQ_STATUS);
		d->idle = rh_rd(b + RH_STATUS);
		for (unsigned c = 0; c < 3; c++) {
			d->status[c] = rh_rd(b + RH_CMD_STATUS + c * RH_CMD_STRIDE);
			complete &= d->status[c] != 0xffffffffu &&
				    (d->status[c] & (RH_ISSUED | RH_COMPLETE)) == (RH_ISSUED | RH_COMPLETE);
		}
		progress |= d->idle == 0 || !complete;
		if (d->idle && d->idle != 0xffffffffu && ((d->irq & bit) || progress) && complete) {
			done = 1;
			break;
		}
		udelay(1);
	}
	if (!done)
		return RH_E_TIMEOUT;

	/* free only our TCS: trigger off, commands off, W1C our IRQ bit, restore its enable */
	if (!rh_sync(b + RH_CONTROL, RH_AMC_EN, udelay) || !rh_sync(b + RH_CONTROL, 0, udelay) ||
	    !rh_sync(b + RH_ENABLE, 0, udelay) || !rh_sync(b + RH_WAIT, 0, udelay))
		return RH_E_SEND;
	rh_wr(RH_TCS_BASE + RH_IRQ_CLEAR, bit);
	rh_sync(RH_TCS_BASE + RH_IRQ_EN, (rh_rd(RH_TCS_BASE + RH_IRQ_EN) & ~bit) | (irq_en & bit), udelay);
	return 0;
}

#endif
