#include "core/IocpCore.h"

#include <vector>

#include "core/Buffer.h"
#include "core/FatalHandler.h"
#include "core/Log.h"
#include "core/Net.h"

namespace nhn {

IocpCore::IocpCore() {
    _iocpHandle = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    NHN_CHECK(_iocpHandle != nullptr, "CreateIoCompletionPort failed: {}",
              SocketUtils::ErrorText(static_cast<int32>(::GetLastError())));
}

IocpCore::~IocpCore() {
    if (_iocpHandle != nullptr && _iocpHandle != INVALID_HANDLE_VALUE) {
        ::CloseHandle(_iocpHandle);
        _iocpHandle = nullptr;
    }
}

bool IocpCore::Register(IocpObjectRef iocpObject) {
    return ::CreateIoCompletionPort(iocpObject->GetHandle(), _iocpHandle, 0, 0) != nullptr;
}

bool IocpCore::Dispatch(uint32 timeoutMs) {
    DWORD numOfBytes = 0;
    ULONG_PTR key = 0;
    OVERLAPPED* overlapped = nullptr;

    const BOOL succeeded =
        ::GetQueuedCompletionStatus(_iocpHandle, &numOfBytes, &key, &overlapped, timeoutMs);

    if (succeeded != FALSE) {
        if (overlapped == nullptr) {
            // A wake-up posted by ThreadManager::RequestStop, not a real
            // completion. Nothing to dispatch.
            return true;
        }

        auto* iocpEvent = static_cast<IocpEvent*>(overlapped);
        // The owner reference is released as the event's own handler completes;
        // taking a local copy first keeps the object alive across Dispatch even
        // if the handler clears it.
        IocpObjectRef owner = iocpEvent->owner;
        if (owner != nullptr) {
            owner->Dispatch(iocpEvent, static_cast<int32>(numOfBytes));
        }
        return true;
    }

    const int32 errorCode = static_cast<int32>(::GetLastError());
    if (errorCode == WAIT_TIMEOUT) {
        return false;
    }

    if (overlapped == nullptr) {
        // Failure of the wait itself rather than of a queued operation.
        LOG_ERROR("GetQueuedCompletionStatus failed: {}", SocketUtils::ErrorText(errorCode));
        return false;
    }

    // A queued operation failed — typically the peer closing mid-flight. Route
    // it to the owner so the session can tear itself down normally.
    auto* iocpEvent = static_cast<IocpEvent*>(overlapped);
    IocpObjectRef owner = iocpEvent->owner;
    if (owner != nullptr) {
        owner->Dispatch(iocpEvent, static_cast<int32>(numOfBytes));
    }
    return true;
}

}  // namespace nhn
