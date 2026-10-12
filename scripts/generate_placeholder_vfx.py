"""Writes the placeholder visual effects of the Royal combat content: a soft glow texture and two Effekseer projects
(a hit spark and a Burn aura). They are plain stand-ins, generated so that they can be regenerated and are easy to
tell from authored effects. Replace them by opening the effects in Effekseer from the editor's VFX tab."""
import struct, zlib
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1] / 'content/vfx/placeholder'

def png(path, size):
    rows = bytearray()
    for y in range(size):
        rows.append(0)
        for x in range(size):
            dx, dy = (x + .5) / size * 2 - 1, (y + .5) / size * 2 - 1
            falloff = max(0.0, 1 - (dx * dx + dy * dy) ** .5)
            alpha = int(255 * falloff * falloff)
            rows += bytes((255, 255, 255, alpha))
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', size, size, 8, 6, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(bytes(rows), 9)) + chunk(b'IEND', b''))

def triple(value):
    low, centre, high = value
    return f'<Center>{centre}</Center><Max>{high}</Max><Min>{low}</Min>'

def axes(name, x=None, y=None, z=None):
    body = ''.join(f'<{axis}>{triple(value)}</{axis}>' for axis, value in (('X', x), ('Y', y), ('Z', z)) if value)
    return f'<{name}>{body}</{name}>'

def node(name, generation, life, interval, velocity, acceleration, scale, grow, colour, fade, location=(None, None, None)):
    red, green, blue, alpha = colour
    return (f'<Node><CommonValues><MaxGeneration>{generation}</MaxGeneration><Life>{triple(life)}</Life><GenerationTime>{triple(interval)}</GenerationTime></CommonValues>'
            f'<LocationValues><Type>1</Type><PVA>{axes("Location", *location)}{axes("Velocity", *velocity)}{axes("Acceleration", *acceleration)}</PVA></LocationValues>'
            f'<ScalingValues><Type>1</Type><PVA>{axes("Scale", scale, scale, scale)}{axes("Velocity", grow, grow, grow)}</PVA></ScalingValues>'
            f'<RendererCommonValues><ColorTexture>Texture/glow.png</ColorTexture><AlphaBlend>2</AlphaBlend><ZWrite>False</ZWrite><FadeInType>1</FadeInType><FadeIn><Frame>2</Frame></FadeIn><FadeOutType>1</FadeOutType><FadeOut><Frame>{fade}</Frame></FadeOut></RendererCommonValues>'
            f'<DrawingValues><Sprite><ColorAll_Fixed><R>{red}</R><G>{green}</G><B>{blue}</B><A>{alpha}</A></ColorAll_Fixed></Sprite></DrawingValues><Name>{name}</Name><Children /></Node>')

# The element names used here are those of Effekseer's oldest project schema (the one its shipped samples use), so the file
# says so in ToolVersion; Effekseer upgrades it when it opens or compiles it. Naming a newer tool makes it skip that upgrade
# and silently ignore the colours.
def project(nodes, end, loop):
    return ('<?xml version="1.0" encoding="utf-8"?>\n<EffekseerProject><Root><Name>Root</Name><Children>' + ''.join(nodes) +
            f'</Children></Root><ToolVersion>0.7CTP1</ToolVersion><Version>3</Version><StartFrame>0</StartFrame><EndFrame>{end}</EndFrame><IsLoop>{loop}</IsLoop></EffekseerProject>\n')

(ROOT / 'Texture').mkdir(parents=True, exist_ok=True)
png(ROOT / 'Texture/glow.png', 64)
# Hit spark: one bright flash and a burst of small sparks that fly out and fall.
spark = project([
    node('Flash', '<Value>1</Value>', (6, 6, 6), (1, 1, 1), (None, None, None), (None, None, None), (.7, .7, .7), (.06, .06, .06), (255, 236, 190, 255), 5),
    node('Sparks', '<Value>14</Value>', (10, 14, 18), (.01, .01, .01), ((-.09, 0, .09), (-.02, .05, .12), (-.09, 0, .09)), (None, (-.006, -.006, -.006), None), (.06, .1, .14), (-.004, -.004, -.004), (255, 190, 80, 255), 8),
], 30, 'False')
# Burn aura: embers rising around the body for as long as the effect lasts.
aura = project([
    node('Embers', '<Infinite>True</Infinite>', (26, 34, 42), (.8, 1, 1.2), ((-.008, 0, .008), (.015, .025, .035), (-.008, 0, .008)), (None, None, None), (.18, .28, .38), (-.004, -.004, -.004), (255, 130, 40, 230), 16, ((-.45, 0, .45), (-.8, -.1, .6), (-.45, 0, .45))),
    node('Glow', '<Infinite>True</Infinite>', (30, 30, 30), (10, 10, 10), (None, (.004, .004, .004), None), (None, None, None), (2.4, 2.4, 2.4), (.006, .006, .006), (255, 90, 20, 110), 14),
], 120, 'True')
(ROOT / 'hit_spark.efkproj').write_text(spark, encoding='utf-8')
(ROOT / 'burn_aura.efkproj').write_text(aura, encoding='utf-8')
print('wrote', ROOT)
