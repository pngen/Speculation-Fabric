# Speculation Fabric - The speculative lifecycle

A speculative cycle runs through an explicit, guarded state machine. A
transition is legal only if it is declared in the transition table and its guard
passes. Terminal phases are absorbing.

## Phases

Ready, ProposalPlanned, ProposalReserved, ProposalDispatched, ProposalRunning,
ProposalProduced, VerificationPlanned, VerificationReserved,
VerificationDispatched, VerificationRunning, Verified, PartiallyAccepted,
FullyAccepted, Rejected, RolledBack, CancelRequested, Cancelled, RetryPending,
Superseded, Expired, Failed, Committed, Terminal.

## A canonical cycle

Ready -> PlanProposal -> ProposalPlanned -> ReserveProposal -> ProposalReserved
-> DispatchProposal -> ProposalDispatched -> ProposalStarted -> ProposalRunning
-> ProposalFinished -> ProposalProduced -> PlanVerification ->
VerificationPlanned -> ReserveVerification -> VerificationReserved ->
DispatchVerification -> VerificationDispatched -> VerificationStarted ->
VerificationRunning -> VerificationFinished -> Verified -> AcceptFull ->
FullyAccepted -> Commit -> Committed. A partial acceptance takes AcceptPartial
then Commit; a full rejection takes RejectAll then Rollback.

## Guards

A cycle rejection carries a structured reason. Threading a guard into a
declared transition returns, for example, staleAuthority when a completion no
longer matches the current authority. This is how stale, duplicate, and
superseded work is rejected rather than silently applied.
