"""Загружаем, инициализируем и выгружаем все плагины через штатный запуск Yate."""
from pathlib import Path
import re
import socket
import subprocess
import sys
import tempfile


def free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reservation:
        reservation.bind(('127.0.0.1', 0))
        return reservation.getsockname()[1]


binary = Path(sys.argv[1])
modules = Path(sys.argv[2])
share = Path(sys.argv[3]) if len(sys.argv) > 3 else binary.parent.parent / 'share/yate'
plugins = sorted(modules.rglob('*.yate'))
with tempfile.TemporaryDirectory(prefix='yate-module-test-') as directory:
    config = Path(directory)
    (config / 'user').mkdir()
    (config / 'yate.conf').write_text('[general]\nmodload=no\n[modules]\n' + ''.join(
        '{}=yes\n'.format(path.name) for path in plugins))
    # Слушатели тестовой копии ограничены loopback и отдельными временными портами.
    (config / 'ysipchan.conf').write_text('[general]\naddr=127.0.0.1\nport={}\nudp_force_bind=no\n'.format(free_port()))
    (config / 'yiaxchan.conf').write_text('[general]\naddr=127.0.0.1\nport={}\n'.format(free_port()))
    (config / 'rmanager.conf').write_text('[general]\nport=0\n')
    args = [str(binary), '-Dx', '-vvvvvvvvvv', '-c', str(config), '-u', str(config / 'user'),
            '-m', str(modules), '-e', str(share)]
    for subdir in sorted({str(path.parent.relative_to(modules)) for path in plugins} - {'.'}):
        args.extend(['-x', subdir])
    result = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            universal_newlines=True, timeout=30)
    loaded = re.search(r'Loaded (\d+) plugins', result.stdout)
    # Один файл может регистрировать несколько плагинов, например rex_debug.
    constructed = set(re.findall(r"SLib::SLib\([^,]+,'([^']+)'", result.stdout))
    missing = {str(path) for path in plugins} - constructed if constructed else set()
    linker_error = re.search(r'undefined symbol|cannot open shared object file|wrong ELF class', result.stdout)
    if result.returncode or not loaded or int(loaded[1]) < len(plugins) or missing or linker_error:
        (modules.parent / 'module-lifecycle.log').write_text(result.stdout)
        sys.stderr.write(result.stdout[-12000:])
        raise RuntimeError('Plugin lifecycle test failed: expected {} plugins, exit {}'.format(len(plugins), result.returncode))
print('Loaded, initialized and unloaded {} plugins'.format(len(plugins)))
