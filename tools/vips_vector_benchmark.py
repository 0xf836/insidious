#!/usr/bin/env python3
"""Developer-only SIMD comparison; fixtures are temporary, results are explicit JSON."""
import argparse
import ctypes
import ctypes.util
import datetime
import json
import os
from pathlib import Path
import platform
import statistics
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    env = dict(os.environ, VIPS_CONCURRENCY='4')
    for key in ('VIPS_VECTOR', 'VIPS_NOVECTOR', 'IM_NOVECTOR'):
        env.pop(key, None)
        os.environ.pop(key, None)
    os.environ['VIPS_CONCURRENCY'] = '4'
    lib = ctypes.CDLL(ctypes.util.find_library('vips'))
    lib.vips_init.argtypes = [ctypes.c_char_p]
    if lib.vips_init(b'insidious-vector-benchmark') != 0:
        raise RuntimeError('libvips initialization failed')
    targets = {}
    for name in ('vips_vector_get_builtin_targets', 'vips_vector_get_supported_targets'):
        fn = getattr(lib, name)
        fn.restype = ctypes.c_int64
        targets[name] = fn()
    lib.vips_shutdown()
    cpu = Path('/proc/cpuinfo').read_text().split('\n\n')[0]
    report = {
        'date_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'platform': platform.platform(), 'cpu': cpu,
        'cpu_affinity': sorted(os.sched_getaffinity(0)), 'simd_targets': targets,
        'os_release': Path('/etc/os-release').read_text(),
        'vips_version': subprocess.check_output(['vips', '--version'], text=True).strip(),
        'method': {'width': 6000, 'height': 4000, 'concurrency': 4,
                   'pairs': 6, 'discard_first_pairs': 1,
                   'command': 'vips resize INPUT.v OUTPUT.v SCALE --kernel KERNEL',
                   'scalar_environment': {'VIPS_NOVECTOR': '1'}},
        'results': [],
    }
    with tempfile.TemporaryDirectory(prefix='insidious-vector-') as directory:
        root = Path(directory)
        with (root / 'input.ppm').open('wb') as file:
            file.write(b'P6\n6000 4000\n255\n')
            row = bytes(c for x in range(6000) for c in (x % 256, (x * 7) % 256, (x * 13) % 256))
            for _ in range(4000):
                file.write(row)
        subprocess.run(['vips', 'copy', str(root / 'input.ppm'), str(root / 'input.v')], env=env, check=True)
        for kernel in ('linear', 'mitchell', 'lanczos3', 'mks2021'):
            for scale in ('0.125', '0.625'):
                samples = []
                for pair in range(6):
                    order = ('simd', 'scalar') if pair % 2 == 0 else ('scalar', 'simd')
                    for mode in order:
                        run_env = dict(env)
                        if mode == 'scalar':
                            run_env['VIPS_NOVECTOR'] = '1'
                        start = time.perf_counter()
                        subprocess.run(['vips', 'resize', str(root / 'input.v'),
                                        str(root / (mode + '.v')), scale, '--kernel', kernel],
                                       env=run_env, check=True, stdout=subprocess.DEVNULL)
                        samples.append({'pair': pair, 'mode': mode, 'warmup': pair == 0,
                                        'seconds': time.perf_counter() - start})
                medians = {mode: statistics.median(s['seconds'] for s in samples
                           if s['mode'] == mode and not s['warmup']) for mode in ('simd', 'scalar')}
                result = {'kernel': kernel, 'scale': scale, 'samples': samples,
                          'median_seconds': medians, 'slowdown': medians['scalar'] / medians['simd']}
                report['results'].append(result)
                print(kernel, scale, medians, flush=True)
    with args.output.open('x') as file:
        json.dump(report, file, indent=2)
        file.write('\n')


if __name__ == '__main__':
    main()
