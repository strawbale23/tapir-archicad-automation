import subprocess, os, time, re, sys, argparse
from pathlib import Path

def MeasureExecutionTime (name, function, *args):
    print ('{} ... '.format (name), end='', flush=True)
    start = time.time ()
    result = function (*args)
    end = time.time ()
    print ('{0:.3f}s'.format (end - start))
    return result

def ExecuteTapirCommand (commandName, inputParameters = None):
    from archicad import ACConnection
    acConnection = ACConnection.connect ()
    if not acConnection:
        print ('Start Archicad before testing')
        raise RuntimeError('No Archicad connection')
    command = acConnection.types.AddOnCommandId ('TapirCommand', commandName)
    return acConnection.commands.ExecuteAddOnCommand (command, inputParameters)

def GetExampleScripts ():
    result = []
    quitExampleScriptFullPath = None
    examplesFolderPath = os.path.join (os.path.dirname (__file__), '..', 'Examples')
    for exampleFileName in os.listdir (examplesFolderPath):
        fileExtension = os.path.splitext (exampleFileName)[1]
        if fileExtension.lower () == '.py':
            scriptFullPath = os.path.join (examplesFolderPath, exampleFileName)
            if 'quit_archicad' in exampleFileName:
                quitExampleScriptFullPath = scriptFullPath
                continue
            result.append (scriptFullPath)
    result.sort ()
    if quitExampleScriptFullPath: result.append (quitExampleScriptFullPath)
    return result

def OpenProject (projectFilePath):
    return ExecuteTapirCommand ('OpenProject', {'projectFilePath': projectFilePath})

def ExecuteScript (exampleScriptFilePath):
    return subprocess.check_output ([sys.executable, exampleScriptFilePath, 'silent'], timeout=60)

def CompareOutput (bynaryOutput, expectedOutputFilePath, update_baselines=False):
    output = '\n'.join (bynaryOutput.decode ('utf-8').split ('\r\n'))
    for mask in [(re.compile(r'[{]?[0-9a-fA-F]{8}-([0-9a-fA-F]{4}-){3}[0-9a-fA-F]{12}[}]?'), '<GUID>'),
                 (re.compile(r'Time": [0-9]+'), 'Time": <TIME>'),
                 (re.compile(r'"(?P<fieldName>[^"]*(folder|path|directory|location)[^"]*)": "([A-Z]:(\\\\?[^\\"]+)+\\\\?|/?([^/"]+/)+)', re.IGNORECASE), r'"\g<fieldName>": "<PATH>')]:
        output = mask[0].sub (mask[1], output)

    baseline = Path (expectedOutputFilePath)
    if update_baselines:
        baseline.parent.mkdir (parents=True, exist_ok=True)
        baseline.write_text (output, encoding='utf-8')
        return True
    return baseline.is_file () and output == baseline.read_text (encoding='utf-8')


def main ():
    parser = argparse.ArgumentParser (description='Run examples against a disposable Archicad project. Examples may modify or close it.')
    parser.add_argument ('--update-baselines', action='store_true', help='Explicitly replace expected outputs after reviewing changes')
    args = parser.parse_args ()
    scripts = GetExampleScripts ()
    failed = []
    for index, script in enumerate (scripts):
        name = Path (script).name
        print (f'{index + 1}/{len(scripts)} {name}')
        try:
            response = OpenProject (str(Path(__file__).parent / 'TestProject.pla'))
            if isinstance(response, dict) and ('error' in response or response.get('success') is False):
                raise RuntimeError (response)
            output = ExecuteScript (script)
            passed = CompareOutput (output, Path(__file__).parent / 'ExpectedOutputs' / (name + '.output'), args.update_baselines)
        except (subprocess.SubprocessError, OSError, RuntimeError) as exc:
            print (f'Execution failed: {exc}')
            passed = False
        print ('PASSED' if passed else 'FAILED')
        if not passed: failed.append (name)
    print (f'{len(scripts)-len(failed)}/{len(scripts)} passed')
    return 1 if failed else 0


if __name__ == '__main__':
    raise SystemExit (main ())
