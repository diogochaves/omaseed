# Recheck after deleting a file of a downloaded hybrid torrent -> torrent_inconsistent_hashes (217).
# Two sessions on loopback, nothing leaves the machine. Usage: python3 repro_217.py [hybrid|v1|v2]
import os, sys, time, shutil, tempfile, libtorrent as lt
kind = sys.argv[1] if len(sys.argv) > 1 else 'hybrid'
victim = sys.argv[2] if len(sys.argv) > 2 else 's.txt'   # the file to delete
base = tempfile.mkdtemp(); src, dst = os.path.join(base, 'src'), os.path.join(base, 'dst')
for rel, size in [('a.bin', 3_000_000), ('b.bin', 300_000), ('s.txt', 3_000)]:
    os.makedirs(os.path.join(src, 'T'), exist_ok=True)
    with open(os.path.join(src, 'T', rel), 'wb') as f: f.write(os.urandom(size))
fs = lt.file_storage(); lt.add_files(fs, os.path.join(src, 'T'))
flags = {'hybrid': 0, 'v1': lt.create_torrent.v1_only, 'v2': lt.create_torrent.v2_only}[kind]
ct = lt.create_torrent(fs, 65536, flags=flags); lt.set_piece_hashes(ct, src)
ti = lt.torrent_info(lt.bencode(ct.generate()))
def session():
    return lt.session({'listen_interfaces': '127.0.0.1:0', 'enable_dht': False, 'enable_lsd': False,
                       'enable_upnp': False, 'enable_natpmp': False, 'enable_outgoing_utp': False,
                       'enable_incoming_utp': False, 'alert_mask': lt.alert_category.error})
def add(s, path):
    p = lt.add_torrent_params(); p.ti = lt.torrent_info(ti); p.save_path = path
    p.flags &= ~lt.torrent_flags.auto_managed & ~lt.torrent_flags.paused
    return s.add_torrent(p)
seeder, leecher = session(), session()
hs = add(seeder, src)
while not hs.status().is_seeding: time.sleep(0.1)
hl = add(leecher, dst); time.sleep(0.5); hl.connect_peer(('127.0.0.1', seeder.listen_port()))
while not hl.status().is_seeding: time.sleep(0.1)
hl.pause(); time.sleep(1)                           # release the files (as a stop in a client does)
os.remove(os.path.join(dst, 'T', victim))           # delete one downloaded file, nothing else
hl.resume()
hl.force_recheck(); time.sleep(3)
st = hl.status()
print(f'{lt.__version__} {kind} deleted {victim}: state={st.state} errc={st.errc.value()} {st.errc.message()!r} have={st.num_pieces}/{ti.num_pieces()}')
shutil.rmtree(base)
