import glob


def has_ko(s):
    return any(0xAC00 <= ord(c) <= 0xD7A3 for c in s)


def strings_in(line):
    out = []
    i = 0
    n = len(line)
    while i < n:
        if line[i] == '"':
            j = i + 1
            buf = []
            while j < n:
                if line[j] == '\\' and j + 1 < n:
                    buf.append(line[j:j + 2])
                    j += 2
                    continue
                if line[j] == '"':
                    break
                buf.append(line[j])
                j += 1
            out.append(''.join(buf))
            i = j + 1
        else:
            i += 1
    return out


for path in glob.glob('Lib/aster/*.ast'):
    with open(path, encoding='utf-8') as f:
        lines = f.read().splitlines()
    for i, line in enumerate(lines, 1):
        for s in strings_in(line):
            if has_ko(s):
                print(f'{path}:{i}: {s}')
