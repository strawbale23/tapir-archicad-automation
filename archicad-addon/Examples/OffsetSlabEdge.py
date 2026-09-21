"""Prepare or apply one floor-edge offset. Updated 2026-09-19 14:04 CEST.

Uses the native HTTP interface, not MCP. Default is read-only: prints the request.
Example: python OffsetSlabEdge.py --port 19723 --project C:/Example/Room.pln
  --element-guid GUID --contour 0 --edge 1 --distance .1 --maximum-movement .5
Add --apply to change the selected slab. The script never saves or retries writes.
"""
import argparse
import json
import ntpath
import urllib.request
import uuid


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--port',type=int,required=True)
    p.add_argument('--project',required=True)
    p.add_argument('--element-guid',type=uuid.UUID,required=True)
    p.add_argument('--contour',type=int,default=0)
    p.add_argument('--edge',type=int,required=True)
    p.add_argument('--distance',type=float,required=True)
    p.add_argument('--maximum-movement',type=float,required=True)
    p.add_argument('--apply',action='store_true')
    args=p.parse_args()
    if not 1<=args.port<=65535: p.error('Invalid port')

    def call(name, parameters=None):
        data={'command':'API.ExecuteAddOnCommand','parameters':{
            'addOnCommandId':{'commandNamespace':'TapirCommand','commandName':name},
            'addOnCommandParameters':parameters or {}}}
        request=urllib.request.Request('http://127.0.0.1:'+str(args.port),
            data=json.dumps(data,allow_nan=False).encode(),headers={'Content-Type':'application/json'})
        with urllib.request.urlopen(request,timeout=60) as reply: envelope=json.load(reply)
        if envelope.get('succeeded') is not True: raise RuntimeError(envelope)
        result=envelope['result']['addOnCommandResponse']
        if 'error' in result or result.get('success') is False: raise RuntimeError(result)
        return result

    def check_project():
        actual=call('GetProjectInfo').get('projectPath','')
        if not actual or ntpath.normcase(ntpath.normpath(actual))!=ntpath.normcase(ntpath.normpath(args.project)):
            raise RuntimeError('The open project does not match --project')

    check_project()
    element={'guid':str(args.element_guid)}
    topology=call('GetSlabTopology',{'elementId':element})
    request={'elementId':element,'expectedModificationStamp':topology['modificationStamp'],
             'contourIndex':args.contour,'edgeIndex':args.edge,'distance':args.distance,
             'maximumEndpointMovement':args.maximum_movement}
    print(json.dumps(request,indent=2,allow_nan=False))
    if args.apply:
        check_project()
        print(json.dumps(call('OffsetSlabEdge',request),indent=2))


if __name__=='__main__': main()
