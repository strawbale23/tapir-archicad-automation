"""Compare native source with a pinned upstream snapshot. Updated 2026-09-19 CEST.

This is a source comparison, not a capability or SDK-coverage assessment.
The supplied GitHub tree verifies each original source file's Git blob hash.
"""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import re
from inventory_native_contracts import inventory

PREFIX = 'archicad-addon/Sources/'
SUFFIXES = {'.cpp', '.hpp', '.json'}


def source_files(root):
    return {p.relative_to(root).as_posix(): p for p in (root/PREFIX).rglob('*')
            if p.is_file() and p.suffix in SUFFIXES}


def normalized(path):
    return path.read_text(encoding='utf-8-sig').replace('\r\n','\n').replace('\r','\n')


def commands(root):
    text=normalized(root/PREFIX/'AddOnMain.cpp')
    return {m[0]: {'group':m[1], 'introducedVersion':m[2]}
            for m in re.findall(r'RegisterCommand<(\w+Command)>\s*\(\s*(\w+)\s*,\s*"([^"]+)"',text)}


def compare(original, current, tree):
    old,new=source_files(original),source_files(current)
    hashes={row['path']:row['sha'] for row in tree['tree'] if row['type']=='blob'}
    if tree.get('truncated'): raise ValueError('Complete upstream tree required')
    expected={p for p in hashes if p.startswith(PREFIX) and Path(p).suffix in SUFFIXES}
    if set(old)!=expected: raise ValueError('Original snapshot source files differ from pinned tree')
    for name,path in old.items():
        raw=path.read_bytes()
        blob=hashlib.sha1(b'blob '+str(len(raw)).encode()+b'\0'+raw).hexdigest()
        if blob!=hashes[name]: raise ValueError('Original file does not match upstream: '+name)
    before,after=commands(original),commands(current)
    old_contracts={row['class']:row for row in inventory(original)['commands']}
    new_contracts={row['class']:row for row in inventory(current)['commands']}
    def fields(schema, prefix=''):
        result=set()
        if not isinstance(schema,dict): return result
        for name,child in schema.get('properties',{}).items():
            path=prefix+'.'+name if prefix else name
            result.add(path)
            result.update(fields(child,path))
        if 'items' in schema: result.update(fields(schema['items'],prefix+'[]'))
        for branch in ('allOf','anyOf','oneOf'):
            for child in schema.get(branch,[]): result.update(fields(child,prefix))
        return result
    changes=[]
    unresolved=[]
    defaults=[]
    def default_input_classes(root):
        found=set()
        for path in (root/PREFIX).glob('*.hpp'):
            text=normalized(path)
            for name,body in re.findall(r'class\s+(\w+Command)\s*:\s*public\s+CommandBase\s*\{(.*?)\n\};',text,re.S):
                if 'GetInputParametersSchema' not in body: found.add(name)
        return found
    shared_defaults=default_input_classes(original)&default_input_classes(current)
    for command in sorted(before.keys()&after.keys()):
        left=old_contracts.get(command,{}).get('inputSchema')
        right=new_contracts.get(command,{}).get('inputSchema')
        if left is None or right is None:
            if command in shared_defaults: defaults.append(command)
            else: unresolved.append(command)
        elif left!=right:
            changes.append({'class':command,'source':new_contracts[command]['source'],
                'addedPropertyPaths':sorted(fields(right)-fields(left)),
                'removedPropertyPaths':sorted(fields(left)-fields(right)),
                'note':'Full schema changed; paths do not cover enums, defaults, references, applicability or runtime behaviour.'})
    return {
        'recordedAt':datetime.datetime.now().astimezone().isoformat(),
        'upstreamCommit':tree['sha'], 'originalFilesVerified':len(old),
        'scope':'Native cpp/hpp/json files; normalized text comparison. Commands are registered C++ classes, not verified runtime names.',
        'notClaimed':['complete SDK field coverage','native behaviour','all changes are new capabilities'],
        'originalCommandCount':len(before),'currentCommandCount':len(after),
        'addedCommands':[{'class':c,**after[c]} for c in sorted(after.keys()-before.keys())],
        'removedCommands':sorted(before.keys()-after.keys()),
        'existingCommandCount':len(before.keys()&after.keys()),
        'changedLiteralInputContracts':changes,
        'inputContractsUsingDefaultSchemaInBoth':defaults,
        'inputContractsRequiringManualComparison':unresolved,
        'addedFiles':sorted(new.keys()-old.keys()),
        'removedFiles':sorted(old.keys()-new.keys()),
        'changedFiles':[p for p in sorted(old.keys()&new.keys()) if normalized(old[p])!=normalized(new[p])],
        'unchangedFiles':[p for p in sorted(old.keys()&new.keys()) if normalized(old[p])==normalized(new[p])],
    }


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--original',type=Path,required=True)
    p.add_argument('--tree',type=Path,required=True)
    p.add_argument('--current',type=Path,default=Path(__file__).resolve().parents[1])
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    result=compare(a.original,a.current,json.loads(a.tree.read_text(encoding='utf-8-sig')))
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    lines=['# Comparison with original Tapir 1.5.8','',
           'Updated: '+result['recordedAt']+' (local timezone offset included).','',
           'Original revision: `'+result['upstreamCommit']+'`. All '+str(result['originalFilesVerified'])+
           ' original native C++/header/JSON files match the saved GitHub tree hashes.','',
           f"Original registrations: **{result['originalCommandCount']}**. Current: **{result['currentCommandCount']}**. "
           f"Added: **{len(result['addedCommands'])}**. Removed: **{len(result['removedCommands'])}**.",'',
           'These counts describe registered C++ command classes. They are not a completion percentage. '
           'Changes to existing commands are additional work and are not counted as new commands.','',
           '## Added command registrations','',
           '| Command class | Group |','|---|---|']
    lines += [f"| `{r['class']}` | {r['group']} |" for r in result['addedCommands']]
    lines += ['', '## Changes to existing input descriptions','',
              f"{len(result['changedLiteralInputContracts'])} directly readable input descriptions changed. "
              f"{len(result['inputContractsRequiringManualComparison'])} existing command descriptions need manual comparison because they are inherited or assembled in code. "
              f"{len(result['inputContractsUsingDefaultSchemaInBoth'])} commands directly inherit CommandBase's default schema method in both versions and are listed separately.",'',
              'The companion JSON file lists the affected commands and added/removed property paths. '
              'An unchanged property name does not mean unchanged behaviour. Values, constraints and native implementation also require review.','',
              '## Native files changed','']
    lines += ['- `'+p+'`' for p in result['changedFiles']]
    lines += ['', '## Meaning and limits','',
              'This is an exact comparison of the selected source files and registered classes, with line endings normalized. '
              'It does not identify the author of every intermediate change, establish SDK field coverage or establish native model behaviour. '
              'Use the functional guide for architectural explanations and the Git history for individual contributions.','']
    a.output.with_suffix('.md').write_text('\n'.join(lines),encoding='utf-8')
    print(json.dumps({k:result[k] for k in ['upstreamCommit','originalFilesVerified','originalCommandCount','currentCommandCount','removedCommands']}))
    print('Added commands:',len(result['addedCommands']),'changed source files:',len(result['changedFiles']))


if __name__=='__main__': main()
