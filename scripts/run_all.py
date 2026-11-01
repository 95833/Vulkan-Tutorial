#!/usr/bin/env python3
"""按课程运行、保存stdout/stderr和PNG，并检验跨实验因果对照。只用Python标准库。"""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import platform
import subprocess
from ppm_tools import read_ppm, assert_scene, statistics, write_png


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=Path('build/vulkan_lessons'))
    parser.add_argument('--out', type=Path, default=Path('out/all'))
    parser.add_argument('--no-validation', action='store_true', help='Explicitly skip validation if unavailable; recorded in summary')
    args = parser.parse_args()
    executable = args.exe.resolve()
    output = args.out.resolve()
    output.mkdir(parents=True, exist_ok=True)
    report = {'utc': datetime.now(timezone.utc).isoformat(), 'platform': platform.platform(),
              'executable': str(executable), 'validation_requested': not args.no_validation, 'runs': [], 'comparisons': {}}
    scenes = {}
    cases = [
        ('probe', 'probe', [], None),
        ('clear', 'clear', ['--shader-dir', str(output / 'deliberately_missing_shaders')], 'clear'),
        ('triangle', 'triangle', [], 'triangle'),
        ('vertex', 'vertex', [], 'vertex'),
        ('camera0', 'camera', ['--shift', '0'], 'camera'),
        ('camera_shift', 'camera', ['--shift', '0.35'], 'camera'),
        ('texture', 'texture', [], 'texture'),
        ('depth', 'depth', [], 'depth'),
        ('depth_off', 'depth', ['--no-depth'], 'depth-off'),
    ]
    cases += [(f'mips{level}', 'mips', ['--lod', str(level)], 'mips0' if level == 0 else 'mips6' if level == 6 else None) for level in range(7)]
    cases += [(f'layer{layer}', 'layers', ['--layer', str(layer)], f'layer{layer}') for layer in range(3)]
    cases += [(f'frames{slots}', 'frames', ['--frames', '9', '--inflight', str(slots)], 'camera') for slots in (1, 2)]
    try:
        for name, lesson, extra, expected in cases:
            command = [str(executable), '--lesson', lesson, '--output', str(output / (name + '.ppm')), *extra]
            if not args.no_validation:
                command.append('--validation')
            result = subprocess.run(command, capture_output=True, text=True, timeout=180)
            (output / (name + '.stdout.txt')).write_text(result.stdout, encoding='utf-8')
            (output / (name + '.stderr.txt')).write_text(result.stderr, encoding='utf-8')
            item = {'name': name, 'lesson': lesson, 'command': command, 'exit_code': result.returncode, 'stderr_bytes': len(result.stderr.encode())}
            report['runs'].append(item)
            assert result.returncode == 0, f'{name} exited {result.returncode}: {result.stderr}'
            assert not result.stderr, f'{name} produced stderr: {result.stderr}'
            assert 'Validation summary: errors=0; warnings=0' in result.stdout, f'{name}: validation warnings/errors'
            if not args.no_validation:
                assert 'synchronization validation explicitly enabled' in result.stdout, f'{name}: requested validation was not enabled'
            if lesson != 'probe':
                image = read_ppm(output / (name + '.ppm'))
                stats = assert_scene(*image, expected) if expected else statistics(*image)
                write_png(output / (name + '.png'), *image)
                item['pixels'] = stats
                scenes[name] = image
            print(f'PASS {name}', flush=True)

        assert scenes['triangle'][2] == scenes['vertex'][2], 'Procedural and vertex-buffer triangles differ'
        report['comparisons']['triangle_equals_vertex'] = True
        stats0, stats1 = statistics(*scenes['camera0']), statistics(*scenes['camera_shift'])
        displacement = stats1['centroid'][0] - stats0['centroid'][0]
        assert abs(displacement - 0.35 * 512 / 2) <= 1, f'Camera X shift not observed: {displacement}'
        assert abs(stats1['centroid'][1] - stats0['centroid'][1]) < 0.01, 'Camera should not move vertically'
        report['comparisons']['camera_displacement_pixels'] = displacement
        assert scenes['texture'][2] == scenes['mips0'][2] == scenes['layer0'][2], 'Base image sampling routes differ'
        report['comparisons']['texture_equals_mip0_equals_layer0'] = True
        assert scenes['mips0'][2] != scenes['mips6'][2], 'LOD selection produced no change'
        report['comparisons']['mip6_differs_from_mip0'] = True
        assert scenes['depth'][2] != scenes['depth_off'][2], 'Depth toggle produced no change'
        report['comparisons']['depth_toggle_changes_overlap'] = True

        centers = []
        for frame in (0, 4, 8):
            one = read_ppm(output / f'frames1_{frame:03}.ppm')
            two = read_ppm(output / f'frames2_{frame:03}.ppm')
            assert one[2] == two[2], f'1-slot and 2-slot output mismatch at frame {frame}'
            centers.append(statistics(*one)['centroid'][0])
            write_png(output / f'frames1_{frame:03}.png', *one)
            write_png(output / f'frames2_{frame:03}.png', *two)
        assert centers[0] < centers[1] < centers[2], 'Frame positions did not advance rightward'
        for slots in (1, 2):
            assert scenes[f'frames{slots}'][2] == read_ppm(output / f'frames{slots}_008.ppm')[2]
        report['comparisons']['one_and_two_slot_representative_frames_equal'] = True
        report['comparisons']['frame_centroid_x'] = centers

        cpu_executable = executable.with_name('cpu_reference.exe' if executable.suffix == '.exe' else 'cpu_reference')
        if cpu_executable.exists():
            cpu_output = output / 'cpu.ppm'
            result = subprocess.run([str(cpu_executable), str(cpu_output)], capture_output=True, text=True, timeout=30)
            (output / 'cpu.stdout.txt').write_text(result.stdout, encoding='utf-8')
            (output / 'cpu.stderr.txt').write_text(result.stderr, encoding='utf-8')
            assert result.returncode == 0 and not result.stderr
            cpu_image = read_ppm(cpu_output)
            assert_scene(*cpu_image, 'clear')
            assert cpu_image[2] == scenes['clear'][2]
            write_png(output / 'cpu.png', *cpu_image)
            report['comparisons']['cpu_reference_equals_clear'] = True
        report['status'] = 'PASS'
    except Exception as error:
        report['status'] = 'FAIL'
        report['error'] = str(error)
        raise
    finally:
        (output / 'summary.json').write_text(json.dumps(report, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    print(f'ALL PASS: {len(cases)} Vulkan runs; summary: {output / "summary.json"}')


if __name__ == '__main__':
    main()
