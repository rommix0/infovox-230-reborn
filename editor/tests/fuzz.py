"""Random edits of rule lines and the check that a parsed rule writes good bytes (spec:
Testing, 7)."""
from ivxedit.rules import Module, decode_module, encode_module, encode_rule

# characters and tokens an edit inserts: the syntax of both forms, names, digits
PIECES = list('RULE*SEKFP;#{}[]<>()0123456789abcf/+-−_^…∅→⁺↺⟵·‹›␠⁏:=@., ') + [
    ' ; ', ' ', 'del', '->', 'once', 'A', 'B', 't=', '.hi', 'v80']


def edit(rng, text):
    """text with 1-3 random edits: delete or insert a character, repeat or swap tokens"""
    for _ in range(rng.randint(1, 3)):
        k, c = rng.randrange(len(text) + 1), rng.random()
        if c < 0.35 and text:
            text = text[:k] + text[k + 1:]
        elif c < 0.7:
            text = text[:k] + rng.choice(PIECES) + text[k:]
        else:
            t = text.split(' ')
            i, j = rng.randrange(len(t)), rng.randrange(len(t))
            if c < 0.85:
                t.insert(j, t[i])
            else:
                t[i], t[j] = t[j], t[i]
            text = ' '.join(t)
    return text


def ph_ids(f):
    """PhRef -> its id in f as the writer lays the inventory out"""
    off, p = {}, 0
    for ph in f.phonemes:
        off[id(ph)] = p
        p += len(ph.to_bytes())
    return lambda ref: p if ref.ph is None else off[id(ref.ph)] + ref.delta


def reads_back(rule, ph_id):
    """True if the rule's bytes, in a module of their own, decode to the same words"""
    data = encode_module(Module(0, [rule]), ph_id)
    back, end = decode_module(data, 0)
    return (end == len(data) and len(back.rules) == 1
            and encode_rule(back.rules[0], None) == encode_rule(rule, ph_id))
