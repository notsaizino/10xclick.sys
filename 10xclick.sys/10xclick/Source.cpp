#include <ntddk.h>
#include<dontuse.h>
#include<ntifs.h>
#include<ntddmou.h>
/*
This driver is to "multiply" any/all left clicks made by the mouse. It does so by attaching itself to the IRP stack of the mouse, intercepting the mouse IRPs, reading 
said IRPs' events to check which how many left clicks there are, allocating memory in order to multiply these left clicks, then sending the "new" IRP down the IRP stack. 
This is extremely complex, and very risky (for my skill level). But it's a nice challenge. :)
*/

#define REAL_IRP_TAG ((PVOID)0x111EA)//this "tag" will be used as context for REAL irps. This is necessary, as the completion routine will be using this as CONTEXT when it handles REAL IRPS.

void irpcopy(PDEVICE_OBJECT DeviceObject, PVOID Context);
void clickUnload(PDRIVER_OBJECT DriverObject);
NTSTATUS OpenCloseDevice(PDEVICE_OBJECT DeviceObject, PIRP irp);
NTSTATUS ReadMouseClickInput(PDEVICE_OBJECT DeviceObject, PIRP irp);
NTSTATUS DeviceAttach(PDRIVER_OBJECT DriverObject, PDEVICE_OBJECT ActualMouse);
NTSTATUS CompletionRoutine(PDEVICE_OBJECT DeviceObject, PIRP irp, PVOID Context);
NTSTATUS FakeCompletionRoutine(PDEVICE_OBJECT DeviceObject, PIRP irp, PVOID Context);



typedef struct _DEVICE_EXTENSION { //this is a private storage box for my driver. it'll store the address of LowerDeviceObject. 

	PDEVICE_OBJECT LowerDeviceObject; //LowerDeviceObject is the next device in the stack, which is why I called it that. 

}DEVICE_EXTENSION, * PDEVICE_EXTENSION;

typedef struct _WORKITEM_CONTEXT {
	PIO_WORKITEM workItem; //must be freed later.
	LONG PositionX; //click co-ordinate.
	LONG PositionY; //click co-ordinate.
	ULONG pairs;
	PDEVICE_OBJECT LowerDeviceObject; //For IoCallDriver.
	
}WORKITEM_CONTEXT, *PWORKITEM_CONTEXT;

extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING registrypath) {
	NTSTATUS status = STATUS_SUCCESS;

	DriverObject->MajorFunction[IRP_MJ_CREATE] = OpenCloseDevice; //no handles should be oppened: I don't plan on making a symlink for this driver yet.
	DriverObject->MajorFunction[IRP_MJ_CLOSE] = OpenCloseDevice; //no handles should be closed: I don't plan on making a symlink for this driver yet.
	DriverObject->MajorFunction[IRP_MJ_READ] = ReadMouseClickInput;
	DriverObject->DriverExtension->AddDevice = DeviceAttach;
	DriverObject->DriverUnload = clickUnload;


	return status;
}
void irpcopy(PDEVICE_OBJECT DeviceObject, PVOID Context)
{
	ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);// we can never access irpcopy unless we're at PASSIVE_LEVEL. otherwise, it will crash and cause BSOD.
	PWORKITEM_CONTEXT wicontext = (PWORKITEM_CONTEXT)Context;
	if (!wicontext) {
		return;
	}

	ULONG paircount = wicontext->pairs;
	if (paircount > 5) paircount = 5; //Prevents flooding of fake IRP inputs.
	ULONG entrycount = paircount * 2;
	ULONG bytecount = entrycount * sizeof(MOUSE_INPUT_DATA);
	
	PIRP fakeirp = IoAllocateIrpEx(DeviceObject, DeviceObject->StackSize ,FALSE);//creates a new IRP. 
	if (!fakeirp) {
		IoFreeWorkItem(wicontext->workItem);
		ExFreePoolWithTag(wicontext, 'CTWI'); //frees the allocated pool for wicontext. Saves from having memory leak.
		return;
	}
	PMOUSE_INPUT_DATA buf = (PMOUSE_INPUT_DATA)ExAllocatePoolWithTag(NonPagedPoolNx, bytecount, 'BUFF'); //nonpagedpool just in case, as I will free the allocated pool in FakeCompletionRoutine.
	if (!buf) {
		IoFreeWorkItem(wicontext->workItem); //if buf allocation failed: frees work item. 
		ExFreePoolWithTag(wicontext, 'CTWI');// if buf allocation failed: free wicontext.
		return; //skip the rest.
	}

	for (ULONG i = 0; i < paircount; i++) {
		buf[2 * i].LastX = 0;
		buf[2 * i].LastY = 0;
		buf[2 * i].ButtonFlags = MOUSE_LEFT_BUTTON_DOWN;
		buf[2 * i].ExtraInformation = 0;
		buf[2 * i].Flags = MOUSE_MOVE_RELATIVE; //supposedly, some mice drivers might not register the clicks properly depending on the stack (specifically HID compliant or touchpad filter stacks).

		buf[2 * i + 1].LastX = 0;
		buf[2 * i + 1].LastY = 0;
		buf[2 * i + 1].ButtonFlags = MOUSE_LEFT_BUTTON_UP;
		buf[2 * i + 1].ExtraInformation = 0;
		buf[2 * i + 1].Flags = MOUSE_MOVE_RELATIVE;
	
	}
	fakeirp->AssociatedIrp.SystemBuffer = buf;
	fakeirp->IoStatus.Information = bytecount;//tells the I/O Manager how much data was actually written into the IRP buffer.
	fakeirp->IoStatus.Status = STATUS_SUCCESS;

	IoSetCompletionRoutine(fakeirp, FakeCompletionRoutine, wicontext, TRUE, TRUE, TRUE);
	
	IoSkipCurrentIrpStackLocation(fakeirp);
	NTSTATUS status = IoCallDriver(wicontext->LowerDeviceObject, fakeirp);
	if (!NT_SUCCESS(status)) {
		IoFreeIrp(fakeirp);
	}

	

}
/*
This is to unload the driver. Must delete all instance(s) of DeviceObject safely, free any paged memory pools,delete any references, etc.
*/
void clickUnload(PDRIVER_OBJECT DriverObject) //finished. Nothing too complex here. Just detaches then deletes the related DeviceObject/DeviceExtension. 
{
	PDEVICE_OBJECT DevObj = DriverObject->DeviceObject;
	
	while (DevObj) {
		PDEVICE_OBJECT next = DevObj->NextDevice;
		PDEVICE_EXTENSION ext = (PDEVICE_EXTENSION)DevObj->DeviceExtension;
		if (ext->LowerDeviceObject) {
			IoDetachDevice(ext->LowerDeviceObject);
		}
		
		IoDeleteDevice(DevObj);
		DevObj = next;
	}
}
/*
ReadMouseClickInput, as the name says, is used to read the left click events in the IRPs intercepted by my driver. Because it is attached to the IRP stack of the mouse through
DeviceAttach, I simply fetch the IRP's current stack Location, copy said stack to the next Device Object (hidmouse.sys) and set the completion routine for said IRP to my own.
*/
NTSTATUS ReadMouseClickInput(PDEVICE_OBJECT DeviceObject, PIRP irp)//!TODO:
{
	PDEVICE_EXTENSION deviceExtension = (PDEVICE_EXTENSION)DeviceObject->DeviceExtension;
	PIO_STACK_LOCATION stackLocation = IoGetCurrentIrpStackLocation(irp); //necessary; the LowerDeviceObject must see what my sniffer sees. 

	
	if (!irp || !stackLocation || !deviceExtension->LowerDeviceObject) { //checks if the IRP is valid. 
		KdPrint(("Error: Invalid irp (0x%08X)\n", STATUS_INVALID_PARAMETER));
		return STATUS_INVALID_PARAMETER;
	}
	IoCopyCurrentIrpStackLocationToNext(irp);//which is why we need IoCopyCurrentIrpStackLocationToNext; it copies the whole Irp stack that I'm using to the next DeviceObject 
	//thats below my DeviceObject.
	IoSetCompletionRoutine(irp, CompletionRoutine, REAL_IRP_TAG, TRUE, TRUE, TRUE); //sets the IRP's CompletionRoutine to my completion routine. The context value is "realtag", as mentionned above.
	NTSTATUS status = IoCallDriver(deviceExtension->LowerDeviceObject, irp);
	return status;
	//We don't need to check about the status of IoCallDriver, as it's out of our hands now: 
	//1. if it fails, we return status and move on. 
	//2. if it succeeds, we don't do anything; we return STATUS_SUCCESS;
	//3. if it's hanging, it's out of our hands. 
	
}
/*
DeviceAttach is the subroutine to attach the DriverObject to "LowerDeviceObject" (the mouse*). This is necessary; whenever an IRP passes by my DeviceObject, it'll 
automatically call "ReadMouseClickInput", which is "IRP_MJ_READ". 
*Note: I abstracted anything after my driver as "the mouse".
*/
NTSTATUS DeviceAttach(PDRIVER_OBJECT DriverObject, PDEVICE_OBJECT ActualMouse) //Completed.
{
	NTSTATUS status = STATUS_SUCCESS;
	PDEVICE_OBJECT sniffer = NULL; //Initializes the "sniffer", which will "sniff" IRPs. Nice name, I know ;).
	PDEVICE_OBJECT LowerDeviceObject = NULL; //Initializes the LowerDeviceObject. 
	status = IoCreateDevice(DriverObject,sizeof(DEVICE_EXTENSION), NULL, ActualMouse->DeviceType, 0, FALSE, &sniffer); //Creates the device. 
	if (!NT_SUCCESS(status)) {
		KdPrint(("Failed to create DeviceObject. Error code: (0x%08X)\n", status));
		return status;
	}
	PDEVICE_EXTENSION deviceExtension = (PDEVICE_EXTENSION)sniffer->DeviceExtension; 
	sniffer->Flags &= ~DO_DEVICE_INITIALIZING;//in order for DO_DEVICE_INITIALIZING not to clear all flags in order to initialize, we do a AND and a NOT on the flag that represents "DO_DEVICE_INITIALIZING" in order to clear it.
	sniffer->Flags |= DO_BUFFERED_IO;//DO_BUFFERED_IO:windows will copy data between usermode and a kernel buffer, making it easier to safely access user input. 
									 //This is required, as we will be modifying said buffer in the completion routine in order to multiply click events.
	

	status = IoAttachDeviceToDeviceStackSafe(sniffer, ActualMouse, &LowerDeviceObject); //attaches the deviceobject to the IRP stack of the "ActualMouse", and stores the address at LowerDeviceObject. This lets us access the IRP stack through LowerDeviceObject, instead of "ActualMouse". 
	if (!NT_SUCCESS(status)) {
		IoDeleteDevice(sniffer); //This is in case it fails. We must free the sniffer device, or it'll leak memory.
		KdPrint(("Failed to attach Sniffer to LowerDeviceObject. Error Code: (0x%08X)\n", status));
		return status;
	}
	deviceExtension->LowerDeviceObject = LowerDeviceObject; //stores the LowerDeviceObject in DeviceExtension struct, which I defined. 
	return status;


}
//TODO
NTSTATUS CompletionRoutine(PDEVICE_OBJECT DeviceObject, PIRP irp, PVOID Context)
{
	
	if (Context != REAL_IRP_TAG) { //Skip processing if not a real IRP.
		return STATUS_SUCCESS;
	}
	if (!NT_SUCCESS(irp->IoStatus.Status)) {//if the IRP was successfully completed, then I can copy it. Otherwise, no point.

		return STATUS_SUCCESS;
	}
	PIO_WORKITEM workItem = IoAllocateWorkItem(DeviceObject);
	if (workItem == NULL) { //we return status success as a bail-out; if we don't, and there's an error, it (probably) will corrupt the original IRP, because it may be aborted halfway through, which would cause lost mouse packets and/or driver crashes.
		return STATUS_SUCCESS;
	}
	
	PWORKITEM_CONTEXT wicontext = (PWORKITEM_CONTEXT)ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(WORKITEM_CONTEXT), 'CTWI');
	if (!wicontext) {
		IoFreeWorkItem(workItem);
		return STATUS_MEMORY_NOT_ALLOCATED;
	}
	
	PDEVICE_EXTENSION deviceExtension = (PDEVICE_EXTENSION)DeviceObject->DeviceExtension;

	if (!irp->AssociatedIrp.SystemBuffer) {
		IoFreeWorkItem(workItem);
		ExFreePoolWithTag(wicontext, 'CTWI');
		return STATUS_SUCCESS;
	}

	PMOUSE_INPUT_DATA mouseData = (PMOUSE_INPUT_DATA)irp->AssociatedIrp.SystemBuffer;
	ULONG inputDataSize = (ULONG)irp ->IoStatus.Information;
	ULONG entrycount = inputDataSize / sizeof(MOUSE_INPUT_DATA);

	ULONG leftclickpairs = 0;
	for (ULONG i = 0; i < entrycount; i++) {
		if (mouseData[i].ButtonFlags & MOUSE_LEFT_BUTTON_DOWN) {
			//no need to check if the next event is a matching UP if a user clicks, it releases.
			leftclickpairs++;
		}
	}
	wicontext->pairs = leftclickpairs;
	//workitem.PositionX = 0 and workitem.PositionY = 0 means that the OS will simply use the location that the mouse was last registered to be. 
	// This is because these X and Y values are
	//deltas, and not the absolute position of where they are.
	wicontext->PositionX = 0;
	wicontext->PositionY = 0;
	wicontext->workItem = workItem;
	wicontext->LowerDeviceObject = deviceExtension->LowerDeviceObject; //gets the lowerDeviceObject. Necessary to call IoCallDriver directly from irpCopy.
	//I also can't just pass the original IRP into IoQueueWorkItem, as it may be "completed"/freed at any moment. For that, I'll create a class (WorkItemCONTEXT) and take the information I need.
	
	IoQueueWorkItem(workItem, irpcopy, DelayedWorkQueue, wicontext); //this is where we will create the fake irps. Unfortunately, CompletionRoutine runs at DISPATCH_LEVEL; 
	//if i try to inject any sort of IRP, it will crash and cause an error. This is because DISPATCH_LEVEL can't be mingling in IRPs; it's basically fancy memory corruption.
	//hence, we must queue a work item, in DelayedWorkQueue (PASSIVE_LEVEL) so that we can generate our IRP using the real IRPs with the "REAL_IRP_TAG" context. 

	return STATUS_SUCCESS;
}


NTSTATUS FakeCompletionRoutine(PDEVICE_OBJECT DeviceObject, PIRP irp, PVOID Context)
{
	UNREFERENCED_PARAMETER(DeviceObject);
	PWORKITEM_CONTEXT wicontext = (PWORKITEM_CONTEXT)Context;
	if (irp->AssociatedIrp.SystemBuffer) {
		ExFreePoolWithTag(irp->AssociatedIrp.SystemBuffer, 'BUFF'); //frees the memory allocated to the buf buffer.
		irp->AssociatedIrp.SystemBuffer = NULL;
	}
	if (wicontext) {
		if (wicontext->workItem) {
			IoFreeWorkItem(wicontext->workItem);
		}
		ExFreePoolWithTag(wicontext, 'CTWI');

	}
	IoCompleteRequest(irp, IO_NO_INCREMENT);
	IoFreeIrp(irp);

	return STATUS_MORE_PROCESSING_REQUIRED; //STATUS_MORE_PROCESSING_REQUIRED tells the I/O manager “don’t touch the IRP anymore; I freed it myself.”
}
/*
* OpenCloseDevice is a simple function - we don't really care what the driver does when a handle is successfully "opened" or "closed". Windows (unfortunately) requires these to be changed, so here you go.
*/
NTSTATUS OpenCloseDevice(PDEVICE_OBJECT DeviceObject, PIRP irp) //Finished.
{
	UNREFERENCED_PARAMETER(DeviceObject);
	PDEVICE_EXTENSION deviceExtension = (PDEVICE_EXTENSION)DeviceObject->DeviceExtension; //finds LowerDeviceObject.
	IoSkipCurrentIrpStackLocation(irp); //skips my driver's location, to go to hidmouse.sys.
	return IoCallDriver(deviceExtension->LowerDeviceObject, irp); //Hands off the irp to the LowerDeviceObject's driver, aka hidmouse.sys.
}
