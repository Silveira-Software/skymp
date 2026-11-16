# R49 client recovery heartbeat

The R7 client stops negotiating after three unanswered hello packets. A real
reconnect on14 November recovered its transformation/resource fence after that
window. A subsequent real Absorption579009 cast logged ownerReadytrue and
sessionReadyfalse; no authoritative impact was sent. The existing native
Authenticated fence rejects hello until recovery finishes. A second reconnect
with the fence already released received READY, proving the lost negotiation.

Keep the existing5second authenticated hello heartbeat while an owner exists.
The native protocol, session identity, transformation fence and combat admission
remain unchanged. Disconnect still clears owner/session. No damage is enabled
by the client change itself.

Game PR66 contains the pre-edit plan client-r9-recovery-plan.md, source/bundle
builder and actual compiled constructor tests. The test reproduces the original
three-attempt exhaustion, then receives ready on a fourth R9 attempt; regular
heartbeat and disconnect reset pass. Native HitService transpilation passes.
The paired Game client also refreshes its existing equipment snapshot after a
same-owner, observed-operation ready ACK.75supernatural tests and the full
compiled SendInputs constructor pass. Client c332f52a0eea1c7e91befdbe1733234261da62aec6ec60d39365f1a07ea7b385,
manifest323d8c5cfcf8b64bd93cfac379f788227546bdfd6e2a4b02f15fdd984211bb97.

Status: package prepared, installation/runtime/human reconnection pending.
No PUBLIC promotion. R7 bundle and its manifest remain the rollback pair.
