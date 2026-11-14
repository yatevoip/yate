"""Локальный SIP-звонок на генератор тона с проверкой RTP и завершения сеанса."""
from pathlib import Path
import re
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import time
import uuid


def udp_socket():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(('127.0.0.1', 0))
    sock.settimeout(0.2)
    return sock


def response(sock, method, timeout=8):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        try:
            data, _ = sock.recvfrom(65535)
        except socket.timeout:
            continue
        message = data.decode('utf-8', 'replace')
        if not re.search(r'^CSeq: \d+ ' + method + r'\r?$', message, re.M | re.I):
            continue
        status = re.match(r'SIP/2.0 (\d+)', message)
        if status and int(status[1]) >= 200:
            if int(status[1]) != 200:
                raise RuntimeError(message)
            return message
    raise RuntimeError('Timed out waiting for SIP ' + method)


def run(binary, modules):
    with tempfile.TemporaryDirectory(prefix='yate-sip-test-') as directory:
        directory = Path(directory)
        config = directory / 'conf'
        config.mkdir()
        user = directory / 'user'
        user.mkdir()
        sip = udp_socket()
        audio = udp_socket()
        reservation = udp_socket()
        port = reservation.getsockname()[1]
        reservation.close()
        client_port = sip.getsockname()[1]
        audio_port = audio.getsockname()[1]
        (config / 'yate.conf').write_text(
            '[general]\nmodload=no\n[modules]\nysipchan.yate=yes\nyrtpchan.yate=yes\n'
            'regexroute.yate=yes\ntonegen.yate=yes\n')
        (config / 'ysipchan.conf').write_text(
            '[general]\naddr=127.0.0.1\nport={}\nrtp_localip=127.0.0.1\n'
            'udp_force_bind=no\n[codecs]\ndefault=no\nmulaw=yes\n'.format(port))
        (config / 'regexroute.conf').write_text('[default]\n^cmake-test$=tone/dial\n')
        call_id = uuid.uuid4().hex
        remote = ('127.0.0.1', port)
        uri = 'sip:cmake-test@127.0.0.1:{}'.format(port)
        from_header = '<sip:cmake@127.0.0.1:{}>;tag=cmake'.format(client_port)
        to_header = '<{}>'.format(uri)

        def request(method, sequence, body='', to=None):
            lines = [
                '{} {} SIP/2.0'.format(method, uri),
                'Via: SIP/2.0/UDP 127.0.0.1:{};branch=z9hG4bK{}'.format(client_port, uuid.uuid4().hex),
                'Max-Forwards: 70', 'From: ' + from_header,
                'To: ' + (to or to_header), 'Call-ID: ' + call_id,
                'CSeq: {} {}'.format(sequence, method),
                'Contact: <sip:cmake@127.0.0.1:{}>'.format(client_port)]
            if body:
                lines.append('Content-Type: application/sdp')
            lines.append('Content-Length: {}'.format(len(body.encode('ascii'))))
            return ('\r\n'.join(lines) + '\r\n\r\n' + body).encode('ascii')

        with (directory / 'yate.log').open('wb') as log:
            process = subprocess.Popen([binary, '-c', str(config), '-u', str(user), '-m', modules],
                                       stdout=log, stderr=subprocess.STDOUT)
            try:
                # OPTIONS позволяет дождаться запуска без фиксированной задержки.
                deadline = time.monotonic() + 10
                while time.monotonic() < deadline:
                    if process.poll() is not None:
                        raise RuntimeError('Yate exited during startup')
                    sip.sendto(request('OPTIONS', 1), remote)
                    try:
                        response(sip, 'OPTIONS', 0.3)
                        break
                    except RuntimeError:
                        pass
                else:
                    raise RuntimeError('SIP listener did not start')
                offer = ('v=0\r\no=cmake 1 1 IN IP4 127.0.0.1\r\ns=cmake\r\n'
                         'c=IN IP4 127.0.0.1\r\nt=0 0\r\nm=audio {} RTP/AVP 0\r\n'
                         'a=rtpmap:0 PCMU/8000\r\na=sendrecv\r\n').format(audio_port)
                sip.sendto(request('INVITE', 2, offer), remote)
                answer = response(sip, 'INVITE')
                peer_to = re.search(r'^To: (.+)\r?$', answer, re.M | re.I)[1].strip()
                peer_port = int(re.search(r'^m=audio (\d+)', answer, re.M)[1])
                sip.sendto(request('ACK', 2, to=peer_to), remote)
                # Отправляем RTP, чтобы отработали также приём и определение адреса.
                for number in range(3):
                    packet = struct.pack('!BBHII', 0x80, 0, number, number * 160, 1234) + b'\xff' * 160
                    audio.sendto(packet, ('127.0.0.1', peer_port))
                packets = []
                deadline = time.monotonic() + 5
                while len(packets) < 3 and time.monotonic() < deadline:
                    try:
                        packet, _ = audio.recvfrom(4096)
                    except socket.timeout:
                        continue
                    if len(packet) > 12 and packet[0] >> 6 == 2 and packet[1] & 0x7f == 0:
                        packets.append(packet)
                if len(packets) < 3 or all(set(packet[12:]) <= {255} for packet in packets):
                    raise RuntimeError('No PCMU tone received over RTP')
                sip.sendto(request('BYE', 3, to=peer_to), remote)
                response(sip, 'BYE')
                print('SIP OPTIONS, INVITE/ACK, RTP tone and BYE passed')
            except Exception:
                log.flush()
                sys.stderr.write((directory / 'yate.log').read_text(errors='replace'))
                raise
            finally:
                if process.poll() is None:
                    process.send_signal(signal.SIGTERM)
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                sip.close()
                audio.close()
            if process.returncode != 0:
                raise RuntimeError('Yate exit code {}'.format(process.returncode))


if __name__ == '__main__':
    run(sys.argv[1], sys.argv[2])
