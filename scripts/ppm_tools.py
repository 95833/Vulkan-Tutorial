#!/usr/bin/env python3
"""读取本实验P6、检查场景、无第三方依赖写PNG；不参与Vulkan渲染。"""
import argparse
import collections
import hashlib
import json
from pathlib import Path
import struct
import zlib

BACKGROUND = (6, 9, 14)


def read_ppm(path):
    data = Path(path).read_bytes()
    index = 0

    def token():
        nonlocal index
        while index < len(data):
            if data[index] in b" \t\r\n":
                index += 1
            elif data[index] == ord('#'):
                newline = data.find(b'\n', index)
                if newline < 0:
                    raise ValueError('Unterminated PPM header comment')
                index = newline + 1
            else:
                break
        begin = index
        while index < len(data) and data[index] not in b" \t\r\n#":
            index += 1
        if begin == index:
            raise ValueError('Incomplete PPM header')
        return data[begin:index]

    if token() != b'P6':
        raise ValueError('Only binary P6 PPM is supported')
    width, height, maximum = int(token()), int(token()), int(token())
    if width < 1 or height < 1 or maximum != 255:
        raise ValueError('Expected positive dimensions and maxval=255')
    if index >= len(data) or data[index] not in b" \t\r\n":
        raise ValueError('Missing separator before binary pixels')
    # 只消费header的一个分隔符（或CRLF），绝不把像素中的空白字节跳掉。
    index += 2 if data[index:index + 2] == b'\r\n' else 1
    rgb = data[index:]
    if len(rgb) != width * height * 3:
        raise ValueError(f'Expected {width * height * 3} RGB bytes, got {len(rgb)}')
    return width, height, rgb


def pixel(rgb, width, x, y):
    start = (y * width + x) * 3
    return tuple(rgb[start:start + 3])


def statistics(width, height, rgb):
    counts = collections.Counter(zip(rgb[0::3], rgb[1::3], rgb[2::3]))
    changed = width * height - counts[BACKGROUND]
    x_sum = y_sum = 0
    left, right, top, bottom = width, -1, height, -1
    for y in range(height):
        for x in range(width):
            if pixel(rgb, width, x, y) != BACKGROUND:
                x_sum += x
                y_sum += y
                left, right = min(left, x), max(right, x)
                top, bottom = min(top, y), max(bottom, y)
    return {
        'width': width, 'height': height, 'rgb_bytes': len(rgb),
        'rgb_sha256': hashlib.sha256(rgb).hexdigest(),
        'changed_pixels': changed, 'changed_fraction': changed / (width * height),
        'unique_colors': len(counts),
        'center_rgb': pixel(rgb, width, width // 2, height // 2),
        'corner_rgb': pixel(rgb, width, 0, 0),
        'bbox': [left, top, right, bottom] if changed else None,
        'centroid': [x_sum / changed, y_sum / changed] if changed else None,
        'most_common': [{'rgb': color, 'count': count} for color, count in counts.most_common(8)]
    }


def assert_scene(width, height, rgb, expected):
    if (width, height) != (512, 512):
        raise AssertionError(f'Expected 512x512, got {width}x{height}')
    stats = statistics(width, height, rgb)
    colors = set(zip(rgb[0::3], rgb[1::3], rgb[2::3]))
    assert stats['corner_rgb'] == BACKGROUND, f'Wrong corner color: {stats["corner_rgb"]}'
    fraction = stats['changed_fraction']
    if expected == 'clear':
        assert stats['changed_pixels'] == 0, 'Clear must contain only the background'
    elif expected in ('triangle', 'vertex'):
        assert 0.25 < fraction < 0.28, f'Unexpected triangle coverage: {fraction}'
        for channel in range(3):
            assert any(c[channel] > 220 and sum(c) - c[channel] < 40 for c in colors), 'Missing RGB vertex region'
    elif expected == 'camera':
        assert 0.29 < fraction < 0.32, f'Unexpected unclipped camera-quad coverage: {fraction}'
        assert stats['unique_colors'] > 100, 'Expected interpolated colors'
    elif expected in ('depth', 'depth-off'):
        center = (0, 0, 255) if expected == 'depth-off' else (255, 0, 0)
        assert stats['center_rgb'] == center, f'Depth comparison failed: {stats["center_rgb"]}'
        assert {(255, 0, 0), (0, 0, 255)} <= colors, 'Both triangles must have visible portions'
    elif expected in ('texture', 'mips0', 'layer0'):
        assert 0.63 < fraction < 0.65, f'Unexpected textured quad coverage: {fraction}'
        assert colors == {BACKGROUND, (230, 50, 40), (240, 240, 240)}, f'Unexpected checker colors: {colors}'
    elif expected == 'mips6':
        assert 0.63 < fraction < 0.65
        foreground = colors - {BACKGROUND}
        assert len(foreground) == 1, f'Final 1x1 mip should be spatially uniform: {foreground}'
        color = next(iter(foreground))
        assert all(abs(a - b) <= 1 for a, b in zip(color, (235, 145, 140))), f'Wrong average: {color}'
    elif expected == 'layer1':
        assert 0.63 < fraction < 0.65
        assert colors == {BACKGROUND, (30, 220, 80), (15, 25, 15)}
    elif expected == 'layer2':
        assert 0.63 < fraction < 0.65
        assert colors == {BACKGROUND, (40, 90, 230), (245, 215, 40)}
    else:
        raise ValueError(f'Unknown scene check: {expected}')
    return stats


def write_png(path, width, height, rgb):
    def chunk(kind, payload):
        return struct.pack('>I', len(payload)) + kind + payload + struct.pack('>I', zlib.crc32(kind + payload) & 0xffffffff)
    raw = b''.join(b'\0' + rgb[y * width * 3:(y + 1) * width * 3] for y in range(height))
    result = b'\x89PNG\r\n\x1a\n'
    result += chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0))
    result += chunk(b'IDAT', zlib.compress(raw, 9))
    result += chunk(b'IEND', b'')
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path)
    parser.add_argument('--png', type=Path)
    parser.add_argument('--expect', choices=['clear', 'triangle', 'vertex', 'camera', 'texture', 'depth', 'depth-off', 'mips0', 'mips6', 'layer0', 'layer1', 'layer2'])
    parser.add_argument('--json', type=Path, help='Also save pixel statistics as JSON')
    args = parser.parse_args()
    width, height, rgb = read_ppm(args.input)
    stats = assert_scene(width, height, rgb, args.expect) if args.expect else statistics(width, height, rgb)
    print(json.dumps(stats, indent=2, ensure_ascii=False))
    if args.png:
        write_png(args.png, width, height, rgb)
        print(f'Saved {args.png}; original RGB bytes retained')
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(stats, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
