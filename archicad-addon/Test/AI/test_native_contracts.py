import json
import pathlib
import re
import unittest
from jsonschema import Draft7Validator

ROOT = pathlib.Path(__file__).resolve().parents[3]
SOURCES = ROOT / 'archicad-addon/Sources'
COMMON = json.loads((SOURCES / 'RFIX/Images/CommonSchemaDefinitions.json').read_text(encoding='utf-8-sig'))


def schema(file, command):
    text = (SOURCES / file).read_text(encoding='utf-8-sig')
    block = text.split(command + 'Command::GetInputParametersSchema', 1)[1]
    block = block.split('\n}\n', 1)[0]
    parts = re.findall(r'R"(?P<tag>\w*)\((?P<body>.*?)\)(?P=tag)"', block, re.S)
    return {**COMMON, **json.loads(''.join(body for _tag, body in parts))}


class NativeContractTests(unittest.TestCase):
    def test_all_literal_contract_references_resolve(self):
        errors = []
        def references(node):
            if isinstance(node, dict):
                if '$ref' in node: yield node['$ref']
                for value in node.values(): yield from references(value)
            elif isinstance(node, list):
                for value in node: yield from references(value)
        for path in SOURCES.glob('*.cpp'):
            text = path.read_text(encoding='utf-8-sig')
            for match in re.finditer(r'R"(?P<tag>\w*)\((?P<body>.*?)\)(?P=tag)"', text, re.S):
                try: parsed = json.loads(match['body'])
                except ValueError: continue
                if not isinstance(parsed, dict) or not ('type' in parsed or '$ref' in parsed): continue
                document = {**COMMON, **parsed}
                queue, visited = list(references(parsed)), set()
                while queue:
                    ref = queue.pop()
                    if ref in visited or not ref.startswith('#/'): continue
                    visited.add(ref)
                    node = document
                    try:
                        for token in ref[2:].split('/'): node = node[token.replace('~1','/').replace('~0','~')]
                    except (KeyError, TypeError):
                        errors.append(path.name + ':' + ref); continue
                    queue.extend(references(node))
        self.assertEqual(errors, [])

    def test_stretch_requires_observed_hotspot_and_database_revision(self):
        validator = Draft7Validator(schema('NativeHotspotCommands.cpp','StretchElementAtHotspot'))
        guid = {'guid':'11111111-1111-4111-8111-111111111111'}
        request = {'elementId':guid,'expectedDatabaseId':guid,'expectedModificationStamp':'9007199254740993',
                   'hotspotIndex':1,'expectedCoordinate':{'x':4,'y':0,'z':0},'vector':{'x':1,'y':0,'z':0}}
        validator.validate(request)
        for field in ['expectedDatabaseId','expectedModificationStamp','expectedCoordinate','hotspotIndex']:
            self.assertFalse(validator.is_valid({key:value for key,value in request.items() if key!=field}))
        self.assertFalse(validator.is_valid({**request,'nativeNeigId':42}))

    def test_master_layout_creation_requires_explicit_paper_geometry(self):
        validator = Draft7Validator(schema('DocumentCreationCommands.cpp','CreateMasterLayouts'))
        master = {'name':'Synthetic A3','widthMillimetres':420,'heightMillimetres':297,
                  'leftMarginMillimetres':10,'rightMarginMillimetres':10,'ifExists':'ReuseIfMatching'}
        validator.validate({'masters':[master]})
        for update in [{'heightMillimetres':0},{'leftMarginMillimetres':-1},{'ifExists':'Overwrite'}]:
            self.assertFalse(validator.is_valid({'masters':[{**master,**update}]}))
        self.assertFalse(validator.is_valid({'masters':[{key:value for key,value in master.items() if key!='widthMillimetres'}]}))

    def test_column_creation_editing_conflicts_and_display_parity(self):
        guid = {'guid':'11111111-1111-4111-8111-111111111111'}
        for file,command,array,base in [('ElementCreationCommands.cpp','CreateColumns','columnsData',{'coordinates':{'x':0,'y':0,'z':0}}),
                                       ('ExtendedElementCommands.cpp','ModifyColumns','columnsWithDetails',{'elementId':guid})]:
            validator = Draft7Validator(schema(file,command))
            validator.validate({array:[{**base,'height':3,'slantAngle':0.1,'slantDirectionAngle':1,'wrapping':False,'cutFillPen':{'overridden':False}}]})
            for invalid in [{'height':3,'relativeTopStory':1},{'relativeTopStory':1.5},{'profileId':guid,'buildingMaterialId':guid},{'profileId':guid,'circleBased':True}]:
                self.assertFalse(validator.is_valid({array:[{**base,**invalid}]}))

    def test_beam_holes_have_explicit_shape_and_clear_semantics(self):
        guid = {'guid':'11111111-1111-4111-8111-111111111111'}
        for command,array,base in [('CreateBeams','beamsData',{'begCoordinate':{'x':0,'y':0},'endCoordinate':{'x':4,'y':0},'zCoordinate':0}),
                                   ('ModifyBeams','beamsWithDetails',{'elementId':guid})]:
            validator = Draft7Validator(schema('ExtendedElementCommands.cpp',command))
            hole = {'type':'Rectangular','centerX':1,'centerZ':0,'width':0.1,'height':0.2}
            validator.validate({array:[{**base,'holes':[hole]}]})
            validator.validate({array:[{**base,'holes':[],'beamShape':'Straight','isFlipped':False}]})
            for invalid in [{'type':'Circular'},{'height':0}]:
                self.assertFalse(validator.is_valid({array:[{**base,'holes':[{**hole,**invalid}]}]}))
            self.assertFalse(validator.is_valid({array:[{**base,'holes':[{key:value for key,value in hole.items() if key!='height'}]}]}))
            self.assertFalse(validator.is_valid({array:[{**base,'buildingMaterialId':guid,'profileId':guid}]}))

    def test_commands_using_common_references_load_the_common_schema(self):
        failures = []
        for path in SOURCES.glob('*.cpp'):
            text = path.read_text(encoding='utf-8-sig')
            for match in re.finditer(r'(\w+Command)::\1\s*\(\s*\)\s*:\s*CommandBase\s*\(CommonSchema::NotUsed\)', text):
                for method in ['GetInputParametersSchema','GetRawResponseSchema']:
                    start = text.find(match[1] + '::' + method)
                    if start >= 0 and '#/' in text[start:text.find('\n}',start)]:
                        failures.append(path.name + ':' + match[1] + ':' + method)
        self.assertEqual(failures, [])

    def test_configured_views_and_both_renovation_identity_forms(self):
        validator = Draft7Validator(schema('NavigatorCommands.cpp','CreateViewsInViewMap'))
        guid = '11111111-1111-4111-8111-111111111111'
        for identity in [guid, {'guid':guid}]:
            validator.validate({'viewsData':[{'navigatorItemId':{'guid':guid},
                'name':'Plan étage','viewSettings':{'drawingScale':50,'renovationFilterGuid':identity}}]})
        self.assertFalse(validator.is_valid({'viewsData':[]}))
        self.assertFalse(validator.is_valid({'viewsData':[{'navigatorItemId':{'guid':guid},'name':'x'*256}]}))

    def test_repeatable_setup_collision_policies(self):
        guid = {'guid':'11111111-1111-4111-8111-111111111111'}
        folders = Draft7Validator(schema('NavigatorCommands.cpp','CreateViewMapFolder'))
        folders.validate({'folderName':'Plans','ifExists':'Reuse'})
        self.assertFalse(folders.is_valid({'folderName':'Plans','ifExists':'Overwrite'}))
        views = Draft7Validator(schema('NavigatorCommands.cpp','CreateViewsInViewMap'))
        view = {'navigatorItemId':guid,'name':'Ground floor','ifExists':'UpdateMatchingSource'}
        views.validate({'viewsData':[view]})
        for update in [{'name':''},{'ifExists':'Overwrite'}]:
            self.assertFalse(views.is_valid({'viewsData':[{**view,**update}]}))
        self.assertFalse(views.is_valid({'viewsData':[{key:value for key,value in view.items() if key!='name'}]}))
        sheets = Draft7Validator(schema('DocumentCreationCommands.cpp','CreateLayout'))
        sheet = {'layoutName':'Plan','masterLayoutName':'A3','createMissingMaster':False,'ifExists':'ReuseIfMatching'}
        sheets.validate({'layoutsData':[sheet]})
        for update in [{'masterNavigatorItemId':guid},{'ifExists':'Overwrite'}]:
            self.assertFalse(sheets.is_valid({'layoutsData':[{**sheet,**update}]}))
        self.assertFalse(sheets.is_valid({'layoutsData':[{'layoutName':'Plan'}]}))

    def test_layout_custom_data_has_explicit_merge_mode(self):
        validator = Draft7Validator(schema('DocumentCreationCommands.cpp','SetLayoutSettings'))
        request = {'layoutsData':[{'layoutDatabaseId':{'guid':'11111111-1111-4111-8111-111111111111'},
            'customDataMode':'Merge','customData':[{'customSchemeName':'Drawing title','customSchemeValue':'Plan'}]}]}
        validator.validate(request)
        request['layoutsData'][0]['customDataMode'] = 'Append'
        self.assertFalse(validator.is_valid(request))
        self.assertFalse(validator.is_valid({'layoutsData':[{'layoutDatabaseId':{'guid':'11111111-1111-4111-8111-111111111111'},'customDataMode':'Merge'}]}))

    def test_room_image_allocation_limits(self):
        validator = Draft7Validator(schema('ElementCommands.cpp','GetRoomImage'))
        request = {'zoneId':{'guid':'11111111-1111-4111-8111-111111111111'},'width':1024,'height':768,'scale':0.02}
        validator.validate(request)
        for extra in [{'width':0},{'height':4097},{'scale':0}]:
            self.assertFalse(validator.is_valid({**request,**extra}))

    def test_core_creation_exposes_explicit_construction(self):
        material = {'guid':'11111111-1111-4111-8111-111111111111'}
        wall = {'begCoordinate':{'x':0,'y':0},'endCoordinate':{'x':4,'y':0},
                'height':3,'thickness':0.2,'thickness1':0.3,'geometryType':'Trapezoid',
                'profileType':'Normal','structureType':'Basic','buildingMaterialId':material}
        Draft7Validator(schema('ExtendedElementCommands.cpp','CreateWalls')).validate({'wallsData':[wall]})
        slab = {'level':0,'thickness':0.2,'structureType':'Basic','buildingMaterialId':material,
                'referencePlaneLocation':'Top','polygonCoordinates':[{'x':0,'y':0},{'x':4,'y':0},{'x':4,'y':3},{'x':0,'y':3}]}
        validator = Draft7Validator(schema('ElementCreationCommands.cpp','CreateSlabs'))
        validator.validate({'slabsData':[slab]})
        self.assertFalse(validator.is_valid({'slabsData':[{**slab,'structureType':'Profile'}]}))

    def test_opening_replacement_requires_explicit_parameter_reset_and_size(self):
        for command, field in [('ModifyWindows','windowsWithDetails'),('ModifyDoors','doorsWithDetails')]:
            validator = Draft7Validator(schema('ExtendedElementCommands.cpp',command))
            item = {'elementId':{'guid':'11111111-1111-4111-8111-111111111111'},
                    'libraryPart':{'index':42,'guid':'22222222-2222-4222-8222-222222222222'},
                    'width':0.9,'height':2.1,'parameterPolicy':'UseSelectedPartDefaults'}
            self.assertTrue(validator.is_valid({field:[item]}))
            for omitted in ['parameterPolicy','height','width']:
                self.assertFalse(validator.is_valid({field:[{key:value for key,value in item.items() if key!=omitted}]}))
            self.assertFalse(validator.is_valid({field:[{**item,'parameterPolicy':'PreserveAll'}]}))

    def test_annotation_style_units_and_spacing_limits(self):
        validator = Draft7Validator(schema('AnnotationCommands.cpp','SetAnnotationTextStyle'))
        elements = [{'elementId':{'guid':'11111111-1111-4111-8111-111111111111'}}]
        self.assertTrue(validator.is_valid({'elements':elements,'style':{'fontName':'Arial','sizeMillimetres':2.5,'bold':True,'alignment':'Left'}}))
        for style in [{},{'sizeMillimetres':0},{'lineSpacingFactor':1.2},{'widthFactor':0.5},{'fontName':''},{'alignment':'Start'},{'pen':0}]:
            self.assertFalse(validator.is_valid({'elements':elements,'style':style}))

    def test_polygonal_wall_edit_requires_revision_and_new_reference(self):
        validator = Draft7Validator(schema('PolygonalWallCommands.cpp','ModifyPolygonalWallGeometry'))
        request = {'elementId':{'guid':'11111111-1111-4111-8111-111111111111'},
                   'expectedModificationStamp':'9007199254740993','referenceEdgeIndex':0,
                   'polygonOutline':[{'x':0,'y':0},{'x':4,'y':0},{'x':4,'y':0.3},{'x':0,'y':0.3}]}
        self.assertTrue(validator.is_valid(request))
        self.assertFalse(validator.is_valid({**request,'height':3}))
        self.assertFalse(validator.is_valid({**request,'expectedModificationStamp':9007199254740993}))
        self.assertFalse(validator.is_valid({key:value for key,value in request.items() if key!='referenceEdgeIndex'}))

    def test_repeated_transforms_require_copy_and_limit_count(self):
        validator = Draft7Validator(schema('ElementCommands.cpp','TransformElements'))
        request = {'elements':[{'elementId':{'guid':'11111111-1111-4111-8111-111111111111'}}],
                   'operation':'Move','vector':{'x':1,'y':0,'z':0},'copy':True,'repeatCount':5,'requireAllGroupMembers':True}
        self.assertTrue(validator.is_valid(request))
        self.assertFalse(validator.is_valid({**request,'copy':False}))
        self.assertFalse(validator.is_valid({**request,'repeatCount':101}))
        mirror = {key:value for key,value in request.items() if key!='vector'}
        mirror.update(operation='Mirror',axisStart={'x':0,'y':0},axisEnd={'x':1,'y':0})
        self.assertFalse(validator.is_valid(mirror))
        self.assertTrue(validator.is_valid({**mirror,'repeatCount':1}))

    def test_native_transaction_results_are_not_discarded(self):
        discarded = []
        for path in SOURCES.glob('*.cpp'):
            for number, line in enumerate(path.read_text(encoding='utf-8-sig').splitlines(), 1):
                if re.match(r'\s*ACAPI_CallUndoableCommand\s*\(', line):
                    discarded.append(f'{path.name}:{number}')
        self.assertEqual(discarded, [], 'Native transaction errors must reach the command result')

    def test_library_ancestry_resolves_both_loaded_identities(self):
        validator = Draft7Validator(schema('LibraryCommands.cpp','CheckLibraryPartAncestry'))
        part = {'index':1,'guid':'11111111-1111-4111-8111-111111111111'}
        self.assertTrue(validator.is_valid({'candidate':part,'ancestor':part}))
        self.assertFalse(validator.is_valid({'candidate':part,'ancestor':{'index':2}}))
        self.assertFalse(validator.is_valid({'candidate':part,'ancestor':{**part,'index':0}}))

    def test_view_saved_settings_flags_are_explicit_booleans(self):
        validator = Draft7Validator(schema('NavigatorCommands.cpp', 'SetViewSettings'))
        def request(settings):
            return {'navigatorItemIdsWithViewSettings':[{'navigatorItemId':{'guid':'11111111-1111-4111-8111-111111111111'},'viewSettings':settings}]}
        flags = ['saveDispOpt','saveLaySet','saveDScale','saveDim','savePenSet','saveStructureDisplay']
        self.assertTrue(validator.is_valid(request({key:False for key in flags})))
        self.assertTrue(validator.is_valid(request({'layerCombination':'Plans','saveLaySet':True})))
        for key in flags:
            self.assertFalse(validator.is_valid(request({key:'false'})))

    def test_component_quantity_queries_are_bounded_and_explicit(self):
        validator = Draft7Validator(schema('NativeProjectCommands.cpp','GetNativeComponentQuantities'))
        element = {'elementId':{'guid':'11111111-1111-4111-8111-111111111111'}}
        self.assertTrue(validator.is_valid({'elements':[element],'minimumWallOpeningArea':0.5}))
        self.assertFalse(validator.is_valid({'elements':[]}))
        self.assertFalse(validator.is_valid({'elements':[element]*101}))
        self.assertFalse(validator.is_valid({'elements':[element],'minimumWallOpeningArea':-1}))

    def test_unplaced_library_inspection_requires_index_and_identity(self):
        validator=Draft7Validator(schema('ElementGDLParameterCommands.cpp','GetLibraryPartParameters'))
        self.assertTrue(validator.is_valid({'libraryPart':{'index':12,'guid':'11111111-1111-4111-8111-111111111111'}}))
        self.assertFalse(validator.is_valid({'libraryPart':{'index':12}}))
        self.assertFalse(validator.is_valid({'libraryPart':{'index':0,'guid':'11111111-1111-4111-8111-111111111111'}}))
    def test_dimension_chain_requires_revision_and_explicit_witness_choice(self):
        validator=Draft7Validator(schema('AnnotationCommands.cpp','EditDimensionChain'))
        base={'elementId':{'guid':'11111111-1111-4111-8111-111111111111'},'expectedModificationStamp':'9007199254740993','witnesses':[{'existingIndex':0},{'existingIndex':2}]}
        self.assertTrue(validator.is_valid(base))
        self.assertFalse(validator.is_valid({**base,'expectedModificationStamp':9007199254740993}))
        self.assertFalse(validator.is_valid({**base,'witnesses':[{'existingIndex':0}]}))
        anchor={'elementId':base['elementId'],'line':True,'inIndex':11}
        self.assertTrue(validator.is_valid({**base,'witnesses':[{'existingIndex':0},{'anchor':anchor}]}))
        self.assertFalse(validator.is_valid({**base,'witnesses':[{'existingIndex':0},{'existingIndex':1,'anchor':anchor}]}))

    def test_polygon_wall_uses_explicit_material_and_footprint(self):
        validator=Draft7Validator(schema('PolygonalWallCommands.cpp','CreatePolygonalWalls'))
        item={'polygonOutline':[{'x':0,'y':0},{'x':4,'y':0},{'x':4,'y':0.2},{'x':0,'y':0.2}],
              'height':2.8,'buildingMaterialId':{'guid':'11111111-1111-4111-8111-111111111111'}}
        self.assertTrue(validator.is_valid({'wallsData':[item]}))
        self.assertFalse(validator.is_valid({'wallsData':[{**item,'height':0}]}))
        self.assertFalse(validator.is_valid({'wallsData':[{**item,'structureType':'Composite'}]}))
        self.assertFalse(validator.is_valid({'wallsData':[{k:v for k,v in item.items() if k!='buildingMaterialId'}]}))
    def test_slab_topology_actions_require_their_own_inputs(self):
        validator = Draft7Validator(schema('SlabTopologyCommands.cpp', 'EditSlabTopology'))
        base = {'elementId': {'guid': '11111111-1111-4111-8111-111111111111'}, 'contourIndex': 0,
                'expectedContourCount': 1, 'expectedContour': [{'x':0,'y':0},{'x':4,'y':0},{'x':4,'y':3},{'x':0,'y':3}]}
        insert = {**base, 'operation':'InsertVertex','vertexIndex':0,'coordinate':{'x':2,'y':0}}
        self.assertTrue(validator.is_valid(insert))
        self.assertFalse(validator.is_valid({**insert,'operation':'DeleteVertex'}))
        self.assertFalse(validator.is_valid({**base,'operation':'DeleteHole'}))
        self.assertTrue(validator.is_valid({**base,'operation':'DeleteHole','contourIndex':1,'expectedContourCount':2}))
        self.assertTrue(validator.is_valid({**base,'operation':'AddHole','hole':[{'x':1,'y':1},{'x':2,'y':1},{'x':1,'y':2}]}))
        self.assertFalse(validator.is_valid({**base,'operation':'AddHole','hole':[]}))

    def test_section_geometry_and_vertical_range_are_explicit(self):
        validator = Draft7Validator(schema('SectionCommands.cpp', 'ModifySectionSettings'))
        base={'elementId':{'guid':'11111111-1111-4111-8111-111111111111'}}
        def valid(settings): return validator.is_valid({'sectionsWithSettings':[{**base,**settings}]})
        self.assertTrue(valid({'verticalRange':{'mode':'Limited','minimum':0,'maximum':3}}))
        self.assertFalse(valid({'verticalRange':{'mode':'Limited','maximum':3}}))
        self.assertFalse(valid({'verticalRange':{'mode':'Infinite','minimum':0}}))
        self.assertTrue(valid({'geometry':{'startCoordinate':{'x':0,'y':0},'endCoordinate':{'x':3,'y':0},'depth':2}}))
        self.assertFalse(valid({'geometry':{'startCoordinate':{'x':0,'y':0},'endCoordinate':{'x':3,'y':0},'depth':0}}))
        self.assertFalse(valid({'name':'a'*256}))

    def test_quantity_catalogue_has_unique_family_names_and_roof_units(self):
        text=(SOURCES/'NativeQuantityDefinitions.hpp').read_text()
        entries=re.findall(r'\{(API_\w+ID), "([^"]+)", "([^"]+)", "([^"]+)"',text)
        keys=[(family,name) for family,name,field,unit in entries]
        self.assertEqual(len(keys),len(set(keys)))
        catalogue={(family,name):(field,unit) for family,name,field,unit in entries}
        self.assertEqual(catalogue[('API_RoofID','roof.grossVolume')],('roof.grossVolume','m3'))
        self.assertEqual(catalogue[('API_RoofID','roof.contourArea')],('roof.contourArea','m2'))
        self.assertEqual(catalogue[('API_WallID','wall.volume')],('wall.volume','m3'))
    def test_relink_bounds_and_required_layout(self):
        validator = Draft7Validator(schema('DocumentCreationCommands.cpp', 'ChangeDrawingLink'))
        identity = {'guid': '11111111-1111-4111-8111-111111111111'}
        item = {'elementId': identity, 'navigatorItemId': identity, 'layoutDatabaseId': identity}
        self.assertTrue(validator.is_valid({'drawingsWithNewLinks': [item]}))
        self.assertFalse(validator.is_valid({'drawingsWithNewLinks': []}))
        self.assertFalse(validator.is_valid({'drawingsWithNewLinks': [item] * 101}))
        self.assertFalse(validator.is_valid({'drawingsWithNewLinks': [{'elementId': identity, 'navigatorItemId': identity}]}))

    def test_relink_failure_retains_diagnostic_mapping_without_success_ids(self):
        source = (SOURCES / 'DocumentCreationCommands.cpp').read_text(encoding='utf-8-sig')
        block = source.split('ChangeDrawingLinkCommand::GetRawResponseSchema', 1)[1]
        contract = {**COMMON, **json.loads(re.search(r'R"\((.*?)\)"', block, re.S)[1])}
        Draft7Validator.check_schema(contract)
        validator = Draft7Validator(contract)
        identity = {'guid': '11111111-1111-4111-8111-111111111111'}
        result = {'elements': [{'error': {'code': -1, 'message': 'Transaction failed'}}],
                  'transactionStatus': 'failed', 'titleParametersAndPlacementNotPreserved': True,
                  'replacements': [{'oldElementId': identity, 'newElementId': identity}]}
        self.assertTrue(validator.is_valid(result))
        self.assertFalse(validator.is_valid({**result, 'transactionStatus': 'success'}))

    def test_drawing_creation_supports_rotation_but_not_output_only_scale(self):
        validator = Draft7Validator(schema('DocumentCreationCommands.cpp', 'CreateDrawings'))
        item = {'navigatorItemId': {'guid': '11111111-1111-4111-8111-111111111111'},
                'name': 'Plan', 'position': {'x': 0.1, 'y': 0.2}, 'angle': 0.5,
                'scale': 1, 'modelOffset': {'x': 0, 'y': 0}}
        self.assertTrue(validator.is_valid({'drawingsData': [item]}))
        for fields in [{'scale': 0}, {'scale': -1}, {'drawingScale': 0.02}]:
            self.assertFalse(validator.is_valid({'drawingsData': [{**item, **fields}]}))
        settings = Draft7Validator({**COMMON, '$ref': '#/DrawingSettings'})
        self.assertTrue(settings.is_valid({'ratio': 2}))
        self.assertFalse(settings.is_valid({'ratio': 0}))
        self.assertFalse(settings.is_valid({'drawingScale': 0.02}))

    def test_dimension_readback_preserves_unsigned_native_node_identifiers(self):
        validator = Draft7Validator({**COMMON, '$ref': '#/DimensionData'})
        point = {'line': False, 'inIndex': 1, 'special': 0,
                 'nodeType': 0, 'nodeStatus': 0, 'nodeId': 4294967295}
        self.assertTrue(validator.is_valid({'witnessPoints': [point],
                                          'linePen': 5, 'horizontalText': False, 'usedIn3D': False}))
        for value in [-1, 4294967296, 1.5]:
            self.assertFalse(validator.is_valid({'witnessPoints': [{**point, 'nodeId': value}]}))

    def test_slab_vertex_edit_requires_stale_geometry_check(self):
        validator = Draft7Validator(schema('SlabTopologyCommands.cpp', 'MoveSlabVertices'))
        element = {'guid': '11111111-1111-4111-8111-111111111111'}
        move = {'contourIndex': 0, 'vertexIndex': 1,
                'expectedCoordinate': {'x': 3, 'y': 0}, 'coordinate': {'x': 4, 'y': 0}}
        self.assertTrue(validator.is_valid({'elementId': element, 'moves': [move]}))
        for replacement in [{'contourIndex': -1}, {'vertexIndex': 1.5},
                            {'expectedNativeVertexId': -1}, {'expectedNativeVertexId': 4294967296}]:
            self.assertFalse(validator.is_valid({'elementId': element, 'moves': [{**move, **replacement}]}))
        incomplete = {key: value for key, value in move.items() if key != 'expectedCoordinate'}
        self.assertFalse(validator.is_valid({'elementId': element, 'moves': [incomplete]}))
        self.assertFalse(validator.is_valid({'elementId': element, 'moves': []}))

    def test_dimension_settings_do_not_accept_witness_replacement(self):
        validator = Draft7Validator(schema('AnnotationCommands.cpp', 'ModifyDimensionSettings'))
        element = {'elementId': {'guid': '11111111-1111-4111-8111-111111111111'}}
        self.assertTrue(validator.is_valid({'dimensionsWithSettings': [{**element, 'linePen': 5}]}))
        self.assertFalse(validator.is_valid({'dimensionsWithSettings': [{**element, 'witnessPoints': []}]}))
        self.assertFalse(validator.is_valid({'dimensionsWithSettings': [element]}))

    def test_quantity_requests_are_bounded_and_rules_explicit(self):
        validator = Draft7Validator(schema('NativeProjectCommands.cpp', 'GetNativeQuantities'))
        item = {'elementId': {'guid': '11111111-1111-4111-8111-111111111111'}}
        self.assertTrue(validator.is_valid({'elements': [item], 'minimumWallOpeningArea': 0}))
        for request in [{'elements': []}, {'elements': [item] * 101},
                        {'elements': [item], 'minimumWallOpeningArea': -1}]:
            self.assertFalse(validator.is_valid(request))

    def test_renovation_role_cannot_be_confused_with_view_filter(self):
        validator = Draft7Validator(schema('NativeProjectCommands.cpp', 'SetElementRenovationStatus'))
        base = {'elements': [{'elementId': {'guid': '11111111-1111-4111-8111-111111111111'}}]}
        for role in ['Existing', 'New', 'ToBeDemolished']:
            self.assertTrue(validator.is_valid({**base, 'status': role}))
        self.assertFalse(validator.is_valid({**base, 'status': 'Demolition Plan'}))
        self.assertFalse(validator.is_valid({**base, 'status': 'New', 'renovationFilter': 'Proposed'}))

    def test_annotation_units_and_field_applicability(self):
        element = {'elementId': {'guid': '11111111-1111-4111-8111-111111111111'}}
        texts = Draft7Validator(schema('ElementCreationCommands.cpp', 'ModifyTexts'))
        labels = Draft7Validator(schema('ElementCreationCommands.cpp', 'ModifyLabels'))
        self.assertTrue(texts.is_valid({'textsWithDetails': [{**element, 'text': 'Chambre\nÉtage'}]}))
        self.assertTrue(texts.is_valid({'textsWithDetails': [{**element, 'coordinate': {'x': 0, 'y': 0, 'z': 0}}]}))
        self.assertFalse(texts.is_valid({'textsWithDetails': [{**element, 'widthMillimetres': 80}]}))
        self.assertFalse(texts.is_valid({'textsWithDetails': [{'text': 'no element id'}]}))
        self.assertTrue(labels.is_valid({'labelsWithDetails': [{**element, 'leaderLine': {'hasLeaderLine': False}}]}))
        self.assertFalse(labels.is_valid({'labelsWithDetails': [{**element, 'widthMillimetres': 80}]}))

    def test_all_new_literal_schemas_are_valid_json_schema(self):
        for file in ['NativeProjectCommands.cpp', 'AnnotationCommands.cpp', 'DeveloperTools.cpp', 'SlabTopologyCommands.cpp', 'SectionCommands.cpp']:
            text = (SOURCES / file).read_text(encoding='utf-8-sig')
            for match in re.finditer(r'R"\((.*?)\)"', text, re.S):
                try: parsed = json.loads(match[1])
                except json.JSONDecodeError: continue
                if isinstance(parsed, dict) and ('type' in parsed or '$ref' in parsed):
                    Draft7Validator.check_schema({**COMMON, **parsed})

    def test_composite_resource_contract(self):
        validator = Draft7Validator(schema('AttributeCommands.cpp', 'CreateComposites'))
        reference = {'attributeId': {'guid': '11111111-1111-4111-8111-111111111111'}}
        skin = {'type': 'Core', 'buildingMaterialId': reference, 'framePen': 1, 'thickness': 0.2}
        separator = {'lineTypeId': reference, 'linePen': 1}
        base = {'name': 'Test composite', 'useWith': ['Wall'], 'skins': [skin], 'separators': [separator, separator]}
        self.assertTrue(validator.is_valid({'compositeDataArray': [{**base, 'index': 1}]}))
        validator.validate({'compositeDataArray':[base],'ifExists':'ReuseIfMatching'})
        self.assertFalse(validator.is_valid({'compositeDataArray':[base],'ifExists':'ReuseIfMatching','overwriteExisting':False}))
        for invalid in [{'index': '1'}, {'useWith': ['Unknown']}, {'useWith': []},
                        {'skins': []}, {'skins': [{**skin, 'type': 'Unknown'}]},
                        {'skins': [{**skin, 'thickness': 0}]}]:
            self.assertFalse(validator.is_valid({'compositeDataArray': [{**base, **invalid}]}))

    def test_query_bounds(self):
        for file, cmd, base in [('DeveloperTools.cpp', 'GetCommandContracts', {}),
                                 ('ElementCommands.cpp', 'GetElementContext', {'elementType': 'Wall'}),
                                 ('LibraryCommands.cpp', 'SearchLibraryParts', {})]:
            validator = Draft7Validator(schema(file, cmd))
            self.assertTrue(validator.is_valid({**base, 'offset': 0, 'limit': 100}))
            for extra in [{'limit': 101}, {'offset': -1}, {'limit': 0}, {'limit': 1.5}, {'unknown': 1}]:
                self.assertFalse(validator.is_valid({**base, **extra}))

    def test_context_native_zone_and_typed_property_queries(self):
        validator = Draft7Validator(schema('ElementCommands.cpp','GetElementContext'))
        guid = {'guid':'11111111-1111-4111-8111-111111111111'}
        bounds = {'xMin':0,'yMin':0,'zMin':0,'xMax':4,'yMax':3,'zMax':2.5}
        predicate = {'propertyId':guid,'operator':'Equals','value':0.2,'tolerance':0.001}
        request = {'elementType':'Wall','zoneId':guid,'includeBounds':True,'bounds3D':bounds,'propertyFilters':[predicate]}
        validator.validate(request)
        for invalid in [{'operator':'Contains'},{'tolerance':-1},{'operator':'GreaterThan'}, {'value':True}]:
            self.assertFalse(validator.is_valid({**request,'propertyFilters':[{**predicate,**invalid}]}))
        self.assertFalse(validator.is_valid({**request,'bounds2D':{'xMin':0,'yMin':0,'xMax':4,'yMax':3}}))
        self.assertFalse(validator.is_valid({**request,'propertyFilters':[predicate]*17}))

    def test_transform_discriminates_operations(self):
        validator = Draft7Validator(schema('ElementCommands.cpp', 'TransformElements'))
        elements = [{'elementId': {'guid': '11111111-1111-4111-8111-111111111111'}}]
        origin = {'x': 10, 'y': 12}
        cases = [{'operation': 'Move', 'vector': {**origin, 'z': 0}},
                 {'operation': 'Mirror', 'axisStart': origin, 'axisEnd': {'x': 13, 'y': 14}},
                 {'operation': 'Rotate', 'origin': origin, 'angle': 1.57},
                 {'operation': 'Resize', 'origin': origin, 'factor': 2}]
        for case in cases:
            self.assertTrue(validator.is_valid({'elements': elements, **case}))
            self.assertFalse(validator.is_valid({'elements': [], **case}))
            self.assertFalse(validator.is_valid({'elements': elements + elements, **case}))
        for bad in [{'operation': 'Mirror'}, {'operation': 'Resize', 'origin': origin, 'factor': -1},
                    {**cases[0], 'angle': 1}, {'operation': 'Stretch'}]:
            self.assertFalse(validator.is_valid({'elements': elements, **bad}))


if __name__ == '__main__': unittest.main()
