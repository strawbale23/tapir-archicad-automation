"""Extended native editing/recovery acceptance. Updated 19 September 2026, 13:31 CEST.

Read-only unless --run. Uses only its own fixtures, an exact saved test PLN,
loaded resources and native HTTP. No saves, retries, favourites or MCP changes.
"""
import argparse
import datetime
import pathlib
import uuid
from native_modelling_fixture import ModellingFixture


class ExtendedFixture(ModellingFixture):
    def preflight(self):
        self.guard()
        for name in ['GetAssemblySegments','SetAssemblySegments','GetDimensionAnchors',
                     'GetAnnotationFormatting','SetAnnotationRunStyles','GetAutomationSession',
                     'ExecuteGuardedCommand','GetOperationReceipt','ModifyRoofs','GetRoofGeometry','GetPlanPrimitives',
                     'CreateSections','ModifySectionSettings','GetSectionSettings']:
            rows=self.call('GetCommandContracts',{'commandName':name})['commands']
            if len(rows)!=1 or rows[0]['name']!=name:
                raise RuntimeError('Missing native capability: '+name)

    def assembly(self, element, material):
        state=self.call('GetAssemblySegments',{'elementId':element})
        segment={'sourceIndex':0,'buildingMaterialId':material,'homogeneous':True,
                 'width':.3,'height':.3,'linkedDimensions':False}
        request={'elementId':element,'expectedModificationStamp':state['modificationStamp'],
                 'segments':[segment,{**segment,'homogeneous':False,'endWidth':.2,'endHeight':.2,'linkedEndDimensions':False}],
                 'schemes':[{'lengthType':'Proportional','value':.5}]*2,
                 'cuts':[{'type':'Horizontal'}]*3}
        self.change('SetAssemblySegments',request)
        after=self.call('GetAssemblySegments',{'elementId':element})
        if len(after['segments'])!=2 or after['segments'][1]['homogeneous']:
            raise RuntimeError('Segment topology/taper was not retained.')
        self.equal(after['segments'][1]['endWidth'],.2,'segment taper end width')
        # Same parent GUID remains queryable. A stale request must not execute again.
        try:
            self.change('SetAssemblySegments',request)
        except RuntimeError:
            pass
        else:
            raise RuntimeError('Stale assembly revision was accepted.')

    def dimension(self, wall):
        anchors=[]; offset=0
        while True:
            page=self.call('GetDimensionAnchors',{'elementId':wall,'offset':offset,'limit':100})
            anchors.extend(a for a in page['anchors'] if a['kind']=='WallPoint')
            if not page['hasMore']: break
            if page['nextOffset']<=offset: raise RuntimeError('Anchor cursor did not advance.')
            offset=page['nextOffset']
        if not anchors: raise RuntimeError('No supported wall anchors; associative test NOT RUN.')
        first=min(anchors,key=lambda a:a['coordinate']['x'])
        last=max(anchors,key=lambda a:a['coordinate']['x'])
        self.equal(last['coordinate']['x']-first['coordinate']['x'],4,'initial wall anchor span')
        dimension=self.remember_created(self.change('CreateAssociativeDimensions',{'dimensionsData':[{
            'referencePoint':{'x':10100,'y':10099},'direction':{'x':1,'y':0},
            'witnessPoints':[first['witnessPoint'],last['witnessPoint']]}]}))
        def span():
            row=self.call('GetDimensionData',{'elements':[{'elementId':dimension}]})['dimensionsData'][0]
            points=row['witnessPoints']
            if len(points)!=2 or any(p.get('baseElementId')!=wall for p in points):
                raise RuntimeError('Native dimension associations were lost.')
            return abs(points[1]['coordinate']['x']-points[0]['coordinate']['x'])
        self.equal(span(),4,'initial associated dimension')
        self.change('ModifyWalls',{'wallsWithDetails':[{'elementId':wall,'endCoordinate':{'x':10105,'y':10100}}]})
        self.equal(span(),5,'dimension following revised wall')

    def recovery(self):
        context=self.call('GetAutomationSession')
        if not context['guardedExecutionAvailable']: raise RuntimeError('Project notifications unavailable; recovery NOT RUN.')
        operation=uuid.uuid4().hex
        request={'expectedSessionId':context['sessionId'],
                 'expectedProjectPath':context['project']['projectPath'],'expectedDatabaseId':context['databaseId'],
                 'operationId':operation,'commandName':'CreateTexts',
                 'parameters':{'textsData':[{'coordinate':{'x':10100,'y':10130,'z':0},'text':'Guarded '+operation,'height':2.5}]}}
        first=self.change('ExecuteGuardedCommand',request)
        # Record returned creation IDs before testing receipt properties.
        element=self.remember_created(first.get('result',{}))
        if first['status']!='returned' or first['replayed']: raise RuntimeError('Incorrect first execution receipt.')
        second=self.change('ExecuteGuardedCommand',request)
        self.remember_created(second.get('result',{}))
        if not second['replayed'] or second['result']!=first['result']:
            raise RuntimeError('Repeated operation did not return exactly the original result.')
        receipt=self.call('GetOperationReceipt',{'expectedSessionId':context['sessionId'],'operationId':operation})
        if receipt['result']!=first['result']: raise RuntimeError('Stored receipt mismatch.')
        try:
            self.change('ExecuteGuardedCommand',{**request,'commandName':'CreateWalls'})
        except RuntimeError:
            pass
        else:
            raise RuntimeError('An operation ID was reused for a different request.')
        self.read_text(element)

    def roof(self, material):
        roof=self.remember_created(self.change('CreateRoofs',{'roofsData':[{
            'level':3,'thickness':.2,'buildingMaterialId':material,'structureType':'Basic',
            'polygonCoordinates':[{'x':10120,'y':10100},{'x':10124,'y':10100},
                                  {'x':10124,'y':10103},{'x':10120,'y':10103}],
            'pivotLine':{'begCoordinate':{'x':10120,'y':10100},'endCoordinate':{'x':10124,'y':10100}},
            'angle':.3,'positiveSide':True}]}))
        self.change('ModifyRoofs',{'roofsWithDetails':[{'elementId':roof,'angle':.4,'positiveSide':False,'thickness':.25}]})
        after=self.call('GetRoofGeometry',{'elementId':roof})
        self.equal(after['angle'],.4,'single plane slope')
        self.equal(after['thickness'],.25,'single plane thickness')
        if after['positiveSide'] or after['roofClass']!='SinglePlane':
            raise RuntimeError('Native roof class/side was not retained.')

    def section(self):
        section=self.remember_created(self.change('CreateSections',{'sectionsData':[{
            'startCoordinate':{'x':10099,'y':10099},'endCoordinate':{'x':10106,'y':10099},
            'depth':4,'name':'Synthetic section '+uuid.uuid4().hex}]}))
        before=self.call('GetSectionSettings',{'elementId':section})
        presentation={'referenceId':'S-AI','vectorHatching':True,'vectorShadows':False,
                      'storyLineAppearance':'ScreenAndPrint','storyLine':True,
                      'cutLinePen':1,'cutFillPen':1,'cutFillBackgroundPen':0,'transparency':True}
        self.change('ModifySectionSettings',{'sectionsWithSettings':[{
            'elementId':section,'expectedModificationStamp':before['modificationStamp'],
            'horizontalRange':'Infinite','markerPosition':'Ends','presentation':presentation,
            'verticalRange':{'mode':'Limited','minimum':-.2,'maximum':3.5}}]})
        after=self.call('GetSectionSettings',{'elementId':section})
        if after['horizontalRange']!='Infinite' or after['markerPosition']!='Ends':
            raise RuntimeError('Section range/marker position was not retained.')
        self.equal(after['verticalMinimum'],-.2,'section lower limit')
        self.equal(after['verticalMaximum'],3.5,'section upper limit')
        for key,value in presentation.items():
            if after['presentation'].get(key)!=value:
                raise RuntimeError('Section presentation not retained: '+key)

    def run(self):
        self.preflight()
        material=self.call('GetAttributesByType',{'attributeType':'BuildingMaterial'})['attributes'][0]['attributeId']
        try:
            beam=self.remember_created(self.change('CreateBeams',{'beamsData':[{
                'begCoordinate':{'x':10100,'y':10110},'endCoordinate':{'x':10104,'y':10110},
                'zCoordinate':0,'width':.3,'height':.3,'isWidthAndHeightLinked':False,
                'buildingMaterialId':material,'arcAngle':0,'verticalCurveHeight':0,'isSlanted':False}]}))
            self.assembly(beam,material)
            column=self.remember_created(self.change('CreateColumns',{'columnsData':[{
                'coordinates':{'x':10110,'y':10110,'z':0},'height':3,'width':.3,'depth':.3,
                'buildingMaterialId':material,'circleBased':False,'isSlanted':False}]}))
            self.assembly(column,material)
            wall=self.remember_created(self.change('CreateWalls',{'wallsData':[{
                'begCoordinate':{'x':10100,'y':10100},'endCoordinate':{'x':10104,'y':10100},
                'height':3,'thickness':.2,'buildingMaterialId':material,'arcAngle':0}]}))
            self.dimension(wall)
            self.roof(material)
            self.section()
            text=self.remember_created(self.change('CreateTexts',{'textsData':[{
                'coordinate':{'x':10100,'y':10120,'z':0},'text':'Étage – formatted text','height':2.5}]}))
            state=self.call('GetAnnotationFormatting',{'elementId':text})
            if not state['runs']: raise RuntimeError('No native text runs; formatting NOT RUN.')
            run=state['runs'][0]
            self.change('SetAnnotationRunStyles',{'elementId':text,'expectedModificationStamp':state['modificationStamp'],
                'runs':[{'paragraphIndex':run['paragraphIndex'],'runIndex':run['runIndex'],'style':{'italic':True,'sizeMillimetres':4}}]})
            if self.read_text(text)['text']!='Étage – formatted text': raise RuntimeError('Run styling altered text content.')
            self.recovery()
            self.record(extendedFixture='passed',visualAcceptance='NOT RUN',undoAcceptance='NOT RUN',saveReopenAcceptance='NOT RUN')
        except Exception as error:
            self.record(extendedFixture='failed',reason=str(error))
            raise
        finally:
            self.cleanup()


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--port',type=int,required=True);p.add_argument('--project',required=True)
    p.add_argument('--journal',type=pathlib.Path,default=pathlib.Path('native-extended-'+uuid.uuid4().hex+'.jsonl'))
    p.add_argument('--run',action='store_true');a=p.parse_args()
    fixture=ExtendedFixture(a.port,a.project,a.journal)
    fixture.run() if a.run else fixture.preflight()
    print(datetime.datetime.now().astimezone().isoformat(),'Extended native fixture passed.' if a.run else 'Read-only preflight passed; no writes.')

if __name__=='__main__':main()
