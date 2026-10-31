import multiprocessing as mp, time, sys
def spin(end):
    x = 0
    while time.time() < end:
        x += 1
if __name__ == '__main__':
    secs = float(sys.argv[1]); n = mp.cpu_count()
    end = time.time() + secs
    ps = [mp.Process(target=spin, args=(end,)) for _ in range(n)]
    [p.start() for p in ps]; [p.join() for p in ps]
    print('busy done', n, 'procs', secs, 's')
