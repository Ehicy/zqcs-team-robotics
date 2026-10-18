"""Build original-style offline wiring desk. User confirmed: 2S to 12V."""
from pathlib import Path
import base64
import hashlib
import json

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
VISIBLE = REPO.parent / '方案可视化' / '下层车接线-20261016'
OUT = VISIBLE / 'chassis-wiring-editor.html'


def validate(data):
    modules = {m['id']: m for m in data['modules']}
    assert len(modules) == len(data['modules'])
    pins = {}
    for m in modules.values():
        assert 0 <= m['x'] <= data['board']['width'] - m['w'], m['id']
        assert 0 <= m['y'] <= data['board']['height'] - m['h'], m['id']
        for p in m['pins']:
            key = m['id'] + '.' + p['id']
            assert key not in pins, key
            pins[key] = p
            limit = m['h'] if p['side'] in ('left', 'right') else m['w']
            assert 15 <= p['offset'] <= limit - 15, key
            if p['role'] == 'ground':
                assert p.get('voltage', 0) == 0, key
    ids = set()
    for w in data['wires']:
        assert w['id'] not in ids, w['id']
        ids.add(w['id'])
        assert w['from'] in pins and w['to'] in pins, w
        assert w['from'] != w['to'], w
        a, b = pins[w['from']], pins[w['to']]
        if a['role'] == 'ground' or b['role'] == 'ground':
            assert a['role'] == b['role'] == w['kind'] == 'ground', w
        if w['kind'] == 'ground':
            assert a['role'] == b['role'] == 'ground', w
        if a['role'] == b['role'] == 'power' and 'voltage' in a and 'voltage' in b:
            assert a['voltage'] == b['voltage'], w
    pairs = {(w['from'], w['to']) for w in data['wires']}
    required = {
        ('battery.POS', 'fuse.IN'),
        ('fuse.OUT', 'relay.COM'), ('fuse.OUT', 'relay.VPLUS'),
        ('relay.VMINUS', 'ground.RELAY'),
        ('relay.NO', 'boost.IN_POS'), ('boost.OUT_POS', 'buck.IN_POS'),
        ('boost.OUT_POS', 'tb1.VM'), ('boost.OUT_POS', 'tb2.VM'),
        ('boost.IN_NEG', 'ground.BOOST_IN'), ('boost.OUT_NEG', 'ground.BOOST_OUT'),
        ('buck.OUT_POS', 'stm.V5'), ('buck.OUT_POS', 'esp.V5'),
        ('esp.GPIO12', 'servo.ARM'), ('esp.GPIO13', 'servo.FOREARM'), ('esp.GPIO14', 'servo.DOOR'),
    }
    assert required <= pairs, required - pairs
    assert not any('relay.NC' in (w['from'], w['to']) for w in data['wires'])
    assert not any('relay.VPLUS' in (w['from'], w['to']) and 'relay.NO' in (w['from'], w['to']) for w in data['wires'])
    assert ('relay.NO', 'buck.IN_POS') not in pairs  # Selected topology: buck follows boost.
    for wheel, position in ((1, '右前'), (2, '左前'), (3, '右后'), (4, '左后')):
        assert position in modules[f'm{wheel}']['title']
    return len(modules), len(data['wires'])


def build():
    data = json.loads((HERE / 'plan.json').read_text(encoding='utf-8'))
    counts = validate(data)
    template = (HERE / 'editor.template.html').read_text(encoding='utf-8')
    photo = (HERE / 'IMG_20261018_122821.jpg').read_bytes()
    raw = json.dumps(data, ensure_ascii=False, separators=(',', ':'))
    data['buildId'] = hashlib.sha256(raw.encode() + template.encode() + photo).hexdigest()[:14]
    html = template.replace('__PLAN_JSON__', json.dumps(data, ensure_ascii=False, separators=(',', ':')).replace('<', '\\u003c'))
    html = html.replace('__RELAY_BACK__', 'data:image/jpeg;base64,' + base64.b64encode(photo).decode())
    assert '__PLAN_JSON__' not in html and '__RELAY_BACK__' not in html
    VISIBLE.mkdir(parents=True, exist_ok=True)
    OUT.write_text(html, encoding='utf-8')
    (HERE / 'chassis-wiring-editor.html').write_text(html, encoding='utf-8')
    print(json.dumps({'output': str(OUT), 'modules': counts[0], 'wires': counts[1],
                      'bytes': len(html.encode()), 'buildId': data['buildId']}, ensure_ascii=False))


if __name__ == '__main__':
    build()
