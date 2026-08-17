import os, sys, time, termios, fcntl, struct

PORT = '/dev/cu.usbserial-A5069RR4'
PAGE = 128
TIOCMBIS = 0x8004746C
TIOCMBIC = 0x8004746B
TIOCM_DTR = 0x0002
TIOCM_RTS = 0x0004
INSYNC, OK, EOP = 0x14, 0x10, 0x20


def load_hex(path):
    flash = bytearray()
    for line in open(path):
        line = line.strip()
        if not line.startswith(':'):
            continue
        raw = bytes.fromhex(line[1:])
        n, addr, typ = raw[0], (raw[1] << 8) | raw[2], raw[3]
        if sum(raw) & 0xFF:
            raise SystemExit('hex checksum error: ' + line)
        if typ == 1:
            break
        if typ != 0:
            continue
        if addr + n > len(flash):
            flash.extend(b'\xff' * (addr + n - len(flash)))
        flash[addr:addr + n] = raw[4:4 + n]
    return bytes(flash)


def open_port():
    fd = os.open(PORT, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    fcntl.fcntl(fd, fcntl.F_SETFL, 0)
    a = termios.tcgetattr(fd)
    a[0] = 0
    a[1] = 0
    a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    a[3] = 0
    a[4] = termios.B57600
    a[5] = termios.B57600
    a[6][termios.VMIN] = 0
    a[6][termios.VTIME] = 3
    termios.tcsetattr(fd, termios.TCSANOW, a)
    return fd


def read_exact(fd, n, deadline=2.0):
    buf = b''
    end = time.time() + deadline
    while len(buf) < n and time.time() < end:
        chunk = os.read(fd, n - len(buf))
        if chunk:
            buf += chunk
    return buf


def cmd(fd, payload, nresp=0):
    termios.tcflush(fd, termios.TCIFLUSH)
    os.write(fd, payload + bytes([EOP]))
    r = read_exact(fd, nresp + 2)
    if len(r) != nresp + 2 or r[0] != INSYNC or r[-1] != OK:
        raise SystemExit('protocol error on %r -> %s' % (payload[:1], r.hex()))
    return r[1:-1]


def enter_bootloader(fd):
    fcntl.ioctl(fd, TIOCMBIS, struct.pack('i', TIOCM_DTR | TIOCM_RTS))
    time.sleep(0.25)
    fcntl.ioctl(fd, TIOCMBIC, struct.pack('i', TIOCM_DTR | TIOCM_RTS))
    for _ in range(20):
        termios.tcflush(fd, termios.TCIOFLUSH)
        os.write(fd, b'0 ')
        time.sleep(0.05)
        r = os.read(fd, 32)
        if r and r[-2:] == bytes([INSYNC, OK]):
            return True
    return False


hexfile = sys.argv[1]
flash = load_hex(hexfile)
print('firmware: %d bytes, %d pages' % (len(flash), (len(flash) + PAGE - 1) // PAGE))

fd = open_port()
if not enter_bootloader(fd):
    raise SystemExit('bootloader did not answer')
print('bootloader: in sync')

sig = cmd(fd, b'u', 3)
print('signature: %s' % sig.hex())
if sig != b'\x1e\x95\x0f':
    raise SystemExit('unexpected signature, aborting before write')

cmd(fd, b'P')

for off in range(0, len(flash), PAGE):
    chunk = flash[off:off + PAGE]
    if len(chunk) < PAGE:
        chunk += b'\xff' * (PAGE - len(chunk))
    word = off >> 1
    cmd(fd, b'U' + bytes([word & 0xFF, (word >> 8) & 0xFF]))
    cmd(fd, b'd' + bytes([len(chunk) >> 8, len(chunk) & 0xFF]) + b'F' + chunk)
print('written: %d bytes' % len(flash))

bad = 0
for off in range(0, len(flash), PAGE):
    chunk = flash[off:off + PAGE]
    word = off >> 1
    cmd(fd, b'U' + bytes([word & 0xFF, (word >> 8) & 0xFF]))
    back = cmd(fd, b't' + bytes([PAGE >> 8, PAGE & 0xFF]) + b'F', PAGE)
    if back[:len(chunk)] != chunk:
        bad += 1
        print('MISMATCH at 0x%04x' % off)
print('verify: %s' % ('OK' if bad == 0 else '%d bad pages' % bad))

cmd(fd, b'Q')
os.close(fd)
print('done, sketch running')
