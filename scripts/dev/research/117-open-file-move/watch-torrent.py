# Logs a torrent's state, path fields and progress whenever they change.
# Usage: python3 watch-torrent.py http://127.0.0.1:PORT/api/v2 HASH SECONDS  (a throwaway qBittorrent)
import json, sys, time, urllib.request
api, h, secs = sys.argv[1], sys.argv[2], float(sys.argv[3])
last = None; t0 = time.time()
while time.time() - t0 < secs:
    try:
        t = json.load(urllib.request.urlopen(f"{api}/torrents/info?hashes={h}"))
        t = t[0] if t else {}
    except Exception as e:
        t = {"err": str(e)}
    key = (t.get("state"), t.get("save_path"), t.get("download_path"), t.get("content_path"), round(t.get("progress", 0), 2))
    if key != last:
        print(f"{time.strftime('%H:%M:%S')}.{int(time.time()*1000)%1000:03d} {key}", flush=True); last = key
    time.sleep(0.25)
