import re
for t in [r'\1(a)', r'(a)\1', r'(a)\11', r'\11', r'\111', r'\0', r'\08', r'\400', r'\g', r'\g<1>', r'\u00e9', r'\U0001F600', r'\N{LATIN SMALL LETTER A}']:
    try:
        p = re.compile(t)
        print(repr(t), '=> OK')
    except Exception as e:
        print(repr(t), '=>', str(e))
print(bool(re.match(r'\1(a)', '\x01a')))
print(repr(re.match(r'(a)(b)(c)(d)(e)(f)(g)(h)(i)(j)(k)\11', 'abcdefghijk\x09')))
