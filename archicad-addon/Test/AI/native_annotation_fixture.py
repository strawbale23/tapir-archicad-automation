"""Native HTTP fixture; no MCP dependency and no office/library favourites.

Updated: 19 September 2026, 12:51 CEST (Europe/Berlin).
Defaults to read-only preflight. --run requires a disposable saved solo project.
Never saves. Journals all requests before sending, with UTC timestamps.
"""
import argparse
import datetime
import json
import math
import ntpath
import pathlib
import urllib.request
import uuid

# AC29 APIdefs_ErrorCodes.h and GSRoot/Definitions.hpp: non-existent database item.
APIERR_DELETED = -(1 << 31) + (262 << 16) + 302


class Fixture:
    def __init__(self, port, project, journal):
        if not 1 <= port <= 65535 or not ntpath.isabs(project):
            raise ValueError('Supply a valid port and absolute project path.')
        self.port, self.project, self.journal = port, project, pathlib.Path(journal)
        self.owned = []

    def record(self, **event):
        event['recordedAt'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
        with self.journal.open('a', encoding='utf-8') as stream:
            stream.write(json.dumps(event, ensure_ascii=False) + '\n')

    def call(self, name, parameters=None):
        request = {'command': 'API.ExecuteAddOnCommand', 'parameters': {
            'addOnCommandId': {'commandNamespace': 'TapirCommand', 'commandName': name},
            'addOnCommandParameters': parameters or {}}}
        self.record(request=request)
        req = urllib.request.Request(f'http://127.0.0.1:{self.port}',
            data=json.dumps(request).encode(), headers={'Content-Type': 'application/json'})
        # No retries: timeout after a write is an unknown outcome, not permission to repeat it.
        with urllib.request.urlopen(req, timeout=30) as stream:
            raw = json.load(stream)
        self.record(response=raw)
        if raw.get('succeeded') is not True:
            raise RuntimeError(f'{name}: transport/API failure: {raw}')
        result = raw['result']['addOnCommandResponse']
        self.observe_result(name, parameters or {}, result)
        self.check_result(name, result)
        return result

    def check_result(self, name, result):
        if name == 'ExecuteGuardedCommand':
            if result.get('status') != 'returned' or not isinstance(result.get('result'), dict):
                raise RuntimeError(f'{name}: execution outcome is not confirmed: {result}')
            self.check_result(result.get('commandName', ''), result['result'])
        if 'error' in result or result.get('success') is False:
            raise RuntimeError(f'{name}: {result}')
        for row in result.get('executionResults', []):
            if row.get('success') is not True:
                raise RuntimeError(f'{name}: {row}')

    def observe_result(self, name, parameters, result):
        """Capture fixture-owned IDs before partial-result errors are raised."""
        if name == 'ExecuteGuardedCommand' and isinstance(result.get('result'), dict):
            self.observe_result(parameters.get('commandName',''), parameters.get('parameters',{}), result['result'])
        if name.startswith('Create'):
            for row in result.get('elements', []):
                element = row.get('elementId')
                if element and element not in self.owned:
                    self.owned.append(element)
        if name == 'TransformElements' and parameters.get('copy') is True:
            sources = [row['elementId'] for row in parameters.get('elements', [])]
            if not sources or any(source not in self.owned for source in sources):
                raise RuntimeError('Fixture copies must use exclusively fixture-owned sources.')
            for row in result.get('nativeReturnedElements', []):
                element = row.get('elementId')
                if element and element not in sources and element not in self.owned:
                    self.owned.append(element)

    def guard(self):
        info = self.call('GetProjectInfo')
        actual = ntpath.normcase(ntpath.normpath(info.get('projectPath', '')))
        expected = ntpath.normcase(ntpath.normpath(self.project))
        if actual != expected or ntpath.splitext(actual)[1] != '.pln':
            raise RuntimeError('Active project is not the authorized saved solo test project.')
        if self.call('GetAddOnVersion').get('version') != '1.5.8-ai.4':
            raise RuntimeError('Expected development add-on 1.5.8-ai.4; no writes permitted.')

    def change(self, name, parameters):
        if name == 'TransformElements' and parameters.get('copy') is True:
            sources = [row['elementId'] for row in parameters.get('elements', [])]
            if not sources or any(source not in self.owned for source in sources):
                raise RuntimeError('Fixture copies must use exclusively fixture-owned sources.')
        self.guard()
        return self.call(name, parameters)

    def remember_created(self, result):
        rows = result.get('elements', [])
        # Capture every returned ID before checking partial failures, so cleanup knows them.
        for row in rows:
            if 'elementId' in row and row['elementId'] not in self.owned:
                self.owned.append(row['elementId'])
        if len(rows) != 1 or 'elementId' not in rows[0] or 'error' in rows[0]:
            raise RuntimeError(f'Unexpected creation result: {result}')
        return rows[0]['elementId']

    def read_text(self, element):
        self.guard()
        rows = self.call('GetAnnotationDetails', {'elements': [{'elementId': element}]})['annotations']
        if len(rows) != 1 or 'error' in rows[0] or rows[0].get('type') != 'Text':
            raise RuntimeError(f'Unexpected text readback: {rows}')
        return rows[0]

    def cleanup(self):
        errors = []
        for element in reversed(self.owned[:]):
            try:
                self.change('DeleteElements', {'elements': [{'elementId': element}]})
                # Confirm absence rather than trusting deletion's acknowledgement.
                rows = self.call('GetAnnotationDetails', {'elements': [{'elementId': element}]})['annotations']
                if len(rows) != 1 or rows[0].get('error', {}).get('code') != APIERR_DELETED:
                    raise RuntimeError('Fixture deletion is not confirmed by a native deleted-item error.')
                self.owned.remove(element)
            except Exception as error:
                errors.append(str(error))
        self.record(cleanupErrors=errors, remainingKnownIds=self.owned)
        if errors:
            raise RuntimeError('Cleanup incomplete; inspect the journal and known IDs: ' + '; '.join(errors))

    def run(self):
        token = 'Tapir native fixture ' + uuid.uuid4().hex
        try:
            element = self.remember_created(self.change('CreateTexts', {'textsData': [{
                'coordinate': {'x': 10000, 'y': 10000, 'z': 0}, 'text': token, 'height': 2.5}]}))
            if self.read_text(element).get('text') != token:
                raise RuntimeError('Created text did not match native readback.')
            revised = token + '\nÉtage – revised'
            self.change('ModifyTexts', {'textsWithDetails': [{'elementId': element,
                'text': revised, 'widthMillimetres': 80, 'nonBreaking': False}]})
            before = self.read_text(element)
            if before.get('text') != revised or not math.isclose(before['widthMillimetres'], 80, abs_tol=1e-6):
                raise RuntimeError('Text edit did not match native readback.')
            self.change('TransformElements', {'elements': [{'elementId': element}],
                'operation': 'Move', 'vector': {'x': 1.25, 'y': -0.5, 'z': 0}})
            after = self.read_text(element)
            for axis, distance in [('x', 1.25), ('y', -0.5)]:
                if not math.isclose(after['coordinate'][axis], before['coordinate'][axis] + distance, abs_tol=1e-6):
                    raise RuntimeError('Native move acknowledgement did not match actual placement.')
            if after.get('text') != revised:
                raise RuntimeError('Move changed text content unexpectedly.')
            self.record(fixture='passed', visualAcceptance='not performed', projectSaved=False)
        except Exception as error:
            self.record(fixture='failed', reason=str(error), remainingKnownIds=self.owned)
            raise
        finally:
            self.cleanup()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True, type=int)
    parser.add_argument('--project', required=True)
    parser.add_argument('--journal', type=pathlib.Path,
                        default=pathlib.Path('native-fixture-' + uuid.uuid4().hex + '.jsonl'))
    parser.add_argument('--run', action='store_true')
    args = parser.parse_args()
    fixture = Fixture(args.port, args.project, args.journal)
    fixture.guard()
    if args.run:
        fixture.run()
        message = 'Native text create/edit/move/readback and cleanup passed. Visual acceptance remains outstanding.'
    else:
        message = 'Read-only project/version preflight passed. No model changes; --run enables the disposable fixture.'
    print(datetime.datetime.now().astimezone().isoformat(), message)
    print('Journal:', args.journal.resolve())


if __name__ == '__main__':
    main()
