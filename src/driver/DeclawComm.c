#include "Declaw.h"

//
// Port Security Descriptor and Attribute
//
NTSTATUS
DeclawPortInitialize(
    _In_ PFLT_FILTER Filter
)
{
    NTSTATUS status;
    PSECURITY_DESCRIPTOR sd = NULL;
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING uniString;

    PAGED_CODE();

    //
    // Create default security descriptor allowing Admins and System to connect
    //
    status = FltBuildDefaultSecurityDescriptor(&sd, FLT_PORT_ALL_ACCESS);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    RtlInitUnicodeString(&uniString, DECLAW_PORT_NAME);

    InitializeObjectAttributes(
        &oa,
        &uniString,
        OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
        NULL,
        sd
    );

    status = FltCreateCommunicationPort(
        Filter,
        &g_DeclawData.ServerPort,
        &oa,
        NULL,
        DeclawPortConnect,
        DeclawPortDisconnect,
        DeclawPortMessage,
        1 // Max connections
    );

    FltFreeSecurityDescriptor(sd);
    return status;
}

VOID
DeclawPortCleanup(
    VOID
)
{
    PAGED_CODE();

    if (g_DeclawData.ServerPort != NULL) {
        FltCloseCommunicationPort(g_DeclawData.ServerPort);
        g_DeclawData.ServerPort = NULL;
    }
}

NTSTATUS
DeclawPortConnect(
    _In_ PFLT_PORT ClientPort,
    _In_opt_ PVOID ServerPortCookie,
    _In_reads_bytes_opt_(SizeOfContext) PVOID ConnectionContext,
    _In_ ULONG SizeOfContext,
    _Outptr_result_maybenull_ PVOID *ConnectionPortCookie
)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(ServerPortCookie);
    UNREFERENCED_PARAMETER(ConnectionContext);
    UNREFERENCED_PARAMETER(SizeOfContext);

    *ConnectionPortCookie = NULL;
    g_DeclawData.ClientPort = ClientPort;
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "[Declaw] User-mode client connected.\n");

    return STATUS_SUCCESS;
}

VOID
DeclawPortDisconnect(
    _In_opt_ PVOID ConnectionCookie
)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(ConnectionCookie);

    if (g_DeclawData.ClientPort != NULL) {
        FltCloseClientPort(g_DeclawData.FilterHandle, &g_DeclawData.ClientPort);
        g_DeclawData.ClientPort = NULL;
    }

    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "[Declaw] User-mode client disconnected.\n");
}

NTSTATUS
DeclawPortMessage(
    _In_opt_ PVOID PortCookie,
    _In_reads_bytes_opt_(InputBufferSize) PVOID InputBuffer,
    _In_ ULONG InputBufferSize,
    _Out_writes_bytes_to_opt_(OutputBufferSize, *ReturnOutputBufferLength) PVOID OutputBuffer,
    _In_ ULONG OutputBufferSize,
    _Out_ PULONG ReturnOutputBufferLength
)
{
    NTSTATUS status = STATUS_SUCCESS;
    PDECLAW_COMMAND_MESSAGE cmdMsg = (PDECLAW_COMMAND_MESSAGE)InputBuffer;
    KLOCK_QUEUE_HANDLE lockHandle;

    PAGED_CODE();
    UNREFERENCED_PARAMETER(PortCookie);

    //
    // BUG FIX: Validate InputBufferSize against the minimum command size (4 bytes for Command enum).
    // The full struct is large because of the union, but for simple commands like CmdGetStats
    // or CmdClearProtectedPaths we only need the command field. However, the user-mode client
    // always sends the full struct, so checking sizeof(DECLAW_COMMAND_MESSAGE) is fine.
    //
    if (InputBuffer == NULL || InputBufferSize < sizeof(DECLAW_COMMAND_MESSAGE)) {
        if (ReturnOutputBufferLength != NULL) {
            *ReturnOutputBufferLength = 0;
        }
        return STATUS_INVALID_BUFFER_SIZE;
    }

    switch (cmdMsg->Command) {

    case CmdAddProtectedPath:
    {
        PPROTECTED_PATH_ENTRY entry = NULL;
        USHORT pathLen = 0;

        // Ensure null-termination within bounds
        cmdMsg->Data.Path[DECLAW_MAX_PATH_LEN - 1] = L'\0';
        pathLen = (USHORT)(wcslen(cmdMsg->Data.Path) * sizeof(WCHAR));

        if (pathLen == 0) {
            status = STATUS_INVALID_PARAMETER;
            break;
        }

        //
        // BUG FIX: ExAllocatePoolWithTag is deprecated in WDK 10.0.22000+.
        // Use ExAllocatePool2 which zero-initializes memory by default.
        // Fallback to ExAllocatePoolWithTag for older WDK via preprocessor.
        //
#if (NTDDI_VERSION >= NTDDI_WIN10_FE)
        entry = (PPROTECTED_PATH_ENTRY)ExAllocatePool2(
            POOL_FLAG_NON_PAGED,
            sizeof(PROTECTED_PATH_ENTRY),
            DECLAW_POOL_TAG
        );
#else
        entry = (PPROTECTED_PATH_ENTRY)ExAllocatePoolWithTag(
            NonPagedPoolNx,
            sizeof(PROTECTED_PATH_ENTRY),
            DECLAW_POOL_TAG
        );
        if (entry != NULL) {
            RtlZeroMemory(entry, sizeof(PROTECTED_PATH_ENTRY));
        }
#endif

        if (entry == NULL) {
            status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        RtlCopyMemory(entry->PathBuffer, cmdMsg->Data.Path, pathLen);
        entry->PathBuffer[pathLen / sizeof(WCHAR)] = L'\0';
        RtlInitUnicodeString(&entry->Path, entry->PathBuffer);

        KeAcquireInStackQueuedSpinLock(&g_DeclawData.PathListLock, &lockHandle);
        if (g_DeclawData.PathCount < DECLAW_MAX_ENTRIES) {
            InsertTailList(&g_DeclawData.PathListHead, &entry->ListEntry);
            g_DeclawData.PathCount++;
        } else {
            status = STATUS_TOO_MANY_NAMES;
        }
        KeReleaseInStackQueuedSpinLock(&lockHandle);

        if (status == STATUS_TOO_MANY_NAMES) {
            ExFreePoolWithTag(entry, DECLAW_POOL_TAG);
        } else {
            // Log outside the spinlock to avoid stalling other CPUs
            DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "[Declaw] Added protected path: %wZ\n", &entry->Path);
        }
        break;
    }

    case CmdRemoveProtectedPath:
    {
        PLIST_ENTRY curr = NULL;
        PPROTECTED_PATH_ENTRY foundEntry = NULL;
        UNICODE_STRING targetPath;

        cmdMsg->Data.Path[DECLAW_MAX_PATH_LEN - 1] = L'\0';
        RtlInitUnicodeString(&targetPath, cmdMsg->Data.Path);

        if (targetPath.Length == 0) {
            status = STATUS_INVALID_PARAMETER;
            break;
        }

        KeAcquireInStackQueuedSpinLock(&g_DeclawData.PathListLock, &lockHandle);
        for (curr = g_DeclawData.PathListHead.Flink;
             curr != &g_DeclawData.PathListHead;
             curr = curr->Flink)
        {
            PPROTECTED_PATH_ENTRY item = CONTAINING_RECORD(curr, PROTECTED_PATH_ENTRY, ListEntry);
            if (RtlEqualUnicodeString(&item->Path, &targetPath, TRUE)) {
                RemoveEntryList(curr);
                g_DeclawData.PathCount--;
                foundEntry = item;
                break;
            }
        }
        KeReleaseInStackQueuedSpinLock(&lockHandle);

        if (foundEntry != NULL) {
            DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "[Declaw] Removed protected path: %wZ\n", &foundEntry->Path);
            ExFreePoolWithTag(foundEntry, DECLAW_POOL_TAG);
        } else {
            status = STATUS_NOT_FOUND;
        }
        break;
    }

    case CmdClearProtectedPaths:
    {
        PLIST_ENTRY curr = NULL;
        LIST_ENTRY toFree;

        InitializeListHead(&toFree);

        KeAcquireInStackQueuedSpinLock(&g_DeclawData.PathListLock, &lockHandle);
        while (!IsListEmpty(&g_DeclawData.PathListHead)) {
            curr = RemoveHeadList(&g_DeclawData.PathListHead);
            InsertTailList(&toFree, curr);
        }
        g_DeclawData.PathCount = 0;
        KeReleaseInStackQueuedSpinLock(&lockHandle);

        while (!IsListEmpty(&toFree)) {
            curr = RemoveHeadList(&toFree);
            ExFreePoolWithTag(CONTAINING_RECORD(curr, PROTECTED_PATH_ENTRY, ListEntry), DECLAW_POOL_TAG);
        }

        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "[Declaw] Cleared all protected paths.\n");
        break;
    }

    case CmdAddBlockedPid:
    {
        PBLOCKED_PID_ENTRY entry = NULL;
        HANDLE targetPid = UlongToHandle(cmdMsg->Data.ProcessId);

        // Check for duplicate PID before adding
        BOOLEAN alreadyExists = FALSE;
        KeAcquireInStackQueuedSpinLock(&g_DeclawData.PidListLock, &lockHandle);
        {
            PLIST_ENTRY curr;
            for (curr = g_DeclawData.PidListHead.Flink;
                 curr != &g_DeclawData.PidListHead;
                 curr = curr->Flink)
            {
                PBLOCKED_PID_ENTRY item = CONTAINING_RECORD(curr, BLOCKED_PID_ENTRY, ListEntry);
                if (item->ProcessId == targetPid) {
                    alreadyExists = TRUE;
                    break;
                }
            }
        }
        KeReleaseInStackQueuedSpinLock(&lockHandle);

        if (alreadyExists) {
            // PID already blocked, silently succeed
            break;
        }

#if (NTDDI_VERSION >= NTDDI_WIN10_FE)
        entry = (PBLOCKED_PID_ENTRY)ExAllocatePool2(
            POOL_FLAG_NON_PAGED,
            sizeof(BLOCKED_PID_ENTRY),
            DECLAW_POOL_TAG
        );
#else
        entry = (PBLOCKED_PID_ENTRY)ExAllocatePoolWithTag(
            NonPagedPoolNx,
            sizeof(BLOCKED_PID_ENTRY),
            DECLAW_POOL_TAG
        );
        if (entry != NULL) {
            RtlZeroMemory(entry, sizeof(BLOCKED_PID_ENTRY));
        }
#endif

        if (entry == NULL) {
            status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        entry->ProcessId = targetPid;

        KeAcquireInStackQueuedSpinLock(&g_DeclawData.PidListLock, &lockHandle);
        if (g_DeclawData.PidCount < DECLAW_MAX_ENTRIES) {
            InsertTailList(&g_DeclawData.PidListHead, &entry->ListEntry);
            g_DeclawData.PidCount++;
        } else {
            status = STATUS_TOO_MANY_NAMES;
        }
        KeReleaseInStackQueuedSpinLock(&lockHandle);

        if (status == STATUS_TOO_MANY_NAMES) {
            ExFreePoolWithTag(entry, DECLAW_POOL_TAG);
        } else {
            DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "[Declaw] Added blocked PID: %u\n", cmdMsg->Data.ProcessId);
        }
        break;
    }

    case CmdRemoveBlockedPid:
    {
        PLIST_ENTRY curr = NULL;
        PBLOCKED_PID_ENTRY foundEntry = NULL;
        HANDLE targetPid = UlongToHandle(cmdMsg->Data.ProcessId);

        KeAcquireInStackQueuedSpinLock(&g_DeclawData.PidListLock, &lockHandle);
        for (curr = g_DeclawData.PidListHead.Flink; curr != &g_DeclawData.PidListHead; curr = curr->Flink) {
            PBLOCKED_PID_ENTRY item = CONTAINING_RECORD(curr, BLOCKED_PID_ENTRY, ListEntry);
            if (item->ProcessId == targetPid) {
                RemoveEntryList(curr);
                g_DeclawData.PidCount--;
                foundEntry = item;
                break;
            }
        }
        KeReleaseInStackQueuedSpinLock(&lockHandle);

        if (foundEntry != NULL) {
            ExFreePoolWithTag(foundEntry, DECLAW_POOL_TAG);
            DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "[Declaw] Removed blocked PID: %u\n", cmdMsg->Data.ProcessId);
        } else {
            status = STATUS_NOT_FOUND;
        }
        break;
    }

    case CmdClearBlockedPids:
    {
        PLIST_ENTRY curr = NULL;
        LIST_ENTRY toFree;

        InitializeListHead(&toFree);

        KeAcquireInStackQueuedSpinLock(&g_DeclawData.PidListLock, &lockHandle);
        while (!IsListEmpty(&g_DeclawData.PidListHead)) {
            curr = RemoveHeadList(&g_DeclawData.PidListHead);
            InsertTailList(&toFree, curr);
        }
        g_DeclawData.PidCount = 0;
        KeReleaseInStackQueuedSpinLock(&lockHandle);

        while (!IsListEmpty(&toFree)) {
            curr = RemoveHeadList(&toFree);
            ExFreePoolWithTag(CONTAINING_RECORD(curr, BLOCKED_PID_ENTRY, ListEntry), DECLAW_POOL_TAG);
        }

        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "[Declaw] Cleared all blocked PIDs.\n");
        break;
    }

    case CmdGetStats:
        // Handled in output response below
        break;

    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    // Populate response if buffer is provided
    if (OutputBuffer != NULL && OutputBufferSize >= sizeof(DECLAW_REPLY_MESSAGE)) {
        PDECLAW_REPLY_MESSAGE replyMsg = (PDECLAW_REPLY_MESSAGE)OutputBuffer;
        replyMsg->Status = status;
        replyMsg->ProtectedPathCount = g_DeclawData.PathCount;
        replyMsg->BlockedPidCount = g_DeclawData.PidCount;
        replyMsg->TotalBlocksCount = (ULONG)g_DeclawData.TotalBlockedAttempts;
        *ReturnOutputBufferLength = sizeof(DECLAW_REPLY_MESSAGE);
    } else if (ReturnOutputBufferLength != NULL) {
        *ReturnOutputBufferLength = 0;
    }

    return status;
}

BOOLEAN
DeclawIsProcessBlocked(
    _In_ HANDLE ProcessId
)
{
    BOOLEAN isBlocked = FALSE;
    KLOCK_QUEUE_HANDLE lockHandle;
    PLIST_ENTRY curr = NULL;

    KeAcquireInStackQueuedSpinLock(&g_DeclawData.PidListLock, &lockHandle);
    for (curr = g_DeclawData.PidListHead.Flink; curr != &g_DeclawData.PidListHead; curr = curr->Flink) {
        PBLOCKED_PID_ENTRY item = CONTAINING_RECORD(curr, BLOCKED_PID_ENTRY, ListEntry);
        if (item->ProcessId == ProcessId) {
            isBlocked = TRUE;
            break;
        }
    }
    KeReleaseInStackQueuedSpinLock(&lockHandle);

    return isBlocked;
}

BOOLEAN
DeclawIsPathProtected(
    _In_ PCUNICODE_STRING NormalizedPath
)
{
    BOOLEAN isProtected = FALSE;
    KLOCK_QUEUE_HANDLE lockHandle;
    PLIST_ENTRY curr = NULL;

    if (NormalizedPath == NULL || NormalizedPath->Buffer == NULL || NormalizedPath->Length == 0) {
        return FALSE;
    }

    KeAcquireInStackQueuedSpinLock(&g_DeclawData.PathListLock, &lockHandle);
    for (curr = g_DeclawData.PathListHead.Flink; curr != &g_DeclawData.PathListHead; curr = curr->Flink) {
        PPROTECTED_PATH_ENTRY item = CONTAINING_RECORD(curr, PROTECTED_PATH_ENTRY, ListEntry);

        //
        // Prefix match: if the file path starts with the protected directory path
        // then this file is inside the vault. Case-insensitive (TRUE).
        //
        // OPTIMIZATION: Also check that the character after the prefix is either
        // '\' (subdirectory) or the lengths are equal (exact directory match).
        // This prevents \Device\HDD\SecretStuff matching rule \Device\HDD\Secret.
        //
        if (RtlPrefixUnicodeString(&item->Path, NormalizedPath, TRUE)) {
            USHORT prefixChars = item->Path.Length / sizeof(WCHAR);
            USHORT fullChars = NormalizedPath->Length / sizeof(WCHAR);

            if (prefixChars == fullChars ||
                NormalizedPath->Buffer[prefixChars] == L'\\')
            {
                isProtected = TRUE;
                break;
            }
        }
    }
    KeReleaseInStackQueuedSpinLock(&lockHandle);

    return isProtected;
}
