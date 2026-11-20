#include "SerialSequence.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace MapSyncWaitTests
{
bool Run();
}

namespace WeaponSpreadTests
{
bool Run();
}

namespace ScriptedTransportTests
{
bool Run();
}

namespace WireEnvelopeTests
{
bool Run();
}

namespace WirePayloadTests
{
bool Run();
}

namespace ClientHelloTests
{
bool Run();
}

namespace ServerWelcomeTests
{
bool Run();
}

namespace AdmissionRejectTests
{
bool Run();
}

namespace PendingAdmissionTests
{
bool Run();
}

namespace AdmissionCompletionTests
{
bool Run();
}

namespace PendingAdmissionLimitTests
{
bool Run();
}

namespace PeerJoinTests
{
bool Run();
}

namespace ClientJoinTests
{
bool Run();
}

namespace BaselineTransferTests
{
bool Run();
}

namespace BaselineStagingTests
{
bool Run();
}

namespace BaselineRecordsTests
{
bool Run();
}

namespace BaselineStagingBudgetTests
{
bool Run();
}

namespace BaselineRebaseTests
{
bool Run();
}

namespace PeerRebaseTests
{
bool Run();
}

namespace CatchUpEventQueueTests
{
bool Run();
}

namespace ClientCatchUpTests
{
bool Run();
}

namespace StalkerPrerequisitesTests
{
bool RunNavigation();
bool RunAuthority();
bool RunW1Option();
}

namespace CombatantRegistryTests
{
bool Run();
}

namespace ParticipantConnectionTests
{
bool Run();
}

namespace CatchUpDespawnTests
{
bool Run();
}

namespace CombatantReplicasTests
{
bool Run();
}

namespace ReplicaSnapshotTests
{
bool Run();
}

namespace SnapshotBatchTests
{
bool Run();
}

namespace CombatantSnapshotTests
{
bool Run();
}

namespace ReplicaSnapshotDispatchTests
{
bool Run();
bool RunQueue();
}

namespace SerialSequenceTests
{
struct SequenceCase
{
	const char* Name;
	u32 Candidate;
	u32 Reference;
	bool Expected;
};

constexpr SequenceCase Cases[] = {
	{"ordinary successor", 42u, 41u, true},
	{"ordinary predecessor", 41u, 42u, false},
	{"duplicate", 42u, 42u, false},
	{"first sequence after wrap", 0u, 0xffffffffu, true},
	{"multi-step advance across wrap", 1u, 0xfffffffeu, true},
	{"stale sequence before wrap", 0xffffffffu, 0u, false},
	{"advance across the sign bit", 0x80000000u, 0x7fffffffu, true},
	{"largest allowed forward distance across wrap", 0x7ffffffdu, 0xfffffffeu, true},
	{"half range clockwise", 0x90000000u, 0x10000000u, false},
	{"half range counterclockwise", 0x10000000u, 0x90000000u, false},
	{"one farther than half range", 0x80000001u, 0u, false},
};

static bool CheckIsNewer(const SequenceCase& Case)
{
	const bool Actual = Multiplayer::IsSequenceNewer(Case.Candidate, Case.Reference);
	if (Actual != Case.Expected)
	{
		std::fprintf(stderr, "%s: expected IsSequenceNewer to return %s\n", Case.Name, Case.Expected ? "true" : "false");
		return false;
	}
	return true;
}
}

int main(int ArgumentCount, char** Arguments)
{
	if (ArgumentCount == 2 && std::strcmp(Arguments[1], "--verify-failure") == 0)
	{
		return SerialSequenceTests::CheckIsNewer({"deliberate failure", 42u, 41u, false}) ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if (ArgumentCount == 2 && std::strcmp(Arguments[1], "--map-sync") == 0)
	{
		return MapSyncWaitTests::Run() ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if (ArgumentCount == 2 && std::strcmp(Arguments[1], "--weapon-spread") == 0)
	{
		return WeaponSpreadTests::Run() ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if (ArgumentCount == 2 && std::strcmp(Arguments[1], "--scripted-transport") == 0)
	{
		return ScriptedTransportTests::Run() ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if (ArgumentCount == 2 && std::strcmp(Arguments[1], "--wire-envelope") == 0)
	{
		return WireEnvelopeTests::Run() ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if (ArgumentCount == 2 && std::strcmp(Arguments[1], "--wire-payload") == 0)
	{
		return WirePayloadTests::Run() ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if (ArgumentCount == 2 && std::strcmp(Arguments[1], "--client-hello") == 0)
	{
		return ClientHelloTests::Run() ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if (ArgumentCount == 2 && std::strcmp(Arguments[1], "--server-welcome") == 0)
	{
		return ServerWelcomeTests::Run() ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if (ArgumentCount == 2 && std::strcmp(Arguments[1], "--admission-reject") == 0)
	{
		return AdmissionRejectTests::Run() ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if (ArgumentCount == 2 && std::strcmp(Arguments[1], "--pending-admission") == 0)
	{
		return PendingAdmissionTests::Run() ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if (ArgumentCount == 2 && std::strcmp(Arguments[1], "--admission-completion") == 0)
	{
		return AdmissionCompletionTests::Run() ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if (ArgumentCount == 2 && std::strcmp(Arguments[1], "--pending-admission-limit") == 0)
	{
		return PendingAdmissionLimitTests::Run() ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if (ArgumentCount == 2 && std::strcmp(Arguments[1], "--peer-join") == 0)
	{
		return PeerJoinTests::Run() ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if (ArgumentCount == 2 && std::strcmp(Arguments[1], "--client-join") == 0)
	{
		return ClientJoinTests::Run() ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if (ArgumentCount == 2 && std::strcmp(Arguments[1], "--baseline-transfer") == 0)
	{
		return BaselineTransferTests::Run() ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if (ArgumentCount == 2 && std::strcmp(Arguments[1], "--baseline-staging") == 0)
	{
		return BaselineStagingTests::Run() ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if (ArgumentCount == 2 && std::strcmp(Arguments[1], "--baseline-records") == 0)
	{
		return BaselineRecordsTests::Run() ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if (ArgumentCount == 2 && std::strcmp(Arguments[1], "--baseline-staging-budget") == 0)
	{
		return BaselineStagingBudgetTests::Run() ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if (ArgumentCount == 2 && std::strcmp(Arguments[1], "--baseline-rebase") == 0)
	{
		return BaselineRebaseTests::Run() ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if (ArgumentCount == 2 && std::strcmp(Arguments[1], "--peer-rebase") == 0)
	{
		return PeerRebaseTests::Run() ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if (ArgumentCount == 2 && std::strcmp(Arguments[1], "--catch-up-event-queue") == 0)
	{
		return CatchUpEventQueueTests::Run() ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if (ArgumentCount == 2 && std::strcmp(Arguments[1], "--client-catch-up") == 0)
	{
		return ClientCatchUpTests::Run() ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if (ArgumentCount == 2 && std::strcmp(Arguments[1], "--stalker-navigation") == 0)
	{
		return StalkerPrerequisitesTests::RunNavigation() ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if (ArgumentCount == 2 && std::strcmp(Arguments[1], "--stalker-authority") == 0)
	{
		return StalkerPrerequisitesTests::RunAuthority() ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if (ArgumentCount == 2 && std::strcmp(Arguments[1], "--w1-navigation-option") == 0)
	{
		return StalkerPrerequisitesTests::RunW1Option() ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if (ArgumentCount == 2 && std::strcmp(Arguments[1], "--combatant-registry") == 0)
	{
		return CombatantRegistryTests::Run() ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if (ArgumentCount == 2 && std::strcmp(Arguments[1], "--participant-connection") == 0)
	{
		return ParticipantConnectionTests::Run() ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if (ArgumentCount == 2 && std::strcmp(Arguments[1], "--catch-up-despawn") == 0)
	{
		return CatchUpDespawnTests::Run() ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if (ArgumentCount == 2 && std::strcmp(Arguments[1], "--combatant-replicas") == 0)
	{
		return CombatantReplicasTests::Run() ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if (ArgumentCount == 2 && std::strcmp(Arguments[1], "--replica-snapshot") == 0)
	{
		return ReplicaSnapshotTests::Run() ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if (ArgumentCount == 2 && std::strcmp(Arguments[1], "--snapshot-batch") == 0)
	{
		return SnapshotBatchTests::Run() ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if (ArgumentCount == 2 && std::strcmp(Arguments[1], "--combatant-snapshot") == 0)
	{
		return CombatantSnapshotTests::Run() ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if (ArgumentCount == 2 && std::strcmp(Arguments[1], "--replica-snapshot-dispatch") == 0)
	{
		return ReplicaSnapshotDispatchTests::Run() ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if (ArgumentCount == 2 && std::strcmp(Arguments[1], "--replica-snapshot-queue") == 0)
	{
		return ReplicaSnapshotDispatchTests::RunQueue() ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if (ArgumentCount != 1)
	{
		std::fputs("Unexpected multiplayer test arguments\n", stderr);
		return EXIT_FAILURE;
	}

	bool Passed = true;
	for (const auto& Case : SerialSequenceTests::Cases)
	{
		if (!SerialSequenceTests::CheckIsNewer(Case))
		{
			Passed = false;
		}
	}
	if (Passed)
	{
		std::puts("Sequence comparisons passed");
	}
	return Passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
