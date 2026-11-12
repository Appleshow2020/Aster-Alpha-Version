src = open('tmp/translate_ko.py', encoding='utf-8').read()
cut = src.index('total = 0')
ns = {}
exec(src[:cut], ns)
M = ns['M']
s = open('Programs/pkg.c', encoding='utf-8').read()
i = 0
n = len(s)
found = 0
while i < n:
    if s[i] == '"':
        j = i + 1
        buf = []
        while j < n:
            if s[j] == '\\' and j + 1 < n:
                buf.append(s[j:j + 2])
                j += 2
                continue
            if s[j] == '"':
                break
            buf.append(s[j])
            j += 1
        c = ''.join(buf)
        if 'init [name]' in c:
            found += 1
            out = ['content=%r' % c, 'inM=%s' % (c in M)]
            for k in M:
                if 'init [name]' in k:
                    out.append('key=%r eq=%s' % (k, k == c))
                    diff = [(idx, hex(ord(a)), hex(ord(b))) for idx, (a, b) in enumerate(zip(k, c)) if a != b]
                    out.append('diff=%r' % diff[:5])
            open('tmp/probe2.txt', 'w', encoding='utf-8').write('\n'.join(out))
            break
        i = j + 1
    else:
        i += 1
print('found', found)
