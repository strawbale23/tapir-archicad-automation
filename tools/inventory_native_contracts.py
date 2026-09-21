"""Reproducible source inventory. Runtime GetCommandContracts is authoritative.

Literal input schemas can be extracted statically. Inherited/composed contracts
are explicitly unresolved rather than guessed from a neighbouring function.
"""
import argparse
import datetime
import hashlib
import json
import pathlib
import re


def inventory(root):
    sources = root / 'archicad-addon/Sources'
    registration = (sources / 'AddOnMain.cpp').read_text(encoding='utf-8-sig')
    implementations = {}
    hashes = {}
    for path in sorted(sources.rglob('*')):
        if not path.is_file() or path.suffix not in {'.cpp', '.hpp', '.json'}: continue
        raw = path.read_bytes()
        relative = path.relative_to(root).as_posix()
        hashes[relative] = hashlib.sha256(raw).hexdigest()
        if path.suffix != '.cpp': continue
        source = raw.decode('utf-8-sig')
        pattern = r'(\w+Command)::GetInputParametersSchema\s*\(\s*\)\s*const\s*\{\s*return\s+R"(?P<tag>\w*)\((?P<body>.*?)\)(?P=tag)";'
        for match in re.finditer(pattern, source, re.S):
            try: schema = json.loads(match['body'])
            except ValueError: continue
            implementations[match[1]] = {'source': relative, 'inputSchema': schema}
    commands = []
    for match in re.finditer(r'RegisterCommand<(\w+Command)>\s*\(\s*(\w+)\s*,\s*"([^"]+)"', registration):
        cls, group, version = match.groups()
        extracted = implementations.get(cls, {})
        commands.append({'class': cls, 'groupVariable': group, 'introducedVersion': version,
            'schemaStatus': 'literal-extracted' if extracted else 'runtime-discovery-required',
            'implementationReview': 'pending', 'nativeAcceptance': 'pending', **extracted})
    return {'formatVersion': 1, 'purpose': 'source inventory, not an SDK coverage or acceptance claim',
            'registeredClassCount': len(commands),
            'literalSchemaCount': sum(x['schemaStatus'] == 'literal-extracted' for x in commands),
            'commands': commands, 'sourceSha256': hashes}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=pathlib.Path, default=pathlib.Path(__file__).resolve().parents[1])
    parser.add_argument('--output', required=True, type=pathlib.Path)
    args = parser.parse_args()
    result = inventory(args.root)
    result['recordedAt'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf8')
    print(f"{result['registeredClassCount']} registrations; {result['literalSchemaCount']} literal input schemas")


if __name__ == '__main__': main()
