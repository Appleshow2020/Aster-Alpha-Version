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
                    buf.append(line[j:j+2])
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

seen = {}
for path in sorted(glob.glob('Programs/*.c') + glob.glob('Parser/*.c') +
                   glob.glob('Parser/lexer/*.c') + glob.glob('Python/*.c') +
                   glob.glob('Objects/*.c')):
    with open(path, encoding='utf-8') as f:
        lines = f.read().splitlines()
    for i, line in enumerate(lines, 1):
        for s in strings_in(line):
            if has_ko(s):
                key = (path, s)
                seen.setdefault(key, []).append(i)

for (path, s), loci in seen.items():
    print(f'{path}:{loci[0]}\t{s}')
print('UNIQUE', len(seen))
