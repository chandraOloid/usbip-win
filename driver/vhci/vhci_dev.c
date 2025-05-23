#include "vhci.h"

#include <wdmsec.h>

#include "vhci_dev.h"

DEFINE_GUID(GUID_SD_USBIP_VHCI,
	0x9d3039dd, 0xcca5, 0x4b4d, 0xb3, 0x3d, 0xe2, 0xdd, 0xc8, 0xa8, 0xc5, 0x2f);

LPCWSTR devcodes[] = {
	L"ROOT", L"CPDO", L"VHCI", L"HPDO", L"VHUB", L"VPDO"
};

static ULONG ext_sizes_per_devtype[] = {
	sizeof(root_dev_t),
	sizeof(cpdo_dev_t),
	sizeof(vhci_dev_t),
	sizeof(hpdo_dev_t),
	sizeof(vhub_dev_t),
	sizeof(vpdo_dev_t)
};

LPWSTR
get_device_prop(PDEVICE_OBJECT pdo, DEVICE_REGISTRY_PROPERTY prop, PULONG plen)
{
	LPWSTR	value;
	ULONG	buflen;
	NTSTATUS	status;

	status = IoGetDeviceProperty(pdo, prop, 0, NULL, &buflen);
	if (status != STATUS_BUFFER_TOO_SMALL) {
		DBGE(DBG_GENERAL, "failed to get device property size: %s\n", dbg_ntstatus(status));
		return NULL;
	}
	//value = ExAllocatePoolWithTag(PagedPool, buflen, USBIP_VHCI_POOL_TAG);
	value = ExAllocatePool2(POOL_FLAG_PAGED, buflen, USBIP_VHCI_POOL_TAG);

	
	if (value == NULL) {
		DBGE(DBG_GENERAL, "failed to get device property: out of memory\n");
		return NULL;
	}
	status = IoGetDeviceProperty(pdo, prop, buflen, value, &buflen);
	if (NT_ERROR(status)) {
		DBGE(DBG_GENERAL, "failed to get device property: %s\n", dbg_ntstatus(status));
		ExFreePoolWithTag(value, USBIP_VHCI_POOL_TAG);
		return NULL;
	}
	if (plen != NULL)
		*plen = buflen;
	return value;
}

PAGEABLE PDEVICE_OBJECT
vdev_create(PDRIVER_OBJECT drvobj, vdev_type_t type)
{
	pvdev_t	vdev;
	PDEVICE_OBJECT	devobj;
	ULONG	extsize = ext_sizes_per_devtype[type];
	NTSTATUS	status;

	switch (type) {
	case VDEV_CPDO:
	case VDEV_HPDO:
	case VDEV_VPDO:
		status = IoCreateDeviceSecure(drvobj, extsize, NULL,
			FILE_DEVICE_BUS_EXTENDER, FILE_AUTOGENERATED_DEVICE_NAME | FILE_DEVICE_SECURE_OPEN,
			FALSE, &SDDL_DEVOBJ_SYS_ALL_ADM_RWX_WORLD_RWX_RES_RWX, // allow normal users to access the devices
			(LPCGUID)&GUID_SD_USBIP_VHCI, &devobj);
		break;
	default:
		status = IoCreateDevice(drvobj, extsize, NULL,
			FILE_DEVICE_BUS_EXTENDER, FILE_DEVICE_SECURE_OPEN, TRUE, &devobj);
		break;
	}
	if (!NT_SUCCESS(status)) {
		DBGE(DBG_GENERAL, "failed to create vdev(%s): status:%s\n", dbg_vdev_type(type), dbg_ntstatus(status));
		return NULL;
	}

	vdev = DEVOBJ_TO_VDEV(devobj);
	RtlZeroMemory(vdev, extsize);

	vdev->DevicePnPState = NotStarted;
	vdev->PreviousPnPState = NotStarted;

	vdev->type = type;
	vdev->Self = devobj;

	vdev->DevicePowerState = PowerDeviceUnspecified;
	vdev->SystemPowerState = PowerSystemWorking;

	devobj->Flags |= DO_POWER_PAGABLE | DO_BUFFERED_IO;

	return devobj;
}

void
vdev_add_ref(pvdev_t vdev)
{
	InterlockedIncrement(&vdev->n_refs);
}

VOID
vdev_del_ref(pvdev_t vdev)
{
	InterlockedDecrement(&vdev->n_refs);
}