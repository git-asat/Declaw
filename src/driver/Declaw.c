#include "Declaw.h"

//
// Global Driver Context
//
DECLAW_DATA g_DeclawData = { 0 };

//
// Minifilter Callbacks Registration Table
//
CONST FLT_OPERATION_REGISTRATION Callbacks[] = {
    { IRP_MJ_CREATE,
      0,
      DeclawPreCreate,
      NULL },

    { IRP_MJ_OPERATION_END }
};

//
// Filter Registration Structure
//
CONST FLT_REGISTRATION FilterRegistration = {
    sizeof(FLT_REGISTRATION),           // Size
    FLT_REGISTRATION_VERSION,           // Version
    0,                                  // Flags
    NULL,                               // Context Registration
    Callbacks,                          // Operation Registration
    DeclawUnload,                     // FilterUnloadCallback
    NULL,                               // InstanceSetupCallback
    NULL,                               // InstanceQueryTeardownCallback
    NULL,                               // InstanceTeardownStartCallback
    NULL,                               // InstanceTeardownCompleteCallback
    NULL,                               // GenerateFileNameCallback
    NULL,                               // NormalizeNameComponentCallback
    NULL                                // NormalizeContextCleanupCallback
};

//
// Driver Entry Point
//
NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
)
{
    NTSTATUS status;

    UNREFERENCED_PARAMETER(RegistryPath);

    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "[Declaw] Initializing Minifilter Driver...\n");

    //
    // BUG FIX: Use KeInitializeSpinLock for KSPIN_LOCK.
    // Previous code called KeInitializeInStackQueuedSpinLock which does not exist.
    // KeInitializeSpinLock sets the spinlock to the released state.
    //
    InitializeListHead(&g_DeclawData.PathListHead);
    KeInitializeSpinLock(&g_DeclawData.PathListLock);
    g_DeclawData.PathCount = 0;

    InitializeListHead(&g_DeclawData.PidListHead);
    KeInitializeSpinLock(&g_DeclawData.PidListLock);
    g_DeclawData.PidCount = 0;

    g_DeclawData.TotalBlockedAttempts = 0;

    // Register with Filter Manager
    status = FltRegisterFilter(
        DriverObject,
        &FilterRegistration,
        &g_DeclawData.FilterHandle
    );

    if (!NT_SUCCESS(status)) {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "[Declaw] FltRegisterFilter failed: 0x%08X\n", status);
        return status;
    }

    // Initialize Filter Communication Port
    status = DeclawPortInitialize(g_DeclawData.FilterHandle);
    if (!NT_SUCCESS(status)) {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "[Declaw] DeclawPortInitialize failed: 0x%08X\n", status);
        FltUnregisterFilter(g_DeclawData.FilterHandle);
        return status;
    }

    // Start filtering I/O
    status = FltStartFiltering(g_DeclawData.FilterHandle);
    if (!NT_SUCCESS(status)) {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "[Declaw] FltStartFiltering failed: 0x%08X\n", status);
        DeclawPortCleanup();
        FltUnregisterFilter(g_DeclawData.FilterHandle);
        return status;
    }

    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "[Declaw] Minifilter started successfully.\n");
    return STATUS_SUCCESS;
}

//
// Driver Unload Callback
//
// BUG FIX: Removed PAGED_CODE() — this function acquires spinlocks which
// raise IRQL to DISPATCH_LEVEL. PAGED_CODE() asserts IRQL < DISPATCH_LEVEL
// and would bugcheck (BSOD) on a checked/debug build.
//
NTSTATUS
DeclawUnload(
    _In_ FLT_FILTER_UNLOAD_FLAGS Flags
)
{
    KLOCK_QUEUE_HANDLE lockHandle;
    PLIST_ENTRY curr = NULL;
    LIST_ENTRY pathsToFree;
    LIST_ENTRY pidsToFree;

    UNREFERENCED_PARAMETER(Flags);

    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "[Declaw] Unloading Minifilter Driver...\n");

    // Close communication port first to prevent new messages arriving
    DeclawPortCleanup();

    // Unregister filter — this waits for all in-flight callbacks to drain
    if (g_DeclawData.FilterHandle != NULL) {
        FltUnregisterFilter(g_DeclawData.FilterHandle);
        g_DeclawData.FilterHandle = NULL;
    }

    // Free protected paths list (hold lock briefly, free outside)
    InitializeListHead(&pathsToFree);
    KeAcquireInStackQueuedSpinLock(&g_DeclawData.PathListLock, &lockHandle);
    while (!IsListEmpty(&g_DeclawData.PathListHead)) {
        curr = RemoveHeadList(&g_DeclawData.PathListHead);
        InsertTailList(&pathsToFree, curr);
    }
    g_DeclawData.PathCount = 0;
    KeReleaseInStackQueuedSpinLock(&lockHandle);

    while (!IsListEmpty(&pathsToFree)) {
        curr = RemoveHeadList(&pathsToFree);
        ExFreePoolWithTag(CONTAINING_RECORD(curr, PROTECTED_PATH_ENTRY, ListEntry), DECLAW_POOL_TAG);
    }

    // Free blocked PIDs list
    InitializeListHead(&pidsToFree);
    KeAcquireInStackQueuedSpinLock(&g_DeclawData.PidListLock, &lockHandle);
    while (!IsListEmpty(&g_DeclawData.PidListHead)) {
        curr = RemoveHeadList(&g_DeclawData.PidListHead);
        InsertTailList(&pidsToFree, curr);
    }
    g_DeclawData.PidCount = 0;
    KeReleaseInStackQueuedSpinLock(&lockHandle);

    while (!IsListEmpty(&pidsToFree)) {
        curr = RemoveHeadList(&pidsToFree);
        ExFreePoolWithTag(CONTAINING_RECORD(curr, BLOCKED_PID_ENTRY, ListEntry), DECLAW_POOL_TAG);
    }

    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "[Declaw] Unloaded cleanly.\n");
    return STATUS_SUCCESS;
}

//
// Pre-Create Callback (IRP_MJ_CREATE)
//
// This runs in the context of the calling thread, so PsGetCurrentProcessId()
// gives us the correct requesting process.
//
FLT_PREOP_CALLBACK_STATUS
DeclawPreCreate(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID *CompletionContext
)
{
    HANDLE processId;
    PFLT_FILE_NAME_INFORMATION nameInfo = NULL;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(FltObjects);
    *CompletionContext = NULL;

    // Ignore kernel-mode requests to avoid self-deadlocks and system crashes
    if (Data->RequestorMode == KernelMode) {
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    }

    //
    // BUG FIX: FltGetRequestorProcessId() does not exist in standard WDK.
    // Use PsGetCurrentProcessId() — the pre-create callback always runs in
    // the calling thread's context, so this is the correct PID.
    //
    processId = PsGetCurrentProcessId();
    if (processId == NULL) {
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    }

    //
    // Quick Path: Is this PID blocked? If not, skip immediately.
    // This check avoids the expensive FltGetFileNameInformation call for
    // every single file open on the system — critical for performance.
    //
    if (!DeclawIsProcessBlocked(processId)) {
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    }

    //
    // Process is flagged as AI! Retrieve and normalize the target file path.
    //
    status = FltGetFileNameInformation(
        Data,
        FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_DEFAULT,
        &nameInfo
    );

    if (!NT_SUCCESS(status)) {
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    }

    status = FltParseFileNameInformation(nameInfo);
    if (!NT_SUCCESS(status)) {
        FltReleaseFileNameInformation(nameInfo);
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    }

    //
    // Check if the target path falls inside any protected directory
    //
    if (DeclawIsPathProtected(&nameInfo->Name)) {
        InterlockedIncrement(&g_DeclawData.TotalBlockedAttempts);

        DbgPrintEx(
            DPFLTR_IHVDRIVER_ID,
            DPFLTR_WARNING_LEVEL,
            "[Declaw] BLOCKED! PID=%lu path=%wZ\n",
            HandleToUlong(processId),
            &nameInfo->Name
        );

        FltReleaseFileNameInformation(nameInfo);

        // Deny the file open
        Data->IoStatus.Status = STATUS_ACCESS_DENIED;
        Data->IoStatus.Information = 0;
        return FLT_PREOP_COMPLETE;
    }

    FltReleaseFileNameInformation(nameInfo);
    return FLT_PREOP_SUCCESS_NO_CALLBACK;
}
