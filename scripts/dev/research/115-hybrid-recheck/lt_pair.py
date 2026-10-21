# omaseed #115: a seeder and a downloader session in one process, offline on loopback.
# Usage: python3 lt_pair.py hybrid|v1|v2 random|zero WORKDIR recheck|stopstart|rewant|restart|clearlong|readd
# Env: KEEP (regex of files kept, default /keep/), PARTIAL=SECONDS (want all that long, then skip),
# NOSKIP, NODELETE, DELETEKEPT, DELETEWANTED (variations). WORKDIR is wiped first.
# Make a fixture (random or zero data), download it, skip files, delete the skipped ones,
# then recheck / stop+start / re-want, and report the downloader's errc.
import re, os, sys, time, shutil, random, libtorrent as lt
kind, data, base, mode = sys.argv[1:5]       # kind: hybrid|v1|v2 ; data: random|zero ; mode: recheck|stopstart|rewant|readd
random.seed(115)
src = os.path.join(base, 'src'); dst = os.path.join(base, 'dst')
shutil.rmtree(base, ignore_errors=True); os.makedirs(src); os.makedirs(dst)
name = 'Fixture'
layout = [('small/a.txt', 3000), ('small/b.txt', 70000), ('keep/k1.bin', 300000), ('keep/k2.bin', 5000),
          ('big/x.bin', 3_000_000), ('big/y.bin', 2_500_000), ('z.bin', 1_000_000)]
for rel, size in layout:
    p = os.path.join(src, name, rel); os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p, 'wb') as f: f.write(os.urandom(size) if data == 'random' else bytes(size))
fs = lt.file_storage(); lt.add_files(fs, os.path.join(src, name))
flags = {'hybrid': 0, 'v1': lt.create_torrent.v1_only, 'v2': lt.create_torrent.v2_only}[kind]
ct = lt.create_torrent(fs, 65536, flags=flags); lt.set_piece_hashes(ct, src)
ti = lt.torrent_info(lt.bencode(ct.generate()))
def sess():
    return lt.session({'listen_interfaces': '127.0.0.1:0', 'enable_dht': False, 'enable_lsd': False,
                       'enable_upnp': False, 'enable_natpmp': False, 'enable_outgoing_utp': False,
                       'enable_incoming_utp': False, 'allow_multiple_connections_per_ip': True,
                       'alert_mask': lt.alert_category.error | lt.alert_category.status | lt.alert_category.storage})
A, B = sess(), sess()
def add(s, path):
    p = lt.add_torrent_params(); p.ti = lt.torrent_info(ti); p.save_path = path
    p.flags &= ~lt.torrent_flags.auto_managed & ~lt.torrent_flags.paused
    return s.add_torrent(p)
ha = add(A, src)
for _ in range(40):
    time.sleep(0.25)
    if ha.status().is_seeding: break
if os.environ.get('PARTIAL'):
    ha.set_upload_limit(400_000)
hb = add(B, dst); time.sleep(0.5)
if os.environ.get('PARTIAL'):
    _f = hb.torrent_file().files()
    _keep = [i for i in range(_f.num_files()) if re.search(os.environ.get('KEEP', '/keep/'), _f.file_path(i)) or (_f.file_flags(i) & lt.file_storage.flag_pad_file)]
    hb.prioritize_files([1 if i in _keep else 0 for i in range(_f.num_files())])
hb.connect_peer(('127.0.0.1', A.listen_port()))
if os.environ.get('PARTIAL'):
    for _ in range(80):
        time.sleep(0.25)
        if hb.status().is_finished: break
    hb.prioritize_files([1] * _f.num_files()); hb.connect_peer(('127.0.0.1', A.listen_port())); time.sleep(float(os.environ['PARTIAL']))
    hb.prioritize_files([1 if i in _keep else 0 for i in range(_f.num_files())]); time.sleep(2)
for _ in range(120):
    time.sleep(0.25)
    if hb.status().is_seeding or (os.environ.get('PARTIAL') and hb.status().is_finished): break
f = hb.torrent_file().files()
real = [i for i in range(f.num_files()) if not (f.file_flags(i) & lt.file_storage.flag_pad_file)]
pad = [i for i in range(f.num_files()) if f.file_flags(i) & lt.file_storage.flag_pad_file]
keep = [i for i in real if re.search(os.environ.get('KEEP', '/keep/'), f.file_path(i))]
def st(tag):
    msgs = [f'{a.what()}: {a.message()}' for a in B.pop_alerts() if a.what() in ('torrent_error', 'file_error', 'hash_failed')]
    t = hb.status()
    print(f'{kind:6} {data:6} {mode:9} {tag:14} state={str(t.state):24} errc={t.errc.value()}:{t.errc.message()!r:52} have={t.num_pieces}/{hb.torrent_file().num_pieces()} wanted={t.total_wanted} done={t.total_wanted_done} {msgs[:2]}', flush=True)
st('downloaded')
if not os.environ.get('NOSKIP'): hb.prioritize_files([1 if (i in keep or i in pad) else 0 for i in range(f.num_files())]); time.sleep(1)
for i in real:
    if i not in keep and not os.environ.get('NODELETE') and (not os.environ.get('NOSKIP') or os.environ.get('DELETEWANTED')): os.remove(os.path.join(dst, f.file_path(i)))
if os.environ.get('DELETEKEPT'):
    os.remove(os.path.join(dst, f.file_path(keep[0])))
time.sleep(0.5)
def wait(n=12):
    for _ in range(n): time.sleep(0.25)
if mode == 'recheck':
    hb.force_recheck(); wait(); st('recheck')
    if hb.status().errc.value():
        hb.clear_error(); hb.resume(); wait(); st('clear+resume')
elif mode == 'stopstart':
    hb.pause(); wait(4); hb.resume(); wait(); st('stop+start')
    hb.force_recheck(); wait(); st('later recheck')
elif mode == 'rewant':
    hb.pause(); wait(4); hb.resume(); wait(4)
    hb.prioritize_files([1] * f.num_files()); wait(); st('re-wanted')
    hb.force_recheck(); wait(); st('re-want+check')
elif mode == 'restart':
    hb.save_resume_data(lt.torrent_handle.save_info_dict); rd = None
    for _ in range(40):
        time.sleep(0.25)
        for a in B.pop_alerts():
            if a.what() == 'save_resume_data': rd = lt.write_resume_data_buf(a.params)
        if rd: break
    B.remove_torrent(hb); time.sleep(1)
    p = lt.read_resume_data(rd); p.flags &= ~lt.torrent_flags.auto_managed & ~lt.torrent_flags.paused
    hb = B.add_torrent(p); wait(); st('restarted')
    hb.force_recheck(); wait(); st('restart+check')
elif mode == 'clearlong':
    hb.force_recheck(); wait(); st('recheck')
    hb.clear_error(); hb.resume()
    for _ in range(40): time.sleep(0.25)
    st('clear+resume 10s')
elif mode == 'readd':
    hb.force_recheck(); wait(); st('recheck')
    B.remove_torrent(hb); time.sleep(1)
    p = lt.add_torrent_params(); p.ti = lt.torrent_info(ti); p.save_path = dst
    p.file_priorities = [1 if (i in keep or i in pad) else 0 for i in range(f.num_files())]
    p.flags &= ~lt.torrent_flags.auto_managed & ~lt.torrent_flags.paused
    hb = B.add_torrent(p); wait(); st('re-added')
