"""Fixture safety regressions. Updated: 19 September 2026, 12:51 CEST."""
import pathlib
import tempfile
import unittest
from unittest.mock import Mock, patch
import io
import json
from native_annotation_fixture import Fixture, APIERR_DELETED


class FixtureTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.fixture = Fixture(19723, r'C:\Tests\Disposable.pln', pathlib.Path(self.temp.name) / 'journal.jsonl')

    def test_changed_project_prevents_write(self):
        self.fixture.call = Mock(return_value={'projectPath': r'C:\Work\Production.pln'})
        with self.assertRaisesRegex(RuntimeError, 'authorized'):
            self.fixture.change('DeleteElements', {})
        self.assertEqual(self.fixture.call.call_count, 1)
        self.assertEqual(self.fixture.call.call_args.args[0], 'GetProjectInfo')

    def test_partial_create_remembers_ids_before_failure(self):
        element = {'guid': '11111111-1111-4111-8111-111111111111'}
        with self.assertRaises(RuntimeError):
            self.fixture.remember_created({'elements': [{'elementId': element}, {'error': {'code': -1}}]})
        self.assertEqual(self.fixture.owned, [element])

    def test_arbitrary_read_error_does_not_confirm_cleanup(self):
        self.fixture.owned = [{'guid': 'test'}]
        self.fixture.change = Mock(return_value={'success': True})
        self.fixture.call = Mock(return_value={'annotations': [{'error': {'code': -1}}]})
        with self.assertRaisesRegex(RuntimeError, 'Cleanup incomplete'):
            self.fixture.cleanup()
        self.assertEqual(len(self.fixture.owned), 1)
        self.fixture.call.return_value = {'annotations': [{'error': {'code': APIERR_DELETED}}]}
        self.fixture.cleanup()
        self.assertEqual(self.fixture.owned, [])

    def test_wrong_development_version_prevents_write(self):
        self.fixture.call = Mock(side_effect=[{'projectPath': r'C:\Tests\Disposable.pln'}, {'version': '1.5.8'}])
        with self.assertRaisesRegex(RuntimeError, 'development'):
            self.fixture.change('CreateTexts', {})
        self.assertEqual(self.fixture.call.call_count, 2)

    def test_partial_copy_captures_new_ids_before_failure(self):
        source = {'guid': '11111111-1111-4111-8111-111111111111'}
        copied = {'guid': '22222222-2222-4222-8222-222222222222'}
        self.fixture.owned = [source]
        response = {'succeeded': True, 'result': {'addOnCommandResponse': {
            'success': False, 'nativeReturnedElements': [{'elementId': copied}]}}}
        with patch('urllib.request.urlopen', return_value=io.BytesIO(json.dumps(response).encode())) as send:
            with self.assertRaises(RuntimeError):
                self.fixture.call('TransformElements', {
                    'copy': True, 'elements': [{'elementId': source}]})
        self.assertEqual(self.fixture.owned, [source, copied])
        self.assertEqual(send.call_count, 1)

    def test_foreign_copy_source_rejected_before_request(self):
        self.fixture.call = Mock()
        with self.assertRaisesRegex(RuntimeError, 'fixture-owned'):
            self.fixture.change('TransformElements', {'copy': True,
                'elements': [{'elementId': {'guid': 'foreign'}}]})
        self.fixture.call.assert_not_called()

    def test_guarded_partial_result_captures_ids_before_nested_error(self):
        element = {'guid': '11111111-1111-4111-8111-111111111111'}
        result = {'status': 'returned', 'commandName': 'CreateTexts', 'result': {
            'success': False, 'elements': [{'elementId': element}], 'error': {'code': -1}}}
        response = {'succeeded': True, 'result': {'addOnCommandResponse': result}}
        with patch('urllib.request.urlopen', return_value=io.BytesIO(json.dumps(response).encode())) as send:
            with self.assertRaises(RuntimeError):
                self.fixture.call('ExecuteGuardedCommand', {'commandName': 'CreateTexts', 'parameters': {}})
        self.assertEqual(self.fixture.owned, [element])
        self.assertEqual(send.call_count, 1)

    def test_unresolved_receipt_is_never_treated_as_success(self):
        for status in ['started', 'unknownOutcome', 'resultTooLarge']:
            with self.assertRaisesRegex(RuntimeError, 'not confirmed'):
                self.fixture.check_result('ExecuteGuardedCommand', {'status': status})


if __name__ == '__main__':
    unittest.main()
