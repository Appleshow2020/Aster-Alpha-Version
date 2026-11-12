import re
for t in ['{', '{1}', '{,3}', 'a{,3}', 'a{1', 'a{1,', 'a{1,2', 'a{2,1}', '}', ']', 'a{1,2}?', 'a{1,2}+', '(?P<name>a)(?P<name2>b)', '(?P<name>a)\1', '(?<=a)b', '(?<!a)b', '(?<!ab)c', '(?<=a|b)c', '(?>)', '(?','(?P','(?P<','(?P<a>', '(?(1)', '(?()', '(?()a|b)', '(?P=a)']:
    try:
        re.compile(t)
        print(repr(t), '=> OK')
    except Exception as e:
        print(repr(t), '=>', str(e))
