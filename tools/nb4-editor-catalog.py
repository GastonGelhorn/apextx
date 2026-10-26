#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Generate the layout and widget catalogue used by the ApexTX Theme Editor.

Reads the zone maps, layout options and widget factories straight from the
firmware sources, so the editor offers exactly what the radio offers.

    python3 tools/nb4-editor-catalog.py > tools/apextx-theme-editor-catalog.json
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent / 'radio' / 'src'
GUI = ROOT / 'gui' / 'colorlcd'
LANGS = ('en', 'de', 'es')

MAP = {
    'LAYOUT_MAP_0': 0, 'LAYOUT_MAP_1SIXTH': 10, 'LAYOUT_MAP_1FIFTH': 12,
    'LAYOUT_MAP_1QTR': 15, 'LAYOUT_MAP_2FIFTH': 24, 'LAYOUT_MAP_1THIRD': 20,
    'LAYOUT_MAP_HALF': 30, 'LAYOUT_MAP_3FIFTH': 36, 'LAYOUT_MAP_2THIRD': 40,
    'LAYOUT_MAP_3QTR': 45, 'LAYOUT_MAP_4FIFTH': 48, 'LAYOUT_MAP_5SIXTH': 50,
    'LAYOUT_MAP_FULL': 60,
}
COMMON_OPTIONS = [
    {'name': 'TOP_BAR', 'type': 'Bool', 'default': 1},
    {'name': 'FLIGHT_MODE', 'type': 'Bool', 'default': 1},
    {'name': 'SLIDERS', 'type': 'Bool', 'default': 1},
    {'name': 'TRIMS', 'type': 'Bool', 'default': 1},
    {'name': 'MIRROR', 'type': 'Bool', 'default': 0},
]


def translations():
    table = {}
    for lang in LANGS:
        text = (ROOT / 'translations' / 'i18n' / f'{lang}.h').read_text(encoding='utf-8', errors='replace')
        table[lang] = dict(re.findall(r'^#define TR_(\w+)\s+"((?:[^"\\]|\\.)*)"\s*$', text, re.M))
    return table


TR = translations()


def label(key):
    """Translations for a TR_ key, falling back to English and the key."""
    return {lang: TR[lang].get(key) or TR['en'].get(key) or key for lang in LANGS}


def string_key(expr):
    expr = expr.strip()
    m = re.match(r'NB4_STR\((\w+)\)', expr)
    if m:
        return 'NB4_' + m.group(1)
    m = re.match(r'STR_DEF\(STR_(\w+)\)|STR_(\w+)', expr)
    if m:
        return m.group(1) or m.group(2)
    return None


def default_value(raw):
    raw = raw.strip()
    m = re.match(r'WIDGET_OPTION_VALUE_(?:SIGNED|UNSIGNED)\((-?\d+)\)', raw)
    if m:
        return int(m.group(1))
    m = re.match(r'(?:RGB2FLAGS|COLOR2FLAGS)\((\d+),\s*(\d+),\s*(\d+)\)', raw)
    if m:
        r, g, b = (int(v) for v in m.groups())
        return f'0x{r:02X}{g:02X}{b:02X}'
    m = re.match(r'WidgetOptionValue\{\s*\.boolValue\s*=\s*(true|false)\s*\}', raw)
    if m:
        return int(m.group(1) == 'true')
    if raw in ('true', 'false'):
        return int(raw == 'true')
    if re.fullmatch(r'-?\d+', raw):
        return int(raw)
    return raw


def split_entries(body):
    """Top-level {...} entries of a C initialiser list."""
    entries, depth, start = [], 0, None
    for i, ch in enumerate(body):
        if ch == '{':
            if depth == 0:
                start = i + 1
            depth += 1
        elif ch == '}':
            depth -= 1
            if depth == 0 and start is not None:
                entries.append(body[start:i])
    return entries


def split_args(entry):
    args, depth, cur = [], 0, ''
    for ch in entry:
        if ch == ',' and depth == 0:
            args.append(cur)
            cur = ''
            continue
        depth += ch in '(['
        depth -= ch in ')]'
        cur += ch
    if cur.strip():
        args.append(cur)
    return [a.strip() for a in args]


def layout_options(text, name):
    m = re.search(re.escape(name) + r'\[\]\s*=\s*\{(.*?)LAYOUT_OPTIONS_END', text, re.S)
    options = [dict(o) for o in COMMON_OPTIONS]
    if not m:
        return options
    for entry in split_entries(m.group(1)):
        args = split_args(entry)
        if len(args) >= 3:
            options.append({'name': string_key(args[0]), 'type': args[1].split('::')[-1],
                            'default': default_value(args[2])})
    return options


def layouts():
    result = []
    for path in sorted((GUI / 'layouts').glob('*.cpp')):
        text = path.read_text(encoding='utf-8')
        zm = re.search(r'zmap\[\]\s*=\s*\{(.*?)\};', text, re.S)
        fac = re.search(r'BaseLayoutFactory<\w+>\s+\w+\(\s*"(\w+)",\s*(?:"([^"]*)"|(STR_\w+)),\s*(\w+)', text)
        if not (zm and fac):
            continue
        values = [MAP[v] for v in re.findall(r'LAYOUT_MAP_\w+', re.sub(r'//.*', '', zm.group(1)))]
        zones = [values[i:i + 4] for i in range(0, len(values), 4)]
        name = fac.group(2) or label(fac.group(3)[4:])['en']
        opts = COMMON_OPTIONS if fac.group(4) == 'defaultLayoutOptions' else layout_options(text, fac.group(4))
        result.append({'id': fac.group(1), 'name': name, 'zones': zones, 'options': opts})
    # ApexTX Racing places its four zones around fixed status rows (see
    # RacingLayout::getZone()); the editor reproduces that geometry.
    result.insert(0, {'id': 'ApexTXRacing', 'name': 'ApexTX Racing', 'zones': [], 'options': [],
                      'racing': True})
    return result


def widget_options(text, cls, array=None):
    # The widget's own Class::options[], or a shared array such as trimOptions
    # (widgets/nb4_trim_row.h) passed to the factory by name.
    m = re.search(re.escape(cls) + r'::options\[\]\s*=\s*\{(.*?)\};', text, re.S)
    if not m and array:
        shared = text + (GUI / 'widgets' / 'nb4_trim_row.h').read_text(encoding='utf-8')
        m = re.search(r'WidgetOption\s+' + re.escape(array) + r'\[\]\s*=\s*\{(.*?)\};', shared, re.S)
    if not m:
        return []
    options = []
    for entry in split_entries(m.group(1)):
        args = split_args(entry)
        if len(args) < 2 or args[0] == 'nullptr':
            continue
        opt = {'name': string_key(args[0]), 'type': args[1].split('::')[-1]}
        if len(args) > 2:
            opt['default'] = default_value(args[2])
        if len(args) > 4:
            opt['min'], opt['max'] = default_value(args[3]), default_value(args[4])
        opt['label'] = label(opt['name'])
        options.append(opt)
    return options


def widgets():
    result = []
    files = sorted((GUI / 'widgets').glob('*.cpp')) + [GUI / 'mainview' / 'nb4_racing_layout.cpp']
    pattern = re.compile(
        r'(?:Nb4WidgetFactory|BaseWidgetFactory)<(\w+)>\s+\w+\(\s*"([^"]+)",\s*([\w:]+|nullptr),\s*([^;]*?)\);', re.S)
    racing = re.compile(r'RacingWidgetFactory\s+\w+\("(\w+)",\s*(NB4_STR\(\w+\))\);')
    for path in files:
        text = path.read_text(encoding='utf-8')
        for cls, wid, opts, name in pattern.findall(text):
            key = string_key(name.strip())
            options = widget_options(text, cls, opts) if opts != 'nullptr' else []
            result.append({'id': wid, 'label': label(key) if key else {l: wid for l in LANGS},
                           'options': options, 'nb4': wid.startswith(('NB4', 'Apex'))})
        for wid, name in racing.findall(text):
            result.append({'id': wid, 'label': label(string_key(name)), 'options': [], 'nb4': True,
                           'racing': True})
    order = {'ApexSteering': 0, 'ApexThrottle': 1, 'ApexTimer': 2, 'ApexStats': 3}
    result.sort(key=lambda w: (order.get(w['id'], 9), not w['nb4'], w['label']['en'].lower()))
    return result


def main():
    catalog = {
        'layouts': layouts(),
        'widgets': widgets(),
        'layoutOptionLabels': {o['name']: label(o['name']) for o in COMMON_OPTIONS},
    }
    sys.stdout.reconfigure(encoding='utf-8')
    json.dump(catalog, sys.stdout, ensure_ascii=False, indent=1)
    sys.stdout.write('\n')


if __name__ == '__main__':
    main()
