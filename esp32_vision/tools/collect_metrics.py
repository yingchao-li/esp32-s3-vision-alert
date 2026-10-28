#!/usr/bin/env python3
"""User-operated HTTP metrics recorder. It never starts a stress test or changes settings."""
import argparse
import json
from pathlib import Path
import time
import urllib.request

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--url",default="http://192.168.4.1")
    p.add_argument("--seconds",type=float,default=300)
    p.add_argument("--interval",type=float,default=2)
    p.add_argument("--output",type=Path,required=True)
    args=p.parse_args()
    if args.seconds<=0 or args.interval<1:p.error("Duration must be positive; interval >=1 second")
    end=time.monotonic()+args.seconds
    with args.output.open("a") as stream:
        while time.monotonic()<end:
            record={"host_unix_time":time.time()}
            try:
                with urllib.request.urlopen(args.url.rstrip("/")+"/api/status",timeout=5) as response:
                    record["device"]=json.load(response)
            except Exception as error:
                record["network_error"]=str(error)
            stream.write(json.dumps(record)+"\n");stream.flush()
            time.sleep(min(args.interval,max(0,end-time.monotonic())))

if __name__=="__main__":main()
