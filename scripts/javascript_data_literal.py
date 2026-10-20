"""Parse JSON data plus JavaScript identifier keys; never evaluate code."""
import json
import math
import re


class JavaScriptDataLiteral:
    @staticmethod
    def parse(text, start=0, *, max_nesting):
        if type(max_nesting) is not int or max_nesting < 1:
            raise ValueError('JAVASCRIPT-DATA-NESTING-BUDGET')
        if type(start) is not int or not 0 <= start <= len(text):
            raise ValueError('JAVASCRIPT-DATA-START')
        decoder = json.JSONDecoder()
        cursor = start

        def whitespace():
            nonlocal cursor
            while cursor < len(text) and text[cursor].isspace():
                cursor += 1

        def value(depth):
            nonlocal cursor
            whitespace()
            if cursor >= len(text):
                raise ValueError('JAVASCRIPT-DATA-TRUNCATED')
            token = text[cursor]
            if token not in '{[':
                if token not in '"-0123456789tfn':
                    raise ValueError('JAVASCRIPT-DATA-EXECUTABLE-TOKEN')
                result, cursor = decoder.raw_decode(text, cursor)
                if isinstance(result, float) and not math.isfinite(result):
                    raise ValueError('JAVASCRIPT-DATA-NONFINITE')
                return result
            if depth >= max_nesting:
                raise ValueError('JAVASCRIPT-DATA-NESTING-BUDGET')
            cursor += 1
            result = {} if token == '{' else []
            closing = '}' if token == '{' else ']'
            whitespace()
            if cursor < len(text) and text[cursor] == closing:
                cursor += 1
                return result
            while True:
                if token == '{':
                    whitespace()
                    if cursor < len(text) and text[cursor] == '"':
                        key, cursor = decoder.raw_decode(text, cursor)
                    else:
                        match = re.match(r'[A-Za-z_$][A-Za-z0-9_$]*', text[cursor:])
                        if not match:
                            raise ValueError('JAVASCRIPT-DATA-OBJECT-KEY')
                        key = match[0]
                        cursor += len(key)
                    whitespace()
                    if cursor >= len(text) or text[cursor] != ':' or key in result:
                        raise ValueError('JAVASCRIPT-DATA-OBJECT-KEY')
                    cursor += 1
                    result[key] = value(depth + 1)
                else:
                    result.append(value(depth + 1))
                whitespace()
                if cursor >= len(text):
                    raise ValueError('JAVASCRIPT-DATA-TRUNCATED')
                separator = text[cursor]
                cursor += 1
                if separator == closing:
                    return result
                if separator != ',':
                    raise ValueError('JAVASCRIPT-DATA-SEPARATOR')
        return value(0), cursor
