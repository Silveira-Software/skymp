// SRP experimental: client side of the srp:caps handshake (server: SKYRIMRP server/src/core/client-caps.js).
//   server -> {srpBridge: 1, type: "srp:caps",    data: {v, min, max, features[], nonce}}
//   client -> {srpBridge: 1, type: "srp:capsAck", data: {v, features[], nonce}}
//   server -> {srpBridge: 1, type: "srp:capsOk",  data: {v, features[], nonce}}
// A feature is used only after srp:capsOk with the same nonce lists it. Anything else keeps the old behavior.

export const SRP_CAPS_CLIENT_VERSIONS = [1];
export const SRP_CAPS_CLIENT_FEATURES = ["onEquipOpId", "durableConsumeAck"];

export interface SrpBridgePacket {
    srpBridge: 1,
    type: string,
    data: Record<string, unknown>
}

export class SrpCapsNegotiation {
    // Returns the packet to send back, if any.
    handle(content: unknown): SrpBridgePacket | null {
        if (!content || typeof content !== "object") {
            return null;
        }
        const packet = content as Record<string, unknown>;
        if (packet["srpBridge"] !== 1 || !packet["data"] || typeof packet["data"] !== "object") {
            return null;
        }
        const data = packet["data"] as Record<string, unknown>;
        if (packet["type"] === "srp:caps") {
            return this.onOffer(data);
        }
        if (packet["type"] === "srp:capsOk") {
            this.onConfirm(data);
        }
        return null;
    }

    isActive(feature: string): boolean {
        return this.active.has(feature);
    }

    reset() {
        this.pendingNonce = null;
        this.active = new Set();
    }

    private onOffer(data: Record<string, unknown>): SrpBridgePacket | null {
        this.reset();
        const min = data["min"], max = data["max"], nonce = data["nonce"], features = data["features"];
        if (typeof min !== "number" || typeof max !== "number" || typeof nonce !== "string" || !Array.isArray(features)) {
            return null;
        }
        const versions = SRP_CAPS_CLIENT_VERSIONS.filter((v) => v >= min && v <= max);
        if (versions.length === 0) {
            return null; // no common version: stay on the old protocol
        }
        const common = SRP_CAPS_CLIENT_FEATURES.filter((f) => features.indexOf(f) !== -1);
        this.pendingNonce = nonce;
        return { srpBridge: 1, type: "srp:capsAck", data: { v: Math.max(...versions), features: common, nonce } };
    }

    private onConfirm(data: Record<string, unknown>) {
        if (this.pendingNonce === null || data["nonce"] !== this.pendingNonce) {
            return;
        }
        this.pendingNonce = null;
        const features = data["features"];
        if (typeof data["v"] !== "number" || data["v"] < 1 || !Array.isArray(features)) {
            return;
        }
        this.active = new Set(SRP_CAPS_CLIENT_FEATURES.filter((f) => features.indexOf(f) !== -1));
    }

    private pendingNonce: string | null = null;
    private active = new Set<string>();
}
