"""Opt-in integration test. No save, close, reference-project edits or automatic retries.
Run only with development add-on loaded in the separately saved Chambre 1 TEST file.
--run is required for mutation. Door/window tests additionally require target favourites.
"""
import argparse, json, math, ntpath, pathlib, urllib.request, uuid

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--project", required=True)
    parser.add_argument("--run", action="store_true")
    parser.add_argument("--door-favorite")
    parser.add_argument("--window-favorite")
    args = parser.parse_args()
    if ntpath.basename(args.project).lower() != "hewson - chambre 1 test.pln":
        raise RuntimeError("This fixture is restricted to the separately saved Chambre 1 TEST project.")
    journal = pathlib.Path(__file__).with_name("live-" + uuid.uuid4().hex + ".jsonl")
    owned = []
    def call(name, parameters=None):
        request = {"command":"API.ExecuteAddOnCommand","parameters":{
            "addOnCommandId":{"commandNamespace":"TapirCommand","commandName":name},
            "addOnCommandParameters":parameters or {}}}
        with journal.open("a",encoding="utf-8") as log:
            log.write(json.dumps({"request":request})+"\n")
        req=urllib.request.Request(f"http://127.0.0.1:{args.port}",data=json.dumps(request).encode(),
                                   headers={"Content-Type":"application/json"})
        with urllib.request.urlopen(req,timeout=30) as response:
            raw=json.load(response)
        with journal.open("a",encoding="utf-8") as log:
            log.write(json.dumps({"response":raw})+"\n")
        if not raw.get("succeeded"): raise RuntimeError(raw)
        result=raw["result"]["addOnCommandResponse"]
        if "error" in result: raise RuntimeError(result)
        for item in result.get("executionResults",[]):
            if item.get("success") is not True: raise RuntimeError(item)
        return result
    def project_guard():
        info=call("GetProjectInfo")
        if ntpath.normcase(ntpath.normpath(info.get("projectPath",""))) != ntpath.normcase(ntpath.normpath(args.project)):
            raise RuntimeError("Active project does not match the authorized test path.")
    def change(name, params):
        project_guard()
        return call(name,params)
    def created(result):
        items=result["elements"]
        # Record all returned IDs before raising an item failure.
        for item in items:
            if "elementId" in item: owned.append(item["elementId"])
        if len(items)!=1 or "elementId" not in items[0]: raise RuntimeError(result)
        return items[0]["elementId"]
    project_guard()
    version=call("GetAddOnVersion")
    if "1.5.8-ai.1" not in json.dumps(version):
        raise RuntimeError("Load the development build first. Installed response: "+json.dumps(version))
    if not args.run:
        print("Project and development version confirmed. No mutations; add --run to execute fixture.")
        return
    try:
        wall=created(change("CreateWalls",{"wallsData":[{
            "begCoordinate":{"x":10000,"y":10000},"endCoordinate":{"x":10003,"y":10004},
            "height":2.9,"thickness":.2,"structureType":"Basic","flipped":False,"junctionOrder":200}]}))
        result=call("GetWallReferenceGeometry",{"elements":[{"elementId":wall}]})["walls"][0]
        frame=result["geometry"]
        assert math.isclose(frame["length"],5,abs_tol=1e-6), result
        assert math.isclose(frame["tangent"]["x"],.6,abs_tol=1e-6), result
        change("ModifyWalls",{"wallsWithDetails":[{"elementId":wall,"flipped":True,"junctionOrder":300}]})
        details=call("GetDetailsOfElements",{"elements":[{"elementId":wall}]})["detailsOfElements"][0]
        # Tapir details are nested by type.
        data=details.get("details",details)
        wall_data=data.get("typeSpecificDetails",data)
        assert wall_data["flipped"] is True and wall_data["junctionOrder"]==300, details
        for kind,favorite in [("Doors",args.door_favorite),("Windows",args.window_favorite)]:
            if not favorite:
                print(kind+": NOT RUN (target-project favourite not provided)")
                continue
            lower=kind.lower()
            item=created(change("Create"+kind,{lower+"Data":[{
                "ownerWallId":wall,"width":.9,"height":2.1,"favoriteName":favorite,
                "placement":{"from":"Start","anchor":"NearestJamb","distance":.15}}]}))
            def opening_details():
                result=call("GetDetailsOfElements",{"elements":[{"elementId":item}]})["detailsOfElements"][0]
                data=result.get("details",result)
                return data.get("typeSpecificDetails",data)
            assert math.isclose(opening_details()["centerOffset"],.6,abs_tol=1e-6)
            change("Modify"+kind,{lower+"WithDetails":[{"elementId":item,"width":1.2,
                "placement":{"from":"End","anchor":"NearestJamb","distance":.2}}]})
            data=opening_details()
            assert math.isclose(data["centerOffset"],4.2,abs_tol=1e-6) and math.isclose(data["width"],1.2,abs_tol=1e-6)
        print("Requested live fixtures passed; native visual review still required.")
    finally:
        # Delete only IDs returned to this test; openings before the host.
        for item in reversed(owned):
            change("DeleteElements",{"elements":[{"elementId":item}]})
        print("Known fixture elements removed; project was not saved. Journal:",journal)

if __name__=="__main__":
    main()

