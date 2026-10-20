/*
 * NegroniTouch: Synaptics S3908 touchscreen of the OnePlus 10 Pro (negroni, SM8450) for Windows.
 *
 * There is no Qualcomm I2C/GPIO/clock stack for SM8450 on Windows, so this driver talks to the
 * hardware itself: the GENI serial engine QUP0 SE4 as an I2C master (drivers/negroni-touch/geni_i2c.h)
 * and the TouchComm protocol (syna_tcm.h). Mu's NegroniTouchDxe leaves the SE clocks, the pin mux,
 * the 1.8 V enable and the reset line set up at ExitBootServices.
 *
 * ACPI device NEGT0001 (negroni SSDT) gives two memory resources:
 *   0: QUP0 SE4 registers 0x00990000, 0x4000
 *   1: TLMM page of GPIO21 (ATTN, active low) 0x0F115000, 0x1000
 * The ATTN line is polled (no GPIO controller driver), 8 ms period, at PASSIVE_LEVEL.
 * Input goes to HID class through the Virtual HID Framework (vhf.sys, a lower filter per the INF).
 */
#include <ntddk.h>
#include <wdf.h>
#include <hidclass.h>
#include <vhf.h>

#include "geni_i2c.h"
#include "syna_tcm.h"

#define S3908_ADDR		0x4b
#define POLL_MS			8
#define REPORT_ID_TOUCH		1
#define REPORT_ID_MAX_COUNT	2
#define NT_CONTACTS		TCM_MAX_OBJECTS

/* HID touch screen, 10 contacts in one report (parallel mode). Per contact: tip, confidence, id, x, y. */
#define NT_FINGER \
	0x09, 0x22,		/*   Usage (Finger) */ \
	0xA1, 0x02,		/*   Collection (Logical) */ \
	0x09, 0x42,		/*     Usage (Tip Switch) */ \
	0x09, 0x47,		/*     Usage (Confidence) */ \
	0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x02, 0x81, 0x02, \
	0x95, 0x06, 0x81, 0x03,	/*     6 bits padding */ \
	0x09, 0x51,		/*     Usage (Contact Identifier) */ \
	0x25, 0x7F, 0x75, 0x08, 0x95, 0x01, 0x81, 0x02, \
	0x05, 0x01,		/*     Usage Page (Generic Desktop) */ \
	0x26, 0x7F, 0x16,	/*     Logical Maximum (5759) */ \
	0x75, 0x10, 0x55, 0x0E, 0x65, 0x11,	/* 16 bits, exponent -2, unit cm */ \
	0x35, 0x00, 0x46, 0xB9, 0x02,	/*     Physical Maximum (697: 69.7 mm) */ \
	0x09, 0x30, 0x81, 0x02,	/*     Usage (X) */ \
	0x26, 0x3F, 0x32,	/*     Logical Maximum (12863) */ \
	0x46, 0x15, 0x06,	/*     Physical Maximum (1557: 155.7 mm) */ \
	0x09, 0x31, 0x81, 0x02,	/*     Usage (Y) */ \
	0x45, 0x00, 0x55, 0x00, 0x65, 0x00, \
	0x05, 0x0D,		/*     Usage Page (Digitizer) */ \
	0xC0			/*   End Collection */

static UCHAR NtReportDescriptor[] = {
	0x05, 0x0D,		/* Usage Page (Digitizer) */
	0x09, 0x04,		/* Usage (Touch Screen) */
	0xA1, 0x01,		/* Collection (Application) */
	0x85, REPORT_ID_TOUCH,
	NT_FINGER, NT_FINGER, NT_FINGER, NT_FINGER, NT_FINGER,
	NT_FINGER, NT_FINGER, NT_FINGER, NT_FINGER, NT_FINGER,
	0x09, 0x54,		/*   Usage (Contact Count) */
	0x25, 0x7F, 0x75, 0x08, 0x95, 0x01, 0x81, 0x02,
	0x85, REPORT_ID_MAX_COUNT,
	0x09, 0x55,		/*   Usage (Contact Count Maximum) */
	0x25, 0x0A, 0x75, 0x08, 0x95, 0x01, 0xB1, 0x02,
	0xC0			/* End Collection */
};

#pragma pack(push, 1)
typedef struct {
	UCHAR Flags;		/* bit 0 tip, bit 1 confidence */
	UCHAR Id;
	USHORT X;
	USHORT Y;
} NT_CONTACT;

typedef struct {
	UCHAR ReportId;
	NT_CONTACT Contact[NT_CONTACTS];
	UCHAR Count;
} NT_TOUCH_REPORT;
#pragma pack(pop)

typedef struct {
	VHFHANDLE Vhf;
	WDFTIMER Timer;
	struct geni_i2c I2c;
	struct tcm Tcm;
	volatile UCHAR *Se;
	volatile UCHAR *GpioPage;	/* GPIO21 control page; io register at +4 */
	SIZE_T SeLength, GpioLength;
	BOOLEAN Ready;
	ULONG Down;			/* contact bitmap reported down last frame */
} NT_CONTEXT;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(NT_CONTEXT, NtGetContext)

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD NtDeviceAdd;
EVT_WDF_DEVICE_PREPARE_HARDWARE NtPrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE NtReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY NtD0Entry;
EVT_WDF_DEVICE_D0_EXIT NtD0Exit;
EVT_WDF_TIMER NtPoll;
EVT_VHF_ASYNC_OPERATION NtGetFeature;

static int NtI2cRead(void *ctx, uint8_t *buf, uint32_t len) { return geni_i2c_read(ctx, S3908_ADDR, buf, len); }
static int NtI2cWrite(void *ctx, const uint8_t *buf, uint32_t len) { return geni_i2c_write(ctx, S3908_ADDR, buf, len); }
static void NtWait1ms(void) { KeStallExecutionProcessor(1000); }

static BOOLEAN NtAttention(NT_CONTEXT *ctx)
{
	return (*(volatile ULONG *)(ctx->GpioPage + 4) & 1) == 0;	/* ATTN active low */
}

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
	WDF_DRIVER_CONFIG config;

	WDF_DRIVER_CONFIG_INIT(&config, NtDeviceAdd);
	return WdfDriverCreate(DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES, &config, WDF_NO_HANDLE);
}

NTSTATUS NtDeviceAdd(WDFDRIVER Driver, PWDFDEVICE_INIT DeviceInit)
{
	WDF_PNPPOWER_EVENT_CALLBACKS pnp;
	WDF_OBJECT_ATTRIBUTES attr;
	WDF_TIMER_CONFIG timerConfig;
	VHF_CONFIG vhfConfig;
	WDFDEVICE device;
	NT_CONTEXT *ctx;
	NTSTATUS status;

	UNREFERENCED_PARAMETER(Driver);
	WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnp);
	pnp.EvtDevicePrepareHardware = NtPrepareHardware;
	pnp.EvtDeviceReleaseHardware = NtReleaseHardware;
	pnp.EvtDeviceD0Entry = NtD0Entry;
	pnp.EvtDeviceD0Exit = NtD0Exit;
	WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnp);

	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attr, NT_CONTEXT);
	status = WdfDeviceCreate(&DeviceInit, &attr, &device);
	if (!NT_SUCCESS(status))
		return status;
	ctx = NtGetContext(device);

	/* Passive-level periodic poll: the GENI transfers spin on status registers */
	WDF_TIMER_CONFIG_INIT_PERIODIC(&timerConfig, NtPoll, POLL_MS);
	WDF_OBJECT_ATTRIBUTES_INIT(&attr);
	attr.ParentObject = device;
	attr.ExecutionLevel = WdfExecutionLevelPassive;
	status = WdfTimerCreate(&timerConfig, &attr, &ctx->Timer);
	if (!NT_SUCCESS(status))
		return status;

	VHF_CONFIG_INIT(&vhfConfig, WdfDeviceWdmGetDeviceObject(device),
			sizeof(NtReportDescriptor), NtReportDescriptor);
	vhfConfig.VhfClientContext = ctx;
	vhfConfig.VendorID = 0x06CB;		/* Synaptics */
	vhfConfig.ProductID = 0x3908;
	vhfConfig.EvtVhfAsyncOperationGetFeature = NtGetFeature;
	return VhfCreate(&vhfConfig, &ctx->Vhf);	/* started in NtPrepareHardware, once the S3908 answers */
}

NTSTATUS NtPrepareHardware(WDFDEVICE Device, WDFCMRESLIST Raw, WDFCMRESLIST Translated)
{
	NT_CONTEXT *ctx = NtGetContext(Device);
	ULONG n = WdfCmResourceListGetCount(Translated), mem = 0;

	UNREFERENCED_PARAMETER(Raw);
	for (ULONG i = 0; i < n; i++) {
		PCM_PARTIAL_RESOURCE_DESCRIPTOR d = WdfCmResourceListGetDescriptor(Translated, i);
		PVOID va;

		if (d->Type != CmResourceTypeMemory)
			continue;
		va = MmMapIoSpaceEx(d->u.Memory.Start, d->u.Memory.Length, PAGE_READWRITE | PAGE_NOCACHE);
		if (!va)
			return STATUS_INSUFFICIENT_RESOURCES;
		if (mem == 0) {
			ctx->Se = va;
			ctx->SeLength = d->u.Memory.Length;
		} else if (mem == 1) {
			ctx->GpioPage = va;
			ctx->GpioLength = d->u.Memory.Length;
		} else {
			MmUnmapIoSpace(va, d->u.Memory.Length);
		}
		mem++;
	}
	if (!ctx->Se || !ctx->GpioPage)
		return STATUS_DEVICE_CONFIGURATION_ERROR;

	ctx->I2c.base = ctx->Se;
	ctx->I2c.polls = 200000;
	if (geni_i2c_init(&ctx->I2c) != GENI_I2C_OK) {
		KdPrint(("NegroniTouch: SE4 is not in I2C mode (proto %u)\n", geni_i2c_proto(&ctx->I2c)));
		return STATUS_DEVICE_CONFIGURATION_ERROR;
	}
	ctx->Tcm.io.ctx = &ctx->I2c;
	ctx->Tcm.io.read = NtI2cRead;
	ctx->Tcm.io.write = NtI2cWrite;
	/* drain what is pending (the IDENTIFY report of the reset done by UEFI), then fetch the report layout */
	for (int i = 0; i < 16 && tcm_read_message(&ctx->Tcm) == 0; i++)
		;
	if (tcm_get_report_config(&ctx->Tcm, NtWait1ms) != 0) {
		KdPrint(("NegroniTouch: no touch report config from the S3908\n"));
		return STATUS_DEVICE_NOT_READY;
	}
	ctx->Ready = TRUE;
	return VhfStart(ctx->Vhf);
}

NTSTATUS NtReleaseHardware(WDFDEVICE Device, WDFCMRESLIST Translated)
{
	NT_CONTEXT *ctx = NtGetContext(Device);

	UNREFERENCED_PARAMETER(Translated);
	ctx->Ready = FALSE;
	if (ctx->Se)
		MmUnmapIoSpace((PVOID)ctx->Se, ctx->SeLength);
	if (ctx->GpioPage)
		MmUnmapIoSpace((PVOID)ctx->GpioPage, ctx->GpioLength);
	ctx->Se = ctx->GpioPage = NULL;
	return STATUS_SUCCESS;
}

NTSTATUS NtD0Entry(WDFDEVICE Device, WDF_POWER_DEVICE_STATE Previous)
{
	UNREFERENCED_PARAMETER(Previous);
	WdfTimerStart(NtGetContext(Device)->Timer, WDF_REL_TIMEOUT_IN_MS(POLL_MS));
	return STATUS_SUCCESS;
}

NTSTATUS NtD0Exit(WDFDEVICE Device, WDF_POWER_DEVICE_STATE Target)
{
	UNREFERENCED_PARAMETER(Target);
	WdfTimerStop(NtGetContext(Device)->Timer, TRUE);
	return STATUS_SUCCESS;
}

static void NtSubmit(NT_CONTEXT *ctx)
{
	NT_TOUCH_REPORT report;
	HID_XFER_PACKET packet;
	ULONG down = 0;
	UCHAR count = 0;

	RtlZeroMemory(&report, sizeof(report));
	report.ReportId = REPORT_ID_TOUCH;
	for (ULONG i = 0; i < NT_CONTACTS; i++) {
		const struct tcm_object *o = &ctx->Tcm.obj[i];
		BOOLEAN isDown = o->status != 0;

		if (!isDown && !(ctx->Down & (1u << i)))
			continue;			/* not down now and not down before */
		report.Contact[count].Flags = isDown ? 0x03 : 0x02;	/* tip + confidence / lift */
		report.Contact[count].Id = (UCHAR)i;
		report.Contact[count].X = (USHORT)min(o->x, TCM_PANEL_MAX_X - 1);
		report.Contact[count].Y = (USHORT)min(o->y, TCM_PANEL_MAX_Y - 1);
		count++;
		if (isDown)
			down |= 1u << i;
	}
	ctx->Down = down;
	if (!count)
		return;
	report.Count = count;
	packet.reportBuffer = (PUCHAR)&report;
	packet.reportBufferLen = sizeof(report);
	packet.reportId = REPORT_ID_TOUCH;
	(void)VhfReadReportSubmit(ctx->Vhf, &packet);
}

void NtPoll(WDFTIMER Timer)
{
	NT_CONTEXT *ctx = NtGetContext(WdfTimerGetParentObject(Timer));

	if (!ctx->Ready)
		return;
	for (int i = 0; i < 4 && NtAttention(ctx); i++) {	/* a few queued reports per tick at most */
		if (tcm_read_message(&ctx->Tcm) != 0)
			break;
		if (ctx->Tcm.code == TCM_REPORT_TOUCH && tcm_parse_touch(&ctx->Tcm) == 0)
			NtSubmit(ctx);
	}
}

void NtGetFeature(PVOID Context, VHFOPERATIONHANDLE Op, PVOID OpContext, PHID_XFER_PACKET Packet)
{
	NTSTATUS status = STATUS_INVALID_PARAMETER;

	UNREFERENCED_PARAMETER(Context);
	UNREFERENCED_PARAMETER(OpContext);
	if (Packet->reportId == REPORT_ID_MAX_COUNT && Packet->reportBufferLen >= 2) {
		Packet->reportBuffer[0] = REPORT_ID_MAX_COUNT;
		Packet->reportBuffer[1] = NT_CONTACTS;
		status = STATUS_SUCCESS;
	}
	VhfAsyncOperationComplete(Op, status);
}
