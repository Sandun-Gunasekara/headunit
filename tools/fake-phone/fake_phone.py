#!/usr/bin/env python3
"""Fake Android Auto phone ("head unit server", TCP 5277) for testing the headunit's connection
handling without a phone.

It answers the version request, completes the TLS handshake, then hangs up straight away (or
after --hold seconds), like a phone that drops right as the session starts. It accepts sessions
forever and prints one line per session, so a headunit that stops reconnecting is easy to spot.

    python3 tools/fake-phone/fake_phone.py [--port 5277] [--hold 0]

Point the desktop headunit at it with {"wifiTransport": true, "phoneIpAddress": "127.0.0.1"}.
"""
import argparse
import os
import socket
import ssl
import struct
import subprocess
import tempfile
import time

AA_VERSION_REQUEST, AA_VERSION_RESPONSE, AA_SSL_HANDSHAKE, AA_AUTH_COMPLETE = 1, 2, 3, 4


def make_tls_context():
    tmp = tempfile.mkdtemp()
    cert, key = os.path.join(tmp, 'cert.pem'), os.path.join(tmp, 'key.pem')
    # The headunit doesn't verify the phone's certificate, a throwaway one is enough
    subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-keyout', key, '-out', cert,
                    '-days', '1', '-subj', '/CN=fake-phone'], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(cert, key)
    return ctx


def read_exact(conn, n):
    buf = b''
    while len(buf) < n:
        chunk = conn.recv(n - len(buf))
        if not chunk:
            raise EOFError('headunit closed the connection')
        buf += chunk
    return buf


def read_frame(conn):
    chan, flags, length = struct.unpack('>BBH', read_exact(conn, 4))
    if (flags & 1) and not (flags & 2):
        read_exact(conn, 4)  # total size of a multi-frame message
    payload = read_exact(conn, length)
    return struct.unpack('>H', payload[:2])[0], payload[2:]


def send_frame(conn, msg_type, body):
    payload = struct.pack('>H', msg_type) + body
    conn.sendall(struct.pack('>BBH', 0, 3, len(payload)) + payload)


def handshake(conn, ctx):
    msg, _ = read_frame(conn)
    if msg != AA_VERSION_REQUEST:
        raise ValueError('expected VersionRequest, got %d' % msg)
    # Version 1.7, status OK: what Android Auto 17.7 answers
    send_frame(conn, AA_VERSION_RESPONSE, bytes([0, 1, 0, 7, 0, 0]))
    incoming, outgoing = ssl.MemoryBIO(), ssl.MemoryBIO()
    tls = ctx.wrap_bio(incoming, outgoing, server_side=True)
    while True:
        msg, body = read_frame(conn)
        if msg == AA_AUTH_COMPLETE:  # the headunit now considers the session started
            return
        incoming.write(body)
        try:
            tls.do_handshake()
        except ssl.SSLWantReadError:
            pass
        out = outgoing.read()
        if out:
            send_frame(conn, AA_SSL_HANDSHAKE, out)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--port', type=int, default=5277)
    parser.add_argument('--hold', type=float, default=0, help='seconds to keep the session open before hanging up')
    args = parser.parse_args()

    ctx = make_tls_context()
    srv = socket.socket()
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(('0.0.0.0', args.port))
    srv.listen(5)
    print('fake phone listening on :%d' % args.port, flush=True)
    session = 0
    while True:
        conn, _ = srv.accept()
        session += 1
        try:
            handshake(conn, ctx)
            time.sleep(args.hold)
            print('%s session %d: handshake done, hanging up' % (time.strftime('%H:%M:%S'), session), flush=True)
        except Exception as e:
            print('%s session %d: %s' % (time.strftime('%H:%M:%S'), session, e), flush=True)
        conn.close()


if __name__ == '__main__':
    main()
