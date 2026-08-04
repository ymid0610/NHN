#pragma once

#include <vector>

#include "core/IocpCore.h"
#include "core/Net.h"

namespace nhn {

class ServerService;

/// The accepting end of a ServerService.
///
/// Keeps a pool of AcceptEx operations posted at all times so that a burst of
/// connections is absorbed by the kernel rather than serialised behind a single
/// accept.
class Listener : public IocpObject {
public:
    ~Listener() override;

    bool StartAccept(Ref<ServerService> service);
    void CloseSocket();

    HANDLE GetHandle() override;
    void Dispatch(IocpEvent* iocpEvent, int32 numOfBytes = 0) override;

private:
    /// Arms one accept slot with a fresh session.
    void RegisterAccept(AcceptEvent* acceptEvent);
    void ProcessAccept(AcceptEvent* acceptEvent);

    /// Concurrent accepts kept posted. Enough to absorb a lobby-wide reconnect
    /// without making the kernel queue connections.
    static constexpr int32 kAcceptBacklog = 32;

    SOCKET _socket = INVALID_SOCKET;
    std::vector<AcceptEvent*> _acceptEvents;
    Ref<ServerService> _service;
};

using ListenerRef = Ref<Listener>;

}  // namespace nhn
