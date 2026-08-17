import os, sys, termios

port = '/dev/cu.usbserial-A5069RR4'
fd = os.open(port, os.O_RDWR | os.O_NOCTTY)

a = termios.tcgetattr(fd)
a[0] = 0
a[1] = 0
a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
a[3] = 0
a[4] = termios.B115200
a[5] = termios.B115200
termios.tcsetattr(fd, termios.TCSANOW, a)
termios.tcflush(fd, termios.TCIFLUSH)

out = open(sys.argv[1], 'wb', buffering=0)
while True:
    d = os.read(fd, 256)
    if d:
        out.write(d)
