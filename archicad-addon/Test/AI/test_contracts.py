"""Schema contracts for the AI wall/opening extension. Does not contact Archicad."""
import json, pathlib, re, sys, unittest
root = pathlib.Path(__file__).resolve().parents[3]
from jsonschema import Draft7Validator

sources = root / "archicad-addon/Sources"
common = json.loads((sources / "RFIX/Images/CommonSchemaDefinitions.json").read_text(encoding="utf-8-sig"))
def schema(command):
    for name in ["ExtendedElementCommands.cpp", "WallReferenceCommands.cpp", "DeveloperTools.cpp"]:
        text = (sources / name).read_text(encoding="utf-8-sig")
        marker = command + "Command::GetInputParametersSchema"
        if marker in text:
            start = text.index(marker)
            block = re.search(r'R"\((.*?)\)"', text[start:], re.S).group(1)
            return {**common, **json.loads(block)}
    raise KeyError(command)

ID = {"guid": "11111111-1111-4111-8111-111111111111"}
PLACE = {"from": "Start", "anchor": "NearestJamb", "distance": .15}
class Contracts(unittest.TestCase):
    def test_explicit_library_selection(self):
        for cmd, array in [('CreateDoors','doorsData'),('CreateWindows','windowsData')]:
            v = Draft7Validator(schema(cmd))
            base = {'ownerWallId':ID, 'placement':PLACE}
            self.assertTrue(v.is_valid({array:[{**base,'libraryPart':{'index':1363,**ID}}]}))
            for selection in [{'index':1363},ID,{'index':0,**ID},{'index':1,**ID,'name':'unverified'}]:
                self.assertFalse(v.is_valid({array:[{**base,'libraryPart':selection}]}))
    def test_create_openings(self):
        for cmd, array in [("CreateWindows", "windowsData"), ("CreateDoors", "doorsData")]:
            validator = Draft7Validator(schema(cmd))
            base = {"ownerWallId": ID, "width": .9}
            for extra in [{"centerOffset": 1}, {"placement": PLACE}]:
                with self.subTest(cmd=cmd,extra=extra):
                    self.assertTrue(validator.is_valid({array:[{**base, **extra}]}))
            for extra in [{}, {"centerOffset": 1, "placement": PLACE},
                          {"placement":{**PLACE,"from":"Left"}},
                          {"placement":{**PLACE,"distance":-1}},
                          {"placement":{**PLACE,"anchor":"Trim"}},
                          {"placement":{**PLACE,"unknown":1}}]:
                with self.subTest(cmd=cmd,extra=extra):
                    self.assertFalse(validator.is_valid({array:[{**base,**extra}]}))
    def test_modify_openings(self):
        for cmd,array in [("ModifyWindows","windowsWithDetails"),("ModifyDoors","doorsWithDetails")]:
            v = Draft7Validator(schema(cmd))
            for changes in [{"width":1.2},{"centerOffset":2},{"placement":PLACE},{"placement":PLACE,"width":1.2}]:
                self.assertTrue(v.is_valid({array:[{"elementId":ID,**changes}]}))
            self.assertFalse(v.is_valid({array:[{"elementId":ID,"centerOffset":1,"placement":PLACE}]}))
    def test_wall_controls(self):
        v = Draft7Validator(schema("ModifyWalls"))
        self.assertTrue(v.is_valid({"wallsWithDetails":[{"elementId":ID,"flipped":True,"junctionOrder":250}]}))
        for val in [-1,1000,2.5]:
            self.assertFalse(v.is_valid({"wallsWithDetails":[{"elementId":ID,"junctionOrder":val}]}))
        v = Draft7Validator(schema("CreateWalls"))
        base={"begCoordinate":{"x":0,"y":0},"endCoordinate":{"x":5,"y":0},"height":2.9,"thickness":.2}
        self.assertTrue(v.is_valid({"wallsData":[{**base,"flipped":True,"junctionOrder":250}]}))
    def test_query(self):
        v=Draft7Validator(schema("GetWallReferenceGeometry"))
        self.assertTrue(v.is_valid({"elements":[{"elementId":ID}]}))
        self.assertFalse(v.is_valid({"elements":[ID]}))
    def test_refs_resolve(self):
        # Include all input/response literals in touched command files.
        for name in ["ExtendedElementCommands.cpp","WallReferenceCommands.cpp"]:
            text=(sources/name).read_text(encoding="utf-8-sig")
            for match in re.finditer(r'R"\((.*?)\)"',text,re.S):
                if not match[1].lstrip().startswith("{"): continue
                doc=json.loads(match[1])
                for ref in re.findall(r'"\$ref"\s*:\s*"#/([^"]+)"',match[1]):
                    self.assertIn(ref,common)
                Draft7Validator.check_schema(doc)

if __name__ == "__main__":
    unittest.main()
