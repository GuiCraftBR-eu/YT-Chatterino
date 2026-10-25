"""Executa aquecimento + medições alternadas dos dois renderizadores, sem rede."""
from pathlib import Path
import argparse
import json
import os
import platform
import statistics
import subprocess
import sys
import time

parser = argparse.ArgumentParser()
parser.add_argument('--legacy-source', required=True)
parser.add_argument('--native', required=True)
parser.add_argument('--output', required=True)
parser.add_argument('--runs', type=int, default=7)
args = parser.parse_args()
output = Path(args.output).resolve()
output.mkdir(parents=True, exist_ok=True)
native = Path(args.native).resolve()
legacy_harness = Path(__file__).with_name('legacy_130.py')
environment = os.environ.copy()
environment.update(QT_SCALE_FACTOR='1', QT_ENABLE_HIGHDPI_SCALING='0', QT_QPA_PLATFORM='windows')
results = {'1.3.0': [], '2.1.0': []}

def run(version, index):
    folder = output / f'{version}-{index}'
    folder.mkdir(exist_ok=True)
    result_file = folder / 'result.json'
    ready_file = Path(str(result_file) + '.ready')
    if result_file.exists() or ready_file.exists():
        raise RuntimeError('Use uma pasta nova para evitar resultados antigos.')
    common = ['--profile', str(folder / 'profile'), '--output', str(result_file)]
    command = ([sys.executable, str(legacy_harness), '--source', args.legacy_source] if version == '1.3.0'
               else [str(native)]) + common
    with (folder / 'stdout.txt').open('w', encoding='utf-8') as stdout, (folder / 'stderr.txt').open('w', encoding='utf-8') as stderr:
        started = time.perf_counter()
        child = subprocess.Popen(command, env=environment, stdout=stdout, stderr=stderr,
                                 creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        while not ready_file.exists():
            if child.poll() is not None:
                raise RuntimeError(f'{version} encerrou antes do primeiro quadro: {folder}')
            if time.perf_counter() - started > 30:
                child.terminate()
                raise TimeoutError(f'{version}: inicialização excedeu 30 segundos')
            time.sleep(.001)
        startup_ms = (time.perf_counter() - started) * 1000
        code = child.wait(timeout=45)
    if code != 0:
        raise RuntimeError(f'{version}: falha {code}; veja {folder}')
    data = json.loads(result_file.read_text(encoding='utf-8'))
    assert data['messages'] == 2000 and data['network_requests'] == 0, data
    data['startup_ms'] = startup_ms
    return data

for version in ['1.3.0', '2.1.0']:
    run(version, 'warmup')
for index in range(args.runs):
    for version in (['1.3.0', '2.1.0'] if index % 2 == 0 else ['2.1.0', '1.3.0']):
        result = run(version, index)
        results[version].append(result)
        print(json.dumps({'version': version, 'run': index + 1, 'startup_ms': round(result['startup_ms'], 2),
                          'load_2000_ms': round(result['load_2000_ms'], 2), 'resize_80_ms': round(result['resize_80_ms'], 2)}), flush=True)

summary = {}
for version, runs in results.items():
    summary[version] = {}
    for metric in ['startup_ms', 'load_2000_ms', 'resize_80_ms']:
        values = [item[metric] for item in runs]
        summary[version][metric] = {'median_ms': statistics.median(values), 'min_ms': min(values), 'max_ms': max(values)}
report = {'platform': platform.platform(), 'runs_per_version': args.runs,
          'messages': 2000, 'resizes': 80, 'fonts_pt': 12, 'network': False,
          'legacy_runtime': sys.version.split()[0], 'results': results, 'summary': summary}
(output / 'comparison.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps({'summary': summary}, ensure_ascii=False), flush=True)
