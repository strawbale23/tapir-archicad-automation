"""New native editing contracts. Updated 19 September 2026, 12:15 CEST.

These tests exercise the published request schemas; they do not simulate Archicad.
"""
import json
import pathlib
import re
import unittest
from jsonschema import Draft7Validator

SOURCES = pathlib.Path(__file__).resolve().parents[2] / 'Sources'
COMMON = json.loads((SOURCES / 'RFIX/Images/CommonSchemaDefinitions.json').read_text(encoding='utf-8-sig'))
ID = {'guid': '11111111-1111-4111-8111-111111111111'}

def validator(file, command):
    text = (SOURCES/file).read_text(encoding='utf-8-sig')
    text = text[text.index(command+'Command::GetInputParametersSchema'):]
    text = text.split('\n}\n', 1)[0]
    parts = re.findall(r'R"\((.*?)\)"', text, re.S)
    return Draft7Validator({**COMMON, **json.loads(''.join(parts))})

class ExtendedContracts(unittest.TestCase):
    def test_section_presentation_and_extent_controls_are_typed(self):
        v=validator('SectionCommands.cpp','ModifySectionSettings')
        def request(presentation):
            return {'sectionsWithSettings':[{'elementId':ID,'expectedModificationStamp':'123','horizontalRange':'Infinite',
                    'markerPosition':'Ends','presentation':presentation}]}
        self.assertTrue(v.is_valid(request({'referenceId':'S-01','lineTypeId':ID,'vectorHatching':True,
            'storyLineAppearance':'ScreenAndPrint','storyMarkerSizeMillimetres':4,'cutFillBackgroundPen':-1})))
        for invalid in [{},{'storyMarkerSizeMillimetres':0},{'cutFillBackgroundPen':-2},
                        {'cutLinePen':0},{'lineTypeId':{'index':1}},
                        {'storyLineAppearance':'Hidden'},{'markerPart':'invented'}]:
            self.assertFalse(v.is_valid(request(invalid)))

    def test_copy_view_settings_is_explicit_bounded_and_selective(self):
        v=validator('NavigatorCommands.cpp','CopyViewSettings')
        base={'sourceViewId':ID,'targetViewIds':[ID],'settingGroups':['Layers','Pens','Scale']}
        self.assertTrue(v.is_valid(base))
        for patch in [{'settingGroups':[]},{'settingGroups':['Layers','Layers']},
                      {'settingGroups':['Invented']},{'targetViewIds':[]},{'targetViewIds':[ID,ID]}]:
            self.assertFalse(v.is_valid({**base,**patch}))
        self.assertFalse(v.is_valid({k:x for k,x in base.items() if k!='sourceViewId'}))

    def test_composed_object_and_lamp_contracts_require_one_library_selector(self):
        source=(SOURCES/'ElementCreationCommands.cpp').read_text(encoding='utf-8-sig')
        def pieces(function):
            block=source[source.index('static GS::UniString '+function):]
            block=block[:block.index('\n}\n')+3]
            return re.findall(r'R"\((.*?)\)"',block,re.S)
        # These two contracts are assembled from literal fragments by the native
        # command; test the actual fragments for both branches of that builder.
        detail=pieces('BuildObjectLampDetailFields')
        prefix,suffix=pieces('BuildLibraryPartBasedSchema')
        for array,kind,lamp in [('objectsData','Object',False),('lampsData','Lamp',True)]:
            contract=json.loads(prefix%(array,kind,kind,'Exact loaded name')+detail[0]+(detail[1] if lamp else '')+suffix%array)
            v=Draft7Validator({**COMMON,**contract})
            base={'coordinates':{'x':0,'y':0,'z':0},'libraryPart':{'index':1,'guid':ID['guid'],'ownUnID':'loaded revision'}}
            self.assertTrue(v.is_valid({array:[base]}))
            self.assertTrue(v.is_valid({array:[{'coordinates':base['coordinates'],'libraryPartName':'Exact name'}]}))
            self.assertFalse(v.is_valid({array:[{**base,'libraryPartName':'Conflicting selection'}]}))
            self.assertFalse(v.is_valid({array:[{'coordinates':base['coordinates']}]}))

    def test_plan_query_is_bounded_and_context_bound(self):
        v=validator('PlanGeometryCommands.cpp','GetPlanPrimitives')
        base={'expectedDatabaseId':ID,'types':['Line','Polyline','Text'],'limit':20}
        self.assertTrue(v.is_valid(base))
        for patch in [{'types':['Wall']},{'types':['Line','Line']},{'types':[]},
                      {'offset':-1},{'limit':101},{'maxCoordinatesPerElement':10001}]:
            self.assertFalse(v.is_valid({**base,**patch}))
        self.assertFalse(v.is_valid({'types':['Line']}))

    def test_guarded_execution_requires_project_session_database_and_operation(self):
        v=validator('GuardedExecutionCommands.cpp','ExecuteGuardedCommand')
        base={'expectedSessionId':'session','expectedProjectPath':'C:/Tests/Disposable.pln',
              'expectedDatabaseId':ID,'operationId':'operation-1','commandName':'CreateTexts','parameters':{}}
        self.assertTrue(v.is_valid(base))
        for field in base:
            self.assertFalse(v.is_valid({k:x for k,x in base.items() if k!=field}))
        for token in ['', 'a'*129, 'spaces are invalid']:
            self.assertFalse(v.is_valid({**base,'operationId':token}))

    def test_partial_creation_retains_id_and_error_with_explicit_status(self):
        v=Draft7Validator({**COMMON,'$ref':'#/ElementIdOrError'})
        partial={'elementId':ID,'error':{'code':-1,'message':'Surface edit failed'},'status':'createdWithError'}
        self.assertTrue(v.is_valid(partial))
        self.assertTrue(v.is_valid({'elementId':ID}))
        self.assertTrue(v.is_valid({'error':partial['error']}))
        self.assertFalse(v.is_valid({**partial,'status':'success'}))
        self.assertFalse(v.is_valid({k:x for k,x in partial.items() if k!='status'}))

    def test_loaded_opening_identity_accepts_revision_guard(self):
        v=validator('ExtendedElementCommands.cpp','CreateDoors')
        part={'index':12,'guid':ID['guid'],'ownUnID':'observed native unique id'}
        base={'ownerWallId':ID,'centerOffset':1,'libraryPart':part}
        self.assertTrue(v.is_valid({'doorsData':[base]}))
        self.assertFalse(v.is_valid({'doorsData':[{**base,'libraryPart':{'index':12}}]}))

    def test_roof_plane_edits(self):
        v = validator('ExtendedElementCommands.cpp','ModifyRoofs')
        base = {'elementId': ID, 'angle': .3, 'positiveSide': False,
                'pivotLine': {'begCoordinate': {'x': 0, 'y': 0}, 'endCoordinate': {'x': 3, 'y': 0}}}
        self.assertTrue(v.is_valid({'roofsWithDetails': [base]}))
        self.assertFalse(v.is_valid({'roofsWithDetails': [{**base, 'angle': 1.6}]}))
        self.assertFalse(v.is_valid({'roofsWithDetails': [{**base, 'floorIndex': .5}]}))

    def test_drawing_placement_requires_context_and_revision(self):
        v = validator('DrawingPlacementCommands.cpp','PositionDrawings')
        base={'expectedDatabaseId': ID, 'dryRun': True, 'drawings': [{'elementId': ID,
              'expectedModificationStamp': '3', 'horizontal': 'Left', 'vertical': 'Top',
              'positionMillimetres': {'x': 20, 'y': 277}}]}
        self.assertTrue(v.is_valid(base))
        self.assertFalse(v.is_valid({'drawings': base['drawings']}))
        for field in ['expectedModificationStamp','positionMillimetres']:
            self.assertFalse(v.is_valid({**base,'drawings':[{k:x for k,x in base['drawings'][0].items() if k!=field}]}))

    def test_native_run_edit_target(self):
        v=validator('TextRunCommands.cpp','SetAnnotationRunStyles')
        base={'elementId': ID, 'expectedModificationStamp': '3',
              'runs': [{'paragraphIndex': 0, 'runIndex': 0, 'style': {'bold': True}}]}
        self.assertTrue(v.is_valid(base))
        for style in [{}, {'pen': 256}, {'sizeMillimetres': 0}, {'text': 'replace'}]:
            self.assertFalse(v.is_valid({**base,'runs':[{'paragraphIndex':0,'runIndex':0,'style':style}]}))

    def test_attribute_index_contracts_are_integer(self):
        for command,array in [('CreateLayers','layerDataArray'),('CreateBuildingMaterials','buildingMaterialDataArray'),
                              ('CreateLayerCombinations','layerCombinationDataArray'),('CreateSurfaces','surfaceDataArray'),
                              ('CreateFills','fillDataArray'),('CreateProfiles','profileDataArray'),('CreatePenTables','penTableDataArray')]:
            v=validator('AttributeCommands.cpp',command)
            # Inspect the published selector independently of unrelated required fields.
            spec=v.schema['properties'][array]['items']['properties']['index']
            index=Draft7Validator(spec)
            self.assertTrue(index.is_valid(1))
            self.assertFalse(index.is_valid('1'))
            self.assertFalse(index.is_valid(0))

    def test_segment_request(self):
        v = validator('AssemblySegmentCommands.cpp','SetAssemblySegments')
        base = {'elementId': ID, 'expectedModificationStamp': '123',
                'segments': [{'sourceIndex': 0, 'width': .3, 'height': .5}],
                'schemes': [{'lengthType': 'Proportional', 'value': 1}],
                'cuts': [{'type': 'Horizontal'}, {'type': 'Custom', 'angle': .4}]}
        self.assertTrue(v.is_valid(base))
        for patch in [{'segments': []}, {'expectedModificationStamp': 123},
                      {'segments': [{'sourceIndex': -1}]},
                      {'segments': [{'sourceIndex': 0, 'buildingMaterialId': ID, 'profileId': ID}]},
                      {'segments': [{'sourceIndex': 0, 'width': 0}]},
                      {'cuts': [{'type': 'Custom'}, {'type': 'Vertical'}]},
                      {'cuts': [{'type': 'Horizontal', 'angle': 0}, {'type': 'Vertical'}]},
                      {'schemes': [{'lengthType': 'Relative', 'value': 1}]}]:
            with self.subTest(patch=patch): self.assertFalse(v.is_valid({**base, **patch}))

    def test_guarded_dimension_anchors(self):
        v = validator('ExtendedElementCommands.cpp', 'CreateAssociativeDimensions')
        selection = {'hotspotIndex': 1, 'expectedModificationStamp': '2',
                     'expectedDatabaseId': ID, 'expectedCoordinate': {'x': 0, 'y': 1, 'z': 0}}
        point = {'elementId': ID, 'hotspot': selection}
        def request(p):
            return {'dimensionsData': [{'referencePoint': {'x': 0, 'y': 0},
                    'direction': {'x': 1, 'y': 0}, 'witnessPoints': [p, point]}]}
        self.assertTrue(v.is_valid(request(point)))
        for field in ['line', 'inIndex', 'nodeId']:
            self.assertFalse(v.is_valid(request({**point, field: False if field=='line' else 0})))
        for missing in selection:
            self.assertFalse(v.is_valid(request({'elementId': ID, 'hotspot': {k: x for k,x in selection.items() if k != missing}})))
        self.assertFalse(v.is_valid(request({'elementId': ID, 'nodeId': .5})))
        self.assertFalse(v.is_valid(request({'elementId': ID, 'special': 128})))

    def test_chain_accepts_same_hotspot_descriptor(self):
        v = validator('AnnotationCommands.cpp','EditDimensionChain')
        point = {'elementId': ID, 'hotspot': {'hotspotIndex': 0, 'expectedModificationStamp': '2',
                 'expectedDatabaseId': ID, 'expectedCoordinate': {'x': 0, 'y': 0, 'z': 0}}}
        base = {'elementId': ID, 'expectedModificationStamp': '3',
                'witnesses': [{'existingIndex': 0}, {'anchor': point}]}
        self.assertTrue(v.is_valid(base))
        self.assertFalse(v.is_valid({**base, 'witnesses': [{'existingIndex': 0}, {'anchor': {**point, 'line': True}}]}))

if __name__ == '__main__':
    unittest.main()
