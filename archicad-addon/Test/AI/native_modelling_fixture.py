"""Native wall, polygon, copy and annotation exercise; no MCP or favourites.

Updated: 11 September 2026, 15:58 CEST (Europe/Berlin).
Requires an authorized, separately saved disposable PLN. Never saves the project.
Default invocation only checks project/version/contracts. --run enables writes.
"""
import argparse
import datetime
import json
import math
import pathlib
import uuid
from native_annotation_fixture import Fixture


class ModellingFixture(Fixture):
    def preflight(self):
        self.guard()
        for command in ['CreateWalls', 'ModifyWalls', 'GetNativeQuantities',
                        'CreatePolygonalWalls', 'GetPolygonalWallGeometry',
                        'ModifyPolygonalWallGeometry', 'SetAnnotationTextStyle',
                        'TransformElements', 'CreateSlabs', 'GetSlabTopology', 'EditSlabTopology',
                        'GetElementHotspots', 'StretchElementAtHotspot', 'GetElementContext']:
            rows = self.call('GetCommandContracts', {
                'commandName': command, 'includeSchemas': True})['commands']
            if len(rows) != 1 or rows[0]['name'] != command:
                raise RuntimeError('Required runtime command unavailable: ' + command)
            contract = json.loads(rows[0]['inputSchemaJson'])
            if command == 'TransformElements' and 'repeatCount' not in contract['properties']:
                raise RuntimeError('Installed add-on predates repeat-copy support.')

    def details(self, element):
        rows = self.call('GetDetailsOfElements', {'elements': [{'elementId': element}]})['detailsOfElements']
        if len(rows) != 1 or 'error' in rows[0]:
            raise RuntimeError('Element details unavailable: ' + repr(rows))
        return rows[0]['details']

    def equal(self, actual, expected, label):
        if not math.isclose(actual, expected, rel_tol=1e-7, abs_tol=1e-6):
            raise RuntimeError(f'{label}: expected {expected}, native readback {actual}')

    def volume(self, element, expected, family='wall'):
        rows = self.call('GetNativeQuantities', {'elements': [{'elementId': element}]})['quantities']
        if len(rows) != 1 or 'error' in rows[0]:
            raise RuntimeError('Native quantity unavailable: ' + repr(rows))
        values = [value for value in rows[0]['values'] if value['name'] == family + '.volume']
        if len(values) != 1 or values[0]['unit'] != 'm3':
            raise RuntimeError('Expected native wall volume in cubic metres.')
        self.equal(values[0]['value'], expected, 'wall volume')

    def run(self):
        self.preflight()
        materials = self.call('GetAttributesByType', {'attributeType': 'BuildingMaterial'})['attributes']
        if not materials:
            raise RuntimeError('Target project has no building material.')
        material = materials[0]['attributeId']
        try:
            wall = self.remember_created(self.change('CreateWalls', {'wallsData': [{
                'begCoordinate': {'x': 10000, 'y': 10000},
                'endCoordinate': {'x': 10004, 'y': 10000}, 'height': 3, 'thickness': 0.2,
                'structureType': 'Basic', 'buildingMaterialId': material, 'arcAngle': 0,
                'offset': 0, 'flipped': False, 'referenceLineLocation': 'Center'}]}))
            self.change('ModifyWalls', {'wallsWithDetails': [{'elementId': wall, 'profileType': 'Normal'}]})
            self.volume(wall, 2.4)
            self.change('ModifyWalls', {'wallsWithDetails': [{'elementId': wall, 'height': 2.5}]})
            self.equal(self.details(wall)['height'], 2.5, 'wall height')
            self.volume(wall, 2)
            query = {'elementType':'Wall','includeBounds':True,'limit':100,
                     'bounds3D':{'xMin':9999,'yMin':9999,'zMin':-10000,'xMax':10006,'yMax':10001,'zMax':10000}}
            matches, offset = [], 0
            while True:
                page = self.call('GetElementContext',{**query,'offset':offset})
                if page['errors']: raise RuntimeError('Context query errors: ' + repr(page['errors']))
                matches.extend(row for row in page['elements'] if row['elementId']==wall)
                if not page['hasMore']: break
                if page['nextOffset'] <= offset: raise RuntimeError('Context cursor did not advance.')
                offset = page['nextOffset']
            if len(matches)!=1 or not matches[0]['modificationStamp'].isdigit():
                raise RuntimeError('Created wall context/revision was not returned exactly once.')
            self.equal(matches[0]['bounds3D']['xMax']-matches[0]['bounds3D']['xMin'],4,'native wall bounds length')
            result = self.change('TransformElements', {'elements': [{'elementId': wall}],
                'operation': 'Move', 'vector': {'x': 0, 'y': 10, 'z': 0},
                'copy': True, 'repeatCount': 3})
            if not result.get('resultMappingAvailable') or len(result['results']) != 3:
                raise RuntimeError('Three copy mappings were not returned.')
            copies = set()
            for row in result['results']:
                copied = row['resultElementId']
                copies.add(copied['guid'])
                self.equal(self.details(copied)['begCoordinate']['y'],
                           10000 + row['repetition'] * 10, 'repeated copy position')
                self.volume(copied, 2)
            if len(copies) != 3 or wall['guid'] in copies:
                raise RuntimeError('Copied walls do not have three distinct new identities.')
            points = self.call('GetElementHotspots', {'elementId': wall, 'limit': 100})
            end_points = [point for point in points['hotspots']
                          if point['nativeElementId'] == wall
                          and abs(point['coordinate']['x']-10004) < 1e-6
                          and abs(point['coordinate']['y']-10000) < 1e-6
                          and abs(point['coordinate']['z']) < 1e-6]
            if not end_points:
                raise RuntimeError('Native wall reference endpoint hotspot unavailable; stretch is NOT RUN.')
            point = end_points[0]
            self.change('StretchElementAtHotspot', {'elementId': wall,
                'expectedDatabaseId': points['databaseId'],
                'expectedModificationStamp': points['modificationStamp'],
                'hotspotIndex': point['hotspotIndex'], 'expectedCoordinate': point['coordinate'],
                'vector': {'x': 1, 'y': 0, 'z': 0}})
            self.equal(self.details(wall)['endCoordinate']['x'], 10005, 'stretched wall endpoint')
            self.volume(wall, 2.5)
            slab = self.remember_created(self.change('CreateSlabs', {'slabsData': [{
                'level': 0, 'thickness': 0.2, 'structureType': 'Basic',
                'buildingMaterialId': material, 'referencePlaneLocation': 'Top',
                'polygonCoordinates': [{'x': 10040, 'y': 10000}, {'x': 10044, 'y': 10000},
                                       {'x': 10044, 'y': 10003}, {'x': 10040, 'y': 10003}]}]}))
            self.volume(slab, 2.4, 'slab')
            topology = self.call('GetSlabTopology', {'elementId': slab})
            contour = topology['contours'][0]
            self.change('EditSlabTopology', {'elementId': slab, 'operation': 'AddHole',
                'contourIndex': 0, 'expectedContourCount': len(topology['contours']),
                'expectedContour': [v['coordinate'] for v in contour['vertices']],
                'hole': [{'x': 10041, 'y': 10001}, {'x': 10042, 'y': 10001},
                         {'x': 10042, 'y': 10002}, {'x': 10041, 'y': 10002}]})
            self.volume(slab, 2.2, 'slab')
            topology = self.call('GetSlabTopology', {'elementId': slab})
            holes = [contour for contour in topology['contours'] if contour['hole']]
            if len(holes) != 1:
                raise RuntimeError('Slab hole topology was not retained.')
            self.change('EditSlabTopology', {'elementId': slab, 'operation': 'DeleteHole',
                'contourIndex': holes[0]['contourIndex'],
                'expectedContourCount': len(topology['contours']),
                'expectedContour': [v['coordinate'] for v in holes[0]['vertices']]})
            self.volume(slab, 2.4, 'slab')
            outline = [{'x': 10020, 'y': 10000}, {'x': 10024, 'y': 10000},
                       {'x': 10024, 'y': 10000.3}, {'x': 10020, 'y': 10000.2}]
            polygon = self.remember_created(self.change('CreatePolygonalWalls', {'wallsData': [{
                'polygonOutline': outline, 'height': 2.7, 'buildingMaterialId': material}]}))
            before = self.call('GetPolygonalWallGeometry', {'elementId': polygon})
            revised = [dict(point, x=point['x'] + 1) for point in outline]
            self.change('ModifyPolygonalWallGeometry', {'elementId': polygon,
                'expectedModificationStamp': before['modificationStamp'],
                'polygonOutline': revised, 'referenceEdgeIndex': 0})
            after = self.call('GetPolygonalWallGeometry', {'elementId': polygon})
            actual = sorted((round(p['x'], 6), round(p['y'], 6)) for p in after['polygonOutline'])
            expected = sorted((round(p['x'], 6), round(p['y'], 6)) for p in revised)
            if actual != expected:
                raise RuntimeError('Polygon footprint did not match requested revision.')
            self.equal(self.details(polygon)['height'], 2.7, 'polygon wall retained height')
            if self.details(polygon)['buildingMaterialId'] != material:
                raise RuntimeError('Polygon edit changed building material.')
            text = self.remember_created(self.change('CreateTexts', {'textsData': [{
                'coordinate': {'x': 10000, 'y': 10050, 'z': 0},
                'text': 'Native modelling fixture – Étage', 'height': 2.5}]}))
            self.change('SetAnnotationTextStyle', {'elements': [{'elementId': text}],
                'style': {'sizeMillimetres': 3.5, 'bold': True, 'alignment': 'Center'}})
            annotation = self.read_text(text)
            style = annotation['baseTextStyle']
            self.equal(style['sizeMillimetres'], 3.5, 'text paper size')
            if not style['bold'] or style['alignment'] != 'Center' or annotation['text'] != 'Native modelling fixture – Étage':
                raise RuntimeError('Text style/content readback mismatch.')
            self.record(fixture='passed', visualAcceptance='not performed',
                        undoAcceptance='not performed', projectSaved=False)
        except Exception as error:
            self.record(fixture='failed', reason=str(error), remainingKnownIds=self.owned)
            raise
        finally:
            self.cleanup()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, required=True)
    parser.add_argument('--project', required=True)
    parser.add_argument('--journal', type=pathlib.Path,
                        default=pathlib.Path('native-model-' + uuid.uuid4().hex + '.jsonl'))
    parser.add_argument('--run', action='store_true')
    args = parser.parse_args()
    fixture = ModellingFixture(args.port, args.project, args.journal)
    if args.run:
        fixture.run()
        message = 'Native model/readback/cleanup passed; visual and Undo acceptance remain outstanding.'
    else:
        fixture.preflight()
        message = 'Read-only project/version/contracts preflight passed. No model changes.'
    print(datetime.datetime.now().astimezone().isoformat(), message)
    print('Journal:', args.journal.resolve())


if __name__ == '__main__':
    main()
