add_library(xrNetServerMultiplayer OBJECT
    SerialSequence.cpp SerialSequence.h
    WireEndian.h
    WireEnvelope.cpp WireEnvelope.h
    WirePayload.cpp WirePayload.h
    CompatibilityId.h
    ClientHello.cpp ClientHello.h
    ServerWelcome.cpp ServerWelcome.h
    AdmissionReject.cpp AdmissionReject.h
    PendingAdmission.cpp PendingAdmission.h
    JoinTypes.cpp JoinTypes.h
    CombatantIdentity.h
    PeerJoin.cpp PeerJoin.h
    BoundMessage.cpp BoundMessage.h
    ClientJoin.cpp ClientJoin.h
    BaselineTransfer.cpp BaselineTransfer.h
    BaselineStaging.cpp BaselineStaging.h
    BaselineStagingBudget.cpp BaselineStagingBudget.h
    BaselineRecords.cpp BaselineRecords.h
    CatchUpEvent.h
    CatchUpDespawn.cpp CatchUpDespawn.h
    SnapshotBatch.cpp SnapshotBatch.h
    CatchUpEventQueue.cpp CatchUpEventQueue.h
    ClientCatchUp.cpp ClientCatchUp.h
)
target_include_directories(xrNetServerMultiplayer PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
set_target_properties(xrNetServerMultiplayer PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)
