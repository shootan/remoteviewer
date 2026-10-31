p = r'C:\work\gnlink-wt\ka-pc\apps\native_poc\src\viewer_managed_standby.cpp'
b = open(p, 'rb').read()
print('before', b.count(b'\x00'))
b = b.replace(b"L'" + b'\x00' + b"'", b"L'" + bytes([0x5C]) + b"0'")
open(p, 'wb').write(b)
print('after', b.count(b'\x00'))
