"""Combine independently generated release entries into a public Hub catalog."""
import argparse
import json
from pathlib import Path
import sys

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from Launcher.downloads import ValidateEntry


def main():
    parser=argparse.ArgumentParser();parser.add_argument("entries",nargs="+",type=Path);parser.add_argument("--output",type=Path,required=True);parser.add_argument("--base-url");args=parser.parse_args()
    entries=[];ids=set()
    for path in args.entries:
        value=json.loads(path.read_text(encoding="utf-8-sig"))
        if args.base_url:value["url"]=args.base_url.rstrip("/")+"/"+path.name.removesuffix(".json")
        ValidateEntry(value)
        if value["id"] in ids:raise ValueError("Duplicate download id: "+value["id"])
        ids.add(value["id"]);entries.append(value)
    args.output.write_text(json.dumps({"schema_version":1,"downloads":entries},indent=2),encoding="utf-8")


if __name__=="__main__":main()
