// Experimental transport outbox. Storage adapter must persist before send.
// Entries are retained across disconnect; only an ACK of the same owner/id/item removes them.
export interface PendingConsume { opId: string; baseId: number; profileId: string; characterId: string; }
export class SrpConsumeQueue {
    private items: PendingConsume[];
    constructor(private save: (items: PendingConsume[]) => void, initial: PendingConsume[] = []) {
        if (!Array.isArray(initial) || initial.some(x => !x || typeof x.opId !== "string" || typeof x.baseId !== "number" || typeof x.profileId !== "string" || typeof x.characterId !== "string")) throw Error("consume_outbox_corrupt");
        this.items = initial.slice();
    }
    add(item: PendingConsume) {
        if (this.items.length >= 64) throw Error("consume_outbox_full");
        const next = this.items.concat(item); this.save(next); this.items = next;
    }
    forOwner(owner: {profileId: string; characterId: string}) {
        return this.items.filter(x => x.profileId === owner.profileId && x.characterId === owner.characterId);
    }
    acknowledge(data: Record<string, unknown>) {
        const committed = data.ok === true && data.removed === 1;
        const aborted = data.ok === false && data.terminal === true && data.removed === 0;
        if (!committed && !aborted) return false;
        const next = this.items.filter(x => !(x.opId === data.requestId && x.baseId === data.baseId && x.profileId === data.profileId && x.characterId === data.characterId));
        if (next.length !== this.items.length) { this.save(next); this.items = next; return true; }
        return false;
    }
}
