/*
 * NegroniKeys: power and volume keys of the OnePlus 10 Pro (negroni, SM8450) for Windows.
 *
 * The keys sit on the PMIC and are read through the SPMI arbiter's observer channel (read only;
 * negroni_spmi_keys.h, shared with Mu's NegroniKeyInputDxe and the iOS kernel). There is no
 * Qualcomm PMIC stack for this phone on Windows, so the driver polls every 20 ms and reports
 * through the Virtual HID Framework (vhf.sys, lower filter per the INF):
 *   volume up / down   Consumer Control Volume Increment / Decrement (with auto-repeat)
 *   power              Generic Desktop System Sleep (display off / on)
 *
 * ACPI device NEGK0001 (negroni SSDT) has no resources: the SPMI arbiter range belongs to the
 * DSDT's SPMI device (a second claim is a resource conflict, code 12), so the two windows are
 * mapped by address; they are only read:
 *   SPMI arbiter core 0x0C400000, 0x3000 (version, APID map)
 *   observer channels 0x0C440000, 0x8000 (EE 0)
 */
#include <ntddk.h>
#include <wdf.h>
#include <hidclass.h>
#include <vhf.h>

#include "negroni_spmi_keys.h"

#define SPMI_CORE_PA		0x0C400000ULL
#define SPMI_CORE_LEN		0x3000
#define SPMI_OBS_PA		0x0C440000ULL
#define SPMI_OBS_LEN		0x8000
#define POLL_MS			20
#define REPEAT_DELAY		20	/* polls before a held volume key repeats (400 ms) */
#define REPEAT_PERIOD		4	/* then every 80 ms */
#define REPORT_ID_CONSUMER	1
#define REPORT_ID_SYSTEM	2

static UCHAR NkReportDescriptor[] = {
	0x05, 0x0C,		/* Usage Page (Consumer) */
	0x09, 0x01,		/* Usage (Consumer Control) */
	0xA1, 0x01,		/* Collection (Application) */
	0x85, REPORT_ID_CONSUMER,
	0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x02,
	0x09, 0xE9,		/*   Usage (Volume Increment) */
	0x09, 0xEA,		/*   Usage (Volume Decrement) */
	0x81, 0x02,		/*   Input (Data, Var, Abs) */
	0x95, 0x06, 0x81, 0x03,	/*   6 bits padding */
	0xC0,
	0x05, 0x01,		/* Usage Page (Generic Desktop) */
	0x09, 0x80,		/* Usage (System Control) */
	0xA1, 0x01,		/* Collection (Application) */
	0x85, REPORT_ID_SYSTEM,
	0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x01,
	0x09, 0x82,		/*   Usage (System Sleep) */
	0x81, 0x02,
	0x95, 0x07, 0x81, 0x03,	/*   7 bits padding */
	0xC0
};

typedef struct {
	VHFHANDLE Vhf;
	WDFTIMER Timer;
	struct sk_arbiter Arb;
	struct sk_keys Keys;
	struct sk_filter Filter;
	volatile UCHAR *Core, *Obs;
	SIZE_T CoreLength, ObsLength;
	BOOLEAN Ready;
	volatile LONG Polling;
	UCHAR Consumer, System;		/* last reported state */
} NK_CONTEXT;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(NK_CONTEXT, NkGetContext)

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD NkDeviceAdd;
EVT_WDF_DEVICE_PREPARE_HARDWARE NkPrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE NkReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY NkD0Entry;
EVT_WDF_DEVICE_D0_EXIT NkD0Exit;
EVT_WDF_TIMER NkPoll;

/* Stage, Status, Detail under HKLM\SYSTEM\CurrentControlSet\Services\NegroniKeys (no debugger on the phone). */
static void NkTrace(ULONG stage, NTSTATUS status, ULONG detail)
{
	ULONG v = stage;

	RtlWriteRegistryValue(RTL_REGISTRY_SERVICES, L"NegroniKeys", L"Stage", REG_DWORD, &v, sizeof(v));
	v = (ULONG)status;
	RtlWriteRegistryValue(RTL_REGISTRY_SERVICES, L"NegroniKeys", L"Status", REG_DWORD, &v, sizeof(v));
	RtlWriteRegistryValue(RTL_REGISTRY_SERVICES, L"NegroniKeys", L"Detail", REG_DWORD, &detail, sizeof(detail));
}

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
	WDF_DRIVER_CONFIG config;
	NTSTATUS status;

	WDF_DRIVER_CONFIG_INIT(&config, NkDeviceAdd);
	status = WdfDriverCreate(DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES, &config, WDF_NO_HANDLE);
	NkTrace(1, status, 0);
	return status;
}

NTSTATUS NkDeviceAdd(WDFDRIVER Driver, PWDFDEVICE_INIT DeviceInit)
{
	WDF_PNPPOWER_EVENT_CALLBACKS pnp;
	WDF_OBJECT_ATTRIBUTES attr;
	WDF_TIMER_CONFIG timerConfig;
	VHF_CONFIG vhfConfig;
	WDFDEVICE device;
	NK_CONTEXT *ctx;
	NTSTATUS status;

	UNREFERENCED_PARAMETER(Driver);
	WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnp);
	pnp.EvtDevicePrepareHardware = NkPrepareHardware;
	pnp.EvtDeviceReleaseHardware = NkReleaseHardware;
	pnp.EvtDeviceD0Entry = NkD0Entry;
	pnp.EvtDeviceD0Exit = NkD0Exit;
	WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnp);

	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attr, NK_CONTEXT);
	status = WdfDeviceCreate(&DeviceInit, &attr, &device);
	if (!NT_SUCCESS(status)) {
		NkTrace(2, status, 0);
		return status;
	}
	ctx = NkGetContext(device);

	WDF_TIMER_CONFIG_INIT(&timerConfig, NkPoll);
	timerConfig.AutomaticSerialization = FALSE;
	WDF_OBJECT_ATTRIBUTES_INIT(&attr);
	attr.ParentObject = device;
	attr.ExecutionLevel = WdfExecutionLevelPassive;
	status = WdfTimerCreate(&timerConfig, &attr, &ctx->Timer);
	if (!NT_SUCCESS(status)) {
		NkTrace(2, status, 1);
		return status;
	}

	VHF_CONFIG_INIT(&vhfConfig, WdfDeviceWdmGetDeviceObject(device),
			sizeof(NkReportDescriptor), NkReportDescriptor);
	vhfConfig.VhfClientContext = ctx;
	vhfConfig.VendorID = 0x045E;
	vhfConfig.ProductID = 0x8450;
	status = VhfCreate(&vhfConfig, &ctx->Vhf);
	NkTrace(3, status, 0);
	return status;
}

NTSTATUS NkPrepareHardware(WDFDEVICE Device, WDFCMRESLIST Raw, WDFCMRESLIST Translated)
{
	NK_CONTEXT *ctx = NkGetContext(Device);
	PHYSICAL_ADDRESS pa;

	UNREFERENCED_PARAMETER(Raw);
	UNREFERENCED_PARAMETER(Translated);
	pa.QuadPart = SPMI_CORE_PA;
	ctx->Core = MmMapIoSpaceEx(pa, SPMI_CORE_LEN, PAGE_READWRITE | PAGE_NOCACHE);
	ctx->CoreLength = SPMI_CORE_LEN;
	pa.QuadPart = SPMI_OBS_PA;
	ctx->Obs = MmMapIoSpaceEx(pa, SPMI_OBS_LEN, PAGE_READWRITE | PAGE_NOCACHE);
	ctx->ObsLength = SPMI_OBS_LEN;
	if (!ctx->Core || !ctx->Obs) {
		NkTrace(4, STATUS_INSUFFICIENT_RESOURCES, (ctx->Core ? 1 : 0) | (ctx->Obs ? 2 : 0));
		return STATUS_INSUFFICIENT_RESOURCES;
	}
	ctx->Arb.core = (volatile uint32_t *)ctx->Core;
	ctx->Arb.obs = (volatile uint32_t *)ctx->Obs;
	ctx->Arb.ee = 0;
	int rc = sk_open(&ctx->Arb, &ctx->Keys);
	NkTrace(5, rc ? STATUS_DEVICE_CONFIGURATION_ERROR : STATUS_SUCCESS,
		(ULONG)rc | ((ULONG)(ctx->Keys.pon & 0xffff) << 16));
	if (rc)
		return STATUS_DEVICE_CONFIGURATION_ERROR;
	ctx->Ready = TRUE;
	NTSTATUS status = VhfStart(ctx->Vhf);
	NkTrace(status == STATUS_SUCCESS ? 7 : 6, status, (ULONG)ctx->Keys.gpio);
	return status;
}

NTSTATUS NkReleaseHardware(WDFDEVICE Device, WDFCMRESLIST Translated)
{
	NK_CONTEXT *ctx = NkGetContext(Device);

	UNREFERENCED_PARAMETER(Translated);
	ctx->Ready = FALSE;
	if (ctx->Core)
		MmUnmapIoSpace((PVOID)ctx->Core, ctx->CoreLength);
	if (ctx->Obs)
		MmUnmapIoSpace((PVOID)ctx->Obs, ctx->ObsLength);
	ctx->Core = ctx->Obs = NULL;
	return STATUS_SUCCESS;
}

NTSTATUS NkD0Entry(WDFDEVICE Device, WDF_POWER_DEVICE_STATE Previous)
{
	NK_CONTEXT *ctx = NkGetContext(Device);

	UNREFERENCED_PARAMETER(Previous);
	InterlockedExchange(&ctx->Polling, 1);
	WdfTimerStart(ctx->Timer, WDF_REL_TIMEOUT_IN_MS(POLL_MS));
	return STATUS_SUCCESS;
}

NTSTATUS NkD0Exit(WDFDEVICE Device, WDF_POWER_DEVICE_STATE Target)
{
	NK_CONTEXT *ctx = NkGetContext(Device);

	UNREFERENCED_PARAMETER(Target);
	InterlockedExchange(&ctx->Polling, 0);
	WdfTimerStop(ctx->Timer, TRUE);
	return STATUS_SUCCESS;
}

static void NkSend(NK_CONTEXT *ctx, UCHAR id, UCHAR bits)
{
	UCHAR report[2] = { id, bits };
	HID_XFER_PACKET packet;

	packet.reportBuffer = report;
	packet.reportBufferLen = sizeof(report);
	packet.reportId = id;
	VhfReadReportSubmit(ctx->Vhf, &packet);
}

void NkPoll(WDFTIMER Timer)
{
	NK_CONTEXT *ctx = NkGetContext(WdfTimerGetParentObject(Timer));
	UCHAR sample = 0, report, consumer, system;

	if (!ctx->Ready || !ctx->Polling)
		return;
	if (sk_sample(&ctx->Arb, &ctx->Keys, &sample) != 0)
		sample = 0;
	report = sk_step(&ctx->Filter, sample, REPEAT_DELAY, REPEAT_PERIOD);
	/* Volume: a repeat is a release + press, so held keys keep stepping */
	consumer = (UCHAR)(((ctx->Filter.stable & SK_KEY_VOLUP) ? 1 : 0) | ((ctx->Filter.stable & SK_KEY_VOLDOWN) ? 2 : 0));
	if ((report & (SK_KEY_VOLUP | SK_KEY_VOLDOWN)) && consumer == ctx->Consumer)
		NkSend(ctx, REPORT_ID_CONSUMER, 0);
	if (consumer != ctx->Consumer || (report & (SK_KEY_VOLUP | SK_KEY_VOLDOWN))) {
		NkSend(ctx, REPORT_ID_CONSUMER, consumer);
		ctx->Consumer = consumer;
	}
	system = (ctx->Filter.stable & SK_KEY_POWER) ? 1 : 0;
	if (system != ctx->System) {
		NkSend(ctx, REPORT_ID_SYSTEM, system);
		ctx->System = system;
	}
	if (ctx->Polling)
		WdfTimerStart(ctx->Timer, WDF_REL_TIMEOUT_IN_MS(POLL_MS));
}
