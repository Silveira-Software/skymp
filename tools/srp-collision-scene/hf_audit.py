"""Audit every ACHR reference of a High Fantasy plugin.

Classifies each original reference the way the expansion needs it: whether it is
enabled, gated behind an enable parent (quest/script control), has a resolvable
base actor, and whether the base actor can be loaded at runtime. It does not
decide gameplay by itself - it produces the evidence a rollout needs.

Usage:
    python hf_audit.py --plugin <mihailhighfantasypack.esp> --out audit.json
"""
import argparse
import collections
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import esm  # noqa: E402

# REFR/ACHR record flags.
FLAG_DELETED = 0x00000020
FLAG_INITIALLY_DISABLED = 0x00000800
FLAG_PERSISTENT = 0x00000400


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--plugin', required=True)
    parser.add_argument('--out', required=True)
    args = parser.parse_args()

    name = os.path.basename(args.plugin)
    plugin = esm.Plugin(args.plugin, name)
    masters = plugin.masters
    self_index = len(masters)  # the plugin's own records carry this master index

    achrs = []
    for form_id in sorted(plugin.index):
        record = plugin.record(form_id)
        if record is None or record.sig != b'ACHR':
            continue
        achrs.append((form_id, record))

    buckets = collections.Counter()
    entries = []
    for form_id, record in achrs:
        flags = record.flags
        base_field = record.field(b'NAME')
        base = struct.unpack('<I', base_field)[0] if base_field and len(base_field) == 4 else None
        data = record.field(b'DATA')
        pos = list(struct.unpack_from('<3f', data, 0)) if data and len(data) >= 12 else None
        enable_parent = record.field(b'XESP')
        entry = {
            'reference': '0x%08x' % form_id,
            'localId': '0x%05x' % (form_id & 0xFFFFFF),
            'base': ('0x%08x' % base) if base is not None else None,
            'position': [round(v, 3) for v in pos] if pos else None,
            'flags': '0x%08x' % flags,
            'disabled': bool(flags & FLAG_INITIALLY_DISABLED),
            'deleted': bool(flags & FLAG_DELETED),
            'persistent': bool(flags & FLAG_PERSISTENT),
            'enableParent': enable_parent.hex() if enable_parent else None,
            'scale': struct.unpack('<f', record.field(b'XSCL'))[0]
            if record.field(b'XSCL') and len(record.field(b'XSCL')) == 4 else 1.0,
            'hasScripts': record.field(b'VMAD') is not None,
        }
        # Base actor has to exist in this plugin or in one of its masters; a
        # reference whose base cannot be resolved cannot be loaded at runtime.
        if base is None:
            entry['decision'] = 'base-missing'
        else:
            base_rec = plugin.record(base) if (base >> 24) == self_index else None
            if (base >> 24) == self_index and base_rec is None:
                entry['decision'] = 'base-missing'
            elif entry['deleted']:
                entry['decision'] = 'deleted'
            elif entry['disabled']:
                entry['decision'] = 'initially-disabled'
            elif entry['enableParent']:
                entry['decision'] = 'gated-by-enable-parent'
            else:
                entry['decision'] = 'candidate'
            if entry['decision'] in ('candidate',) and base_rec is not None:
                entry['baseSignature'] = base_rec.sig.decode('latin1')
                if base_rec.sig != b'NPC_':
                    entry['decision'] = 'base-not-actor'
        buckets[entry['decision']] += 1
        entries.append(entry)

    report = {
        'plugin': name,
        'masters': masters,
        'selfMasterIndex': self_index,
        'achrCount': len(achrs),
        'decisions': dict(buckets),
        'references': entries,
    }
    with open(args.out, 'w', encoding='utf8') as f:
        json.dump(report, f, indent=1, sort_keys=True)

    print('plugin %s: %d ACHR, %d masters (self index %d)' %
          (name, len(achrs), len(masters), self_index))
    for decision, count in buckets.most_common():
        print('  %-26s %d' % (decision, count))
    return 0


if __name__ == '__main__':
    sys.exit(main())
