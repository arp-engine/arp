"""Whitespace-independent C++ source assertions, preserving literal/token identity.

This is a lexical evidence helper, not a parser. Slices keep the original source
so regex assertions and scoped method readers still inspect the actual code.
"""
import re
from functools import lru_cache


_TOKENS = re.compile(
    r'//[^\n]*|/\*.*?\*/|(?:u8|u|U|L)?R"(?P<delimiter>[^ ()\\\t\r\n]{0,16})\(.*?\)(?P=delimiter)"'
    r'|(?:u8|u|U|L)?"(?:\\.|[^"\\])*"|(?:u8|u|U|L)?\'(?:\\.|[^\'\\])*\''
    r"|[A-Za-z_]\w*|(?:\d[\w.']*|\.\d[\w.]*)"
    r'|===|!==|=>|\?\?=|\?\?|\?\.|\*\*=|\*\*|>>=|<<=|<=>|->\*|\.\.\.|::|->|\+\+|--|&&|\|\||==|!=|<=|>=|<<|>>'
    r'|\+=|-=|\*=|/=|%=|&=|\|=|\^=|[^\s]', re.S)


@lru_cache(maxsize=512)
def tokens(source):
    return [(m.group(), m.start(), m.end()) for m in _TOKENS.finditer(source)]


def switch_regions(source):
    """Yield lexical switch bodies and direct labels, excluding nested switches.

    Offsets refer to the unmodified source. This only inventories evidence;
    it does not establish the expression's C++ type or prove execution safety.
    Comments and literals are already atomic in the shared tokenizer.
    """
    lexical = [t for t in tokens(source) if not t[0].startswith(('//', '/*'))]
    for index, token in enumerate(lexical):
        if token[0] != 'switch' or index + 1 >= len(lexical) or lexical[index + 1][0] != '(':
            continue
        opening = index + 1
        depth, cursor = 1, opening + 1
        while cursor < len(lexical) and depth:
            depth += (lexical[cursor][0] == '(') - (lexical[cursor][0] == ')')
            cursor += 1
        if depth or cursor >= len(lexical) or lexical[cursor][0] != '{':
            continue
        expression = source[lexical[opening][2]:lexical[cursor - 1][1]]
        body_opening = cursor
        depth, cursor, labels = 1, cursor + 1, []
        while cursor < len(lexical) and depth:
            value = lexical[cursor][0]
            if depth == 1 and value in ('case', 'default'):
                closing = cursor + 1
                while closing < len(lexical) and lexical[closing][0] != ':':
                    closing += 1
                if closing < len(lexical):
                    labels.append(source[lexical[cursor][1]:lexical[closing][2]])
            depth += (value == '{') - (value == '}')
            cursor += 1
        if not depth:
            yield dict(start=token[1], end=lexical[cursor - 1][2],
                       expression=expression, labels=labels,
                       body=source[lexical[body_opening][2]:lexical[cursor - 1][1]])


def embedded_javascript_regions(source):
    """Locate Emscripten JavaScript bodies with their original source offsets."""
    lexical = tokens(source)
    regions = []
    macro = re.compile(r'(?:EM_(?:ASYNC_)?JS|(?:MAIN_THREAD_)?EM_ASM(?:_INT|_DOUBLE|_PTR)?)$')
    index = 0
    while index < len(lexical):
        name, start, _ = lexical[index]
        if not macro.fullmatch(name) or index + 1 == len(lexical) or lexical[index + 1][0] != '(':
            index += 1
            continue
        begin = index
        depth = 1
        index += 2
        while depth:
            depth += (lexical[index][0] == '(') - (lexical[index][0] == ')')
            index += 1
        body = next(i for i in range(begin, index) if lexical[i][0] == '{')
        depth = 1
        closing = body + 1
        while depth:
            depth += (lexical[closing][0] == '{') - (lexical[closing][0] == '}')
            closing += 1
        regions.append((name, start, lexical[index - 1][2],
                        lexical[body][2], lexical[closing - 1][1]))
    return regions


class CppSource(str):
    def _matches(self, needle, start=0, end=None):
        end = len(self) if end is None else end
        raw = str(self)
        position = raw.find(needle, start, end)
        matches = []
        while position >= 0:
            matches.append((position, position + len(needle)))
            position = raw.find(needle, position + max(1, len(needle)), end)
        expected = [t[0] for t in tokens(needle)]
        if len(expected) <= 1 and needle.strip() != needle:
            return matches
        if not expected:
            return matches
        actual = tokens(str(self)[start:end])
        if not any(t.startswith(('//', '/*')) for t in expected):
            actual = [t for t in actual if not t[0].startswith(('//', '/*'))]
        width = len(expected)
        lexical = [(start + actual[i][1], start + actual[i + width - 1][2])
                for i in range(len(actual) - width + 1)
                if actual[i][0] == expected[0] and [t[0] for t in actual[i:i + width]] == expected]
        return sorted(dict(matches + lexical).items())

    def __contains__(self, needle):
        return bool(self._matches(needle))

    def find(self, needle, start=0, end=None):
        matches = self._matches(needle, start, end)
        return matches[0][0] if matches else -1

    def index(self, needle, start=0, end=None):
        result = self.find(needle, start, end)
        if result < 0:
            raise ValueError("Source tokens not found: " + needle)
        return result

    def count(self, needle, start=0, end=None):
        matches = self._matches(needle, start, end)
        count, previous = 0, -1
        for opening, closing in matches:
            if opening >= previous:
                count += 1
                previous = closing
        return count

    def split(self, sep=None, maxsplit=-1):
        if sep is None:
            return [CppSource(part) for part in super().split(sep, maxsplit)]
        parts, previous = [], 0
        for opening, closing in self._matches(sep):
            if opening < previous:
                continue
            if maxsplit >= 0 and len(parts) >= maxsplit:
                break
            parts.append(CppSource(str(self)[previous:opening]))
            previous = closing
        parts.append(CppSource(str(self)[previous:]))
        return parts

    def __getitem__(self, key):
        return CppSource(super().__getitem__(key))

    def __add__(self, other):
        return CppSource(super().__add__(other))

    def __radd__(self, other):
        return CppSource(other + str(self))
