#pragma once

#include "core/Types.h"

namespace nhn::match {

class SessionManager;
class RoomManager;
class PeerRegistry;
class MatchServer;

/// Process-wide singletons.
///
/// The match server is one process with one of each of these, and threading
/// them through every call site as parameters would add a lot of plumbing for
/// no flexibility. They are created once in main before any service starts and
/// torn down after the worker pool stops.
extern Ref<SessionManager> GSessionManager;
extern Ref<RoomManager> GRoomManager;
extern Ref<PeerRegistry> GPeerRegistry;
extern Ref<MatchServer> GMatchServer;

}  // namespace nhn::match
