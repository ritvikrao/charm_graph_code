# Independent reconstruction of terrain_graph's output for a small region
# (numpy/scipy), compared with the wide .wsg it wrote.
#   check_terrain.py TILEDIR LAT0 LAT1 LON0 LON1 SEED_LAT SEED_LON OUT.wsg [ARCSEC 3|1]
import sys, struct, numpy as np
from scipy import ndimage
tiles, lat0, lat1, lon0, lon1, slat, slon, wsg = sys.argv[1], *map(int, sys.argv[2:6]), float(sys.argv[6]), float(sys.argv[7]), sys.argv[8]
arcsec = int(sys.argv[9]) if len(sys.argv) > 9 else 3
T, PREFIX = {3: (1200, "30"), 1: (3600, "10")}[arcsec]
R, C = (lat1-lat0)*T, (lon1-lon0)*T
H = np.zeros((R, C), np.float32); have = np.zeros((R, C), bool)
for tr in range(lat1-lat0):
    for tc in range(lon1-lon0):
        lat, lon = lat1-1-tr, lon0+tc
        n = f"{tiles}/Copernicus_DSM_COG_{PREFIX}_{'S' if lat<0 else 'N'}{abs(lat):02d}_00_{'W' if lon<0 else 'E'}{abs(lon):03d}_00_DEM.f32"
        try: a = np.fromfile(n, '<f4').reshape(T, T)
        except FileNotFoundError: continue
        H[tr*T:(tr+1)*T, tc*T:(tc+1)*T] = a; have[tr*T:(tr+1)*T, tc*T:(tc+1)*T] = True
land = have & (H != 0)
lab, _ = ndimage.label(land, structure=np.ones((3, 3)))
sr, sc = round((lat1-slat)*T), round((slon-lon0)*T)
assert land[sr, sc], 'seed not on land in this test'
kept = lab == lab[sr, sc]
rr, cc = np.nonzero(kept)
def spread(x):
    x = x.astype(np.uint64); out = np.zeros_like(x)
    for b in range(32): out |= ((x >> np.uint64(b)) & np.uint64(1)) << np.uint64(2*b)
    return out
code = spread(rr) | (spread(cc) << np.uint64(1))
order = np.argsort(code, kind='stable'); n = len(order)
ids = np.full((R, C), -1, np.int64); ids[rr[order], cc[order]] = np.arange(n)
with open(wsg, 'rb') as f: d, m, nn = struct.unpack('<Bqq', f.read(17))
assert nn == n, (nn, n)
off = np.memmap(wsg, '<i8', 'r', 17, (n+1,))
rec = np.memmap(wsg, np.dtype([('v', '<i8'), ('w', '<i4'), ('pad', '<i4')]), 'r', 17+(n+1)*8, (m,))
dy = 6371008.8*np.pi/180/T
DR = [-1,-1,-1,0,0,1,1,1]; DC = [-1,0,1,-1,1,-1,0,1]
r0, c0 = rr[order], cc[order]
nbv, nbw = [], []
for k in range(8):
    r2, c2 = r0+DR[k], c0+DC[k]
    ok = (r2 >= 0) & (r2 < R) & (c2 >= 0) & (c2 < C)
    r2c, c2c = np.clip(r2, 0, R-1), np.clip(c2, 0, C-1)
    ok &= kept[r2c, c2c]
    lat = ((lat1 - r0/T) + (lat1 - r2c/T))/2*np.pi/180
    dx = dy*np.cos(lat)
    dist = np.sqrt((DR[k]**2)*dy*dy + (DC[k]**2)*dx*dx)
    s = (H[r2c, c2c].astype(np.float64) - H[r0, c0].astype(np.float64))/dist
    v = lambda s: 6*np.exp(-3.5*np.abs(s+0.05))
    t = dist*(1/v(s) + 1/v(-s))/2*3.6
    w = np.clip(np.rint(t*10), 1, 36000)   # llround vs rint differ only at exact .5
    nbv.append(np.where(ok, ids[r2c, c2c], np.iinfo(np.int64).max)); nbw.append(np.where(ok, w, 0))
V = np.stack(nbv, 1); W = np.stack(nbw, 1)
srt = np.argsort(V, 1); V = np.take_along_axis(V, srt, 1); W = np.take_along_axis(W, srt, 1)
deg = (V != np.iinfo(np.int64).max).sum(1)
exp_off = np.concatenate([[0], np.cumsum(deg)])
print('vertices', n, 'edges', m, 'offsets equal', np.array_equal(exp_off, off))
mask = V != np.iinfo(np.int64).max
print('destinations equal', np.array_equal(V[mask], rec['v']))
dw = W[mask].astype(np.int64) - rec['w']
print('weights equal', int((dw == 0).sum()), 'of', m, 'max |diff|', int(np.abs(dw).max()), 'pad zero', bool((rec['pad'] == 0).all()))
# undirected: weight(u,v) == weight(v,u)
u = np.repeat(np.arange(n), np.diff(np.asarray(off)))
a = np.lexsort((rec['v'], u)); b = np.lexsort((u, rec['v']))
print('symmetric', np.array_equal(u[a], rec['v'][b]) and np.array_equal(rec['w'][a], rec['w'][b]))
w = np.asarray(rec['w']); print('weight percentiles 1/50/99/99.9', np.percentile(w, [1, 50, 99, 99.9]), 'capped', int((w == 36000).sum()))
