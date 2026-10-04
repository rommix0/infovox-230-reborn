"""Which rules refer to which phoneme (the phoneme view's "Used by")."""
from .model import PhRef
from .rules import ph_positions


def phoneme_uses(f):
    """{Phoneme: [(module name, rule index), ...]}: every rule with a phoneme-id operand
    in that phoneme's record, once per rule, modules in name order (M0..M5)"""
    uses = {}
    for name in sorted(f.modules):
        for i, rule in enumerate(f.modules[name].rules):
            seen = set()
            for ins in rule.instrs:
                for k in ph_positions(ins):
                    a = ins.args[k]
                    if isinstance(a, PhRef) and a.ph is not None and id(a.ph) not in seen:
                        seen.add(id(a.ph))
                        uses.setdefault(a.ph, []).append((name, i))
    return uses
