import re
for t in ['(?(1)a|b|c)', '(a)(?(1)b|c)', '(?(1)a|b)(x)', '(a)?(?(1)b|c)', '(?i)(?m)a', 'a(?i)(?m)b', '(?i-m:a)', '(?i-m)', '(?u)', '(?a)', '(?L)', '(?x) a b', '(?x)a#c\nb', '(?x)[a b]', '(?x)a{ 1,2 }', '(?P<a>x)(?P<a>y)']:
    try:
        p = re.compile(t)
        print(repr(t), '=> OK')
    except Exception as e:
        print(repr(t), '=>', str(e))
print(repr(re.match(r'(a)?(?(1)b|c)', 'c')))
print(repr(re.match(r'(a)?(?(1)b|c)', 'ab')))
print(repr(re.compile(r'(?x) a b ').pattern))
