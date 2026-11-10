# Generates the example mod's sprites (RGBA PNG) and sound (WAV) from code, so the repo
# carries no game assets. Usage: python make_demo_assets.py <mod folder>
import math, os, struct, sys, zlib, wave

out = sys.argv[1]
os.makedirs(os.path.join(out, 'Images'), exist_ok=True)
os.makedirs(os.path.join(out, 'Sounds'), exist_ok=True)

def png(path, w, h, px):
    raw = b''.join(b'\x00' + bytes(px[y * w * 4:(y + 1) * w * 4]) for y in range(h))
    def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    data = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) \
        + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b'')
    open(path, 'wb').write(data)

def canvas(w, h): return [0] * (w * h * 4)
def put(px, w, x, y, r, g, b, a):
    i = (y * w + x) * 4
    px[i:i + 4] = [r, g, b, a]

# Medkit: white rounded box with a red cross and a dark outline, 26x26.
W = H = 26
px = canvas(W, H)
for y in range(H):
    for x in range(W):
        dx, dy = max(0, abs(x - 12.5) - 9), max(0, abs(y - 12.5) - 9)
        d = math.hypot(dx, dy)
        if d < 3.2:
            edge = d > 2.0 or abs(x - 12.5) > 11.6 or abs(y - 12.5) > 11.6
            if edge: put(px, W, x, y, 40, 40, 40, 255)
            elif (abs(x - 12.5) < 2.6 and abs(y - 12.5) < 8) or (abs(y - 12.5) < 2.6 and abs(x - 12.5) < 8):
                put(px, W, x, y, 220, 30, 30, 255)
            else:
                put(px, W, x, y, 245, 245, 240, 255)
png(os.path.join(out, 'Images', 'medkit.png'), W, H, px)

# Plasma cell: cyan battery, 18x26.
W, H = 18, 26
px = canvas(W, H)
for y in range(H):
    for x in range(W):
        if 6 <= x <= 11 and 0 <= y <= 2:
            put(px, W, x, y, 200, 200, 200, 255)
        elif 1 <= x <= 16 and 3 <= y <= 25:
            border = x in (1, 16) or y in (3, 25)
            if border: put(px, W, x, y, 30, 60, 70, 255)
            else:
                glow = 0.5 + 0.5 * math.sin((y - 3) / 22 * math.pi)
                put(px, W, x, y, int(40 + 60 * glow), int(200 + 55 * glow), 255, 255)
png(os.path.join(out, 'Images', 'plasmacell.png'), W, H, px)

# Plasma bolt: 4-frame strip of a pulsing glowing orb, 16x16 per frame (additive, so
# black = transparent-looking; alpha also fades out).
F, S = 4, 16
W, H = F * S, S
px = canvas(W, H)
for f in range(F):
    rad = 5.0 + 1.5 * math.sin(f / F * 2 * math.pi)
    for y in range(S):
        for x in range(S):
            d = math.hypot(x - 7.5, y - 7.5)
            a = max(0.0, 1.0 - d / (rad + 2.5))
            if a <= 0: continue
            core = max(0.0, 1.0 - d / (rad * 0.5))
            r = int(min(255, 80 + 175 * core)); g = int(min(255, 180 + 75 * a)); b = 255
            put(px, W, f * S + x, y, r, g, b, int(255 * a))
png(os.path.join(out, 'Images', 'plasma.png'), W, H, px)

# Zap: a short falling square-ish sweep, 22050 Hz mono 16-bit.
rate, dur = 22050, 0.22
frames = bytearray()
phase = 0.0
for i in range(int(rate * dur)):
    t = i / rate
    freq = 1400 * (1 - t / dur) + 180
    phase += freq / rate
    v = 1.0 if (phase % 1.0) < 0.5 else -1.0
    v = 0.6 * v + 0.4 * math.sin(phase * 2 * math.pi)
    env = (1 - t / dur) ** 1.5
    frames += struct.pack('<h', int(v * env * 9000))
with wave.open(os.path.join(out, 'Sounds', 'zap.wav'), 'wb') as wf:
    wf.setnchannels(1); wf.setsampwidth(2); wf.setframerate(rate); wf.writeframes(bytes(frames))
print('assets written to', out)
