#pragma once

#include <fltKernel.h>
#include <dontuse.h>
#include <suppress.h>

#define DECLAW_PORT_NAME          L"\\DeclawPort"
#define DECLAW_MAX_PATH_LEN       512
#define DECLAW_MAX_ENTRIES        256
#define DECLAW_POOL_TAG           'ihSA' // 'AShi'

//
// Communication Message Commands
//
typedef enum _DECLAW_COMMAND {
    CmdAddProtectedPath = 1,
    CmdRemoveProtectedPath,
    CmdClearProtectedPaths,
    CmdAddBlockedPid,
    CmdRemoveBlockedPid,
    CmdClearBlockedPids,
    CmdGetStats
} DECLAW_COMMAND;

//
// Message sent from User-mode to Kernel-mode
//
#pragma pack(push, 1)
typedef struct _DECLAW_COMMAND_MESSAGE {
    DECLAW_COMMAND Command;
    union {
        WCHAR Path[DECLAW_MAX_PATH_LEN];
        ULONG ProcessId;
    } Data;
} DECLAW_COMMAND_MESSAGE, *PDECLAW_COMMAND_MESSAGE;

//
// Reply from Kernel-mode to User-mode
//
typedef struct _DECLAW_REPLY_MESSAGE {
    NTSTATUS Status;
    ULONG ProtectedPathCount;
    ULONG BlockedPidCount;
    ULONG TotalBlocksCount;
} DECLAW_REPLY_MESSAGE, *PDECLAW_REPLY_MESSAGE;
#pragma pack(pop)

//
// Protected Path Entry in Kernel Linked List
//
typedef struct _PROTECTED_PATH_ENTRY {
    LIST_ENTRY ListEntry;
    UNICODE_STRING Path;
    WCHAR PathBuffer[DECLAW_MAX_PATH_LEN];
} PROTECTED_PATH_ENTRY, *PPROTECTED_PATH_ENTRY;

//
// Blocked PID Entry in Kernel Linked List
//
typedef struct _BLOCKED_PID_ENTRY {
    LIST_ENTRY ListEntry;
    HANDLE ProcessId;
} BLOCKED_PID_ENTRY, *PBLOCKED_PID_ENTRY;

//
// Global Driver Data Structure
//
// BUG FIX: Changed from EX_SPIN_LOCK to KSPIN_LOCK.
// EX_SPIN_LOCK is a reader-writer spinlock that uses ExAcquireSpinLockExclusive/Shared.
// We were calling KeAcquireInStackQueuedSpinLock which requires KSPIN_LOCK.
// Using KSPIN_LOCK + KeAcquireInStackQueuedSpinLock is the correct, standard pattern.
//
typedef struct _DECLAW_DATA {
    PFLT_FILTER FilterHandle;
    PFLT_PORT ServerPort;
    PFLT_PORT ClientPort;

    // Synchronization — KSPIN_LOCK for use with KeAcquireInStackQueuedSpinLock
    KSPIN_LOCK PathListLock;
    LIST_ENTRY PathListHead;
    ULONG PathCount;

    KSPIN_LOCK PidListLock;
    LIST_ENTRY PidListHead;
    ULONG PidCount;

    // Statistics
    volatile LONG TotalBlockedAttempts;
} DECLAW_DATA, *PDECLAW_DATA;

extern DECLAW_DATA g_DeclawData;

//
// Function Declarations (Driver Core)
//
DRIVER_INITIALIZE DriverEntry;
NTSTATUS DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
);

NTSTATUS
DeclawUnload(
    _In_ FLT_FILTER_UNLOAD_FLAGS Flags
);

FLT_PREOP_CALLBACK_STATUS
DeclawPreCreate(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID *CompletionContext
);

//
// Function Declarations (Communication Port)
//
NTSTATUS
DeclawPortInitialize(
    _In_ PFLT_FILTER Filter
);

VOID
DeclawPortCleanup(
    VOID
);

NTSTATUS
DeclawPortConnect(
    _In_ PFLT_PORT ClientPort,
    _In_opt_ PVOID ServerPortCookie,
    _In_reads_bytes_opt_(SizeOfContext) PVOID ConnectionContext,
    _In_ ULONG SizeOfContext,
    _Outptr_result_maybenull_ PVOID *ConnectionPortCookie
);

VOID
DeclawPortDisconnect(
    _In_opt_ PVOID ConnectionCookie
);

NTSTATUS
DeclawPortMessage(
    _In_opt_ PVOID PortCookie,
    _In_reads_bytes_opt_(InputBufferSize) PVOID InputBuffer,
    _In_ ULONG InputBufferSize,
    _Out_writes_bytes_to_opt_(OutputBufferSize, *ReturnOutputBufferLength) PVOID OutputBuffer,
    _In_ ULONG OutputBufferSize,
    _Out_ PULONG ReturnOutputBufferLength
);

//
// Rule Evaluation Helpers
//
BOOLEAN
DeclawIsProcessBlocked(
    _In_ HANDLE ProcessId
);

BOOLEAN
DeclawIsPathProtected(
    _In_ PCUNICODE_STRING NormalizedPath
);
