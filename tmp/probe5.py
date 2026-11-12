import re
for t in [r'\x1', r'\xzz', r'\u12', r'\U123', r'\N', r'\N{', r'\N{BOGUS NAME}', r'\0', r'\08', r'\099', r'\777', r'[\b]', r'[\A]', r'[\1]', r'[a-\x00]', r'(?<x', r'(?P', r'(?P=', r'(?P=1)']:
    try:
        re.compile(t)
        print(repr(t), '=> OK')
    except Exception as e:
        print(repr(t), '=>', str(e))
