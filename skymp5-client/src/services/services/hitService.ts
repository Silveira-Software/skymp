// TODO: refactor this out
import { localIdToRemoteId } from "../../view/worldViewMisc";

import { FormType, HitEvent, storage } from "skyrimPlatform";
import { ClientListener, CombinedController, Sp } from "./clientListener";
import { MsgType } from "../../messages";
import { Hit } from "../messages/hitMessage";
import { randomBytes } from "crypto";
import { ConnectionMessage } from "../events/connectionMessage";
import { CustomPacketMessage } from "../messages/customPacketMessage";
import { RemoteServer } from "./remoteServer";

export class HitService extends ClientListener {
    constructor(private sp: Sp, private controller: CombinedController) {
        super();
        controller.on('hit', (e) => this.onHit(e));
        controller.emitter.on("customPacketMessage", e => this.onMeleePacket(e));
        controller.emitter.on("connectionDisconnect", () => this.resetMeleeSession());
        controller.emitter.on("connectionFailed", () => this.resetMeleeSession());
        controller.emitter.on("createActorMessage", e => {
            if (!e.message.isMe) return;
            this.resetMeleeSession();
            this.meleeOwner = Number(e.message.refrId) >>> 0;
        });
        controller.on("update", () => {
            if (this.meleeOwner && Date.now() >= this.nextMeleeHello &&
                (this.meleeSession || this.meleeHelloAttempts < 3)) {
                this.nextMeleeHello = Date.now() + 5000;
                this.meleeHelloAttempts++;
                this.sendMeleePacket({ action: "hello" });
            }
            this.updateDrain();
        });
        controller.once("update", () => this.sp.hooks.sendAnimationEvent.add({
            enter: () => {},
            leave: ctx => {
                if (!ctx.animationSucceeded || !this.meleeSession ||
                    !/^attackStart/i.test(ctx.animEventName)) return;
                this.swing = { id: randomBytes(16).toString("hex"), until: Date.now() + 2500 };
            },
        }, 0x14, 0x14));
    }

    private resetMeleeSession() {
        this.meleeOwner = 0;
        this.meleeSession = null;
        this.swing = null;
        this.nextMeleeHello = 0;
        this.meleeHelloAttempts = 0;
        this.drain = null;
    }

    private sendMeleePacket(data: Record<string, unknown>) {
        this.controller.emitter.emit("sendMessage", {
            message: {
                t: MsgType.CustomPacket,
                contentJsonDump: JSON.stringify({ customPacketType: "srpOriginalMelee", version: 1, ...data }),
            },
            reliability: "reliable",
        });
    }

    private onMeleePacket(e: ConnectionMessage<CustomPacketMessage>) {
        try {
            const packet = JSON.parse(e.message.contentJsonDump);
            if (packet?.customPacketType === 'srpOriginalMelee' && packet.version === 1 &&
                packet.action === 'drainAck' && packet.actorId === this.meleeOwner &&
                this.drain && this.drain.id === packet.actionId) {
                if (packet.state === 'APPLIED') {
                    if (!this.drain.concentration) { this.drain = null; return; }
                    this.drain.id = randomBytes(16).toString('hex'); this.drain.created = Date.now(); this.drain.next = Date.now();
                } else if (['CANCELLED', 'REFUSED'].includes(packet.state)) this.drain = null;
                return;
            }
            if (packet?.customPacketType !== "srpOriginalMelee" || packet.version !== 1 ||
                packet.action !== "ready" || !this.meleeOwner || packet.actorId !== this.meleeOwner ||
                typeof packet.session !== "string" || !/^[0-9a-f]{64}$/.test(packet.session)) return;
            this.meleeSession = packet.session;
        } catch {
            // Other custom packets belong to their respective services.
        }
    }

    private onHit(e: HitEvent) {
        // TODO: add more logging in case of 'return'
        // TODO: allow non-weapon sources
        const aggressor = e.aggressor.getFormID();
        if (aggressor < 0xff000000 && aggressor !== 0x14) return; // all skymp npcs are FF+

        if (aggressor >= 0xff000000) {
            // TODO: make host service
            const hosted = storage['hosted'];
            let alreadyHosted = false;
            if (Array.isArray(hosted)) {
                const remoteId = localIdToRemoteId(aggressor);
                if (hosted.includes(remoteId) || hosted.includes(remoteId + 0x100000000)) {
                    alreadyHosted = true;
                }
            }

            if (!alreadyHosted) {
              return;
            }
        }

        const base = e.target.getBaseObject();
        const type = base?.getType();

        if (type === FormType.Static || type === FormType.MovableStatic) {
            return;
        }

        const sourceId = e.source ? e.source.getFormID() : 0;
        if (this.isDrainSpell(sourceId)) return;
        const beastRace = this.sp.Game.getFormFromFile(0xcdd84, 'Skyrim.esm');
        const werewolf = !!beastRace && this.sp.Game.getPlayer()?.getRace()?.getFormID() === beastRace.getFormID();
        const claws = aggressor === 0x14 && werewolf && (!sourceId || sourceId === 0x1f4);
        const isWeapon = e.source && this.sp.Weapon.from(e.source);
        const isSpell = e.source && !isWeapon && this.sp.Spell.from(e.source);
        const isScroll = e.source && !isWeapon && !isSpell && this.sp.Scroll.from(e.source);

        if (!isWeapon && !isSpell && !isScroll && !claws) {
            return;
        }

        // prevent double hit that happens for some reason with magic projectiles
        if (isSpell || isScroll) {
            const aggressorId = e.aggressor.getFormID();
            const now = Date.now();

            const lastHitTime = this.recentMagicHits.get(aggressorId);
            if (lastHitTime && now - lastHitTime < 100) {
                return;
            }

            this.recentMagicHits.set(aggressorId, now);
        }

        const data = this.getHitData(e);
        const swing = this.swing;
        if (aggressor === 0x14 && (isWeapon || claws) && data.target > 0 && data.target < 0xff000000 &&
            !data.projectile && !data.isBashAttack && !data.isPowerAttack && !data.isSneakAttack &&
            this.meleeSession && swing && !swing.target && Date.now() <= swing.until &&
            this.controller.lookupListener(RemoteServer).getMyRemoteRefrId() % 0x100000000 === this.meleeOwner) {
            swing.target = data.target;
            this.sendMeleePacket({ action: "impact", session: this.meleeSession,
                actionId: swing.id, targetId: swing.target, ...(claws ? { power: 'werewolfClaws' } : {}) });
        }
        if (claws) return;
        this.controller.emitter.emit("sendMessage", {
            message: { t: MsgType.OnHit, data },
            reliability: "reliable"
        });
    }

    private getHitData(e: HitEvent): Hit {
        const hitData: Hit = {
            aggressor: localIdToRemoteId(e.aggressor.getFormID()),
            isBashAttack: e.isBashAttack,
            isHitBlocked: e.isHitBlocked,
            isPowerAttack: e.isPowerAttack,
            isSneakAttack: e.isSneakAttack,
            projectile: e.projectile ? e.projectile.getFormID() : 0,
            source: e.source ? e.source.getFormID() : 0,
            target: localIdToRemoteId(e.target.getFormID())
        }
        return hitData;
    }

    isDrainSpell(spell: number) {
        const first = this.sp.Game.getFormFromFile(0x8d5bf, 'Skyrim.esm')?.getFormID();
        const lord = this.sp.Game.getFormFromFile(0x19324, 'Dawnguard.esm')?.getFormID();
        return (!!first && spell >= first && spell <= first + 3) || (!!lord && spell === lord);
    }

    beginDrain(target: number, spell: number, source: number) {
        if (!this.isDrainSpell(spell)) return false;
        if (!this.meleeSession || !this.meleeOwner || target <= 0 || target >= 0xff000000 || ![0, 1].includes(source)) return true;
        if (this.drain && this.drain.spell === spell && this.drain.target === target && this.drain.source === source) return true;
        this.interruptDrain();
        const concentration = spell !== this.sp.Game.getFormFromFile(0x19324, 'Dawnguard.esm')?.getFormID();
        this.drain = { id: randomBytes(16).toString('hex'), target, spell, source, concentration,
            created: Date.now(), next: Date.now() };
        return true;
    }

    interruptDrain() {
        if (this.drain && this.meleeSession) this.sendMeleePacket({ action: 'interruptDrain', session: this.meleeSession,
            actionId: this.drain.id, targetId: this.drain.target, spellId: this.drain.spell });
        this.drain = null;
    }

    private updateDrain() {
        const action = this.drain; if (!action) return;
        const player = this.sp.Game.getPlayer(), now = Date.now();
        if (!this.meleeSession || !player ||
            this.controller.lookupListener(RemoteServer).getMyRemoteRefrId() % 0x100000000 !== this.meleeOwner) {
            this.drain = null; return;
        }
        const equipped = player.getEquippedSpell(action.source)?.getFormID();
        const casting = !action.concentration || player.getAnimationVariableBool(action.source === 1 ? 'IsCastingRight' : 'IsCastingLeft');
        if (equipped !== action.spell || (!casting && now - action.created > 250) || now - action.created > 5000) {
            this.interruptDrain(); return;
        }
        if (!casting || now < action.next) return;
        action.next = now + 250;
        this.sendMeleePacket({ action: 'drain', session: this.meleeSession, actionId: action.id,
            targetId: action.target, spellId: action.spell });
    }

    private recentMagicHits: Map<number, number> = new Map();
    private meleeOwner = 0;
    private meleeSession: string | null = null;
    private nextMeleeHello = 0;
    private meleeHelloAttempts = 0;
    private swing: { id: string; until: number; target?: number } | null = null;
    private drain: { id: string; target: number; spell: number; source: number; concentration: boolean; created: number; next: number } | null = null;
}
