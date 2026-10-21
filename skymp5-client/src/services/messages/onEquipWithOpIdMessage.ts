import { MsgType } from "../../messages";

// SRP experimental: OnEquip with a client operation id, so the server can
// drop a repeated delivery of the same potion/ingredient use.
// Only sent after the server announced support (custom packet "srpCaps").
export interface OnEquipWithOpIdMessage {
    t: MsgType.OnEquipWithOpId,
    baseId: number,
    opId: string
}
