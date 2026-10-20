#include "scriptvm/compile/lexer.h"

#include <cctype>
#include <cstring>
#include <limits>
#include <unordered_map>

namespace arp::scriptvm {

// --- token names ---

const char* tokName(Tok t)
{
    switch (t) {
    case Tok::Eof:
        return "EOF";
    case Tok::Newline:
        return "Newline";
    case Tok::Indent:
        return "Indent";
    case Tok::Dedent:
        return "Dedent";
    case Tok::Int:
        return "Int";
    case Tok::Int64:
        return "Int64";
    case Tok::Float:
        return "Float";
    case Tok::String:
        return "String";
    case Tok::Ident:
        return "Ident";
    case Tok::KwVar:
        return "var";
    case Tok::KwConst:
        return "const";
    case Tok::KwDef:
        return "def";
    case Tok::KwClass:
        return "class";
    case Tok::KwIf:
        return "if";
    case Tok::KwElif:
        return "elif";
    case Tok::KwElse:
        return "else";
    case Tok::KwFor:
        return "for";
    case Tok::KwWhile:
        return "while";
    case Tok::KwReturn:
        return "return";
    case Tok::KwBreak:
        return "break";
    case Tok::KwContinue:
        return "continue";
    case Tok::KwIn:
        return "in";
    case Tok::KwNil:
        return "nil";
    case Tok::KwTrue:
        return "true";
    case Tok::KwFalse:
        return "false";
    case Tok::KwPass:
        return "pass";
    case Tok::KwAnd:
        return "and";
    case Tok::KwOr:
        return "or";
    case Tok::KwNot:
        return "not";
    case Tok::KwImport:
        return "import";
    case Tok::KwFrom:
        return "from";
    case Tok::KwAs:
        return "as";
    case Tok::KwAwait:
        return "await";
    case Tok::KwLambda:
        return "lambda";
    case Tok::KwTry:
        return "try";
    case Tok::KwExcept:
        return "except";
    case Tok::KwRaise:
        return "raise";
    case Tok::KwYield:
        return "yield";
    case Tok::LParen:
        return "(";
    case Tok::RParen:
        return ")";
    case Tok::LBracket:
        return "[";
    case Tok::RBracket:
        return "]";
    case Tok::LBrace:
        return "{";
    case Tok::RBrace:
        return "}";
    case Tok::Colon:
        return ":";
    case Tok::Comma:
        return ",";
    case Tok::Dot:
        return ".";
    case Tok::Arrow:
        return "->";
    case Tok::Assign:
        return "=";
    case Tok::Eq:
        return "==";
    case Tok::Ne:
        return "!=";
    case Tok::Lt:
        return "<";
    case Tok::Le:
        return "<=";
    case Tok::Gt:
        return ">";
    case Tok::Ge:
        return ">=";
    case Tok::Plus:
        return "+";
    case Tok::Minus:
        return "-";
    case Tok::Star:
        return "*";
    case Tok::Slash:
        return "/";
    case Tok::Percent:
        return "%";
    case Tok::StarStar:
        return "**";
    case Tok::Amp:
        return "&";
    case Tok::Pipe:
        return "|";
    case Tok::Caret:
        return "^";
    case Tok::Tilde:
        return "~";
    case Tok::Shl:
        return "<<";
    case Tok::Shr:
        return ">>";
    case Tok::FloorDiv:
        return "//";
    case Tok::PlusAssign:
        return "+=";
    case Tok::MinusAssign:
        return "-=";
    case Tok::StarAssign:
        return "*=";
    case Tok::SlashAssign:
        return "/=";
    case Tok::PercentAssign:
        return "%=";
    case Tok::Question:
        return "?";
    case Tok::TokCount:
        return "?TokCount";
    }
    return "?";
}

namespace {

// Keyword lookup table.
const std::unordered_map<std::string_view, Tok>& keywordTable()
{
    static const std::unordered_map<std::string_view, Tok> table = {
        {"var", Tok::KwVar},
        {"const", Tok::KwConst},
        {"def", Tok::KwDef},
        {"class", Tok::KwClass},
        {"if", Tok::KwIf},
        {"elif", Tok::KwElif},
        {"else", Tok::KwElse},
        {"for", Tok::KwFor},
        {"while", Tok::KwWhile},
        {"return", Tok::KwReturn},
        {"break", Tok::KwBreak},
        {"continue", Tok::KwContinue},
        {"in", Tok::KwIn},
        {"nil", Tok::KwNil},
        {"true", Tok::KwTrue},
        {"false", Tok::KwFalse},
        {"pass", Tok::KwPass},
        {"and", Tok::KwAnd},
        {"or", Tok::KwOr},
        {"not", Tok::KwNot},
        {"import", Tok::KwImport},
        {"from", Tok::KwFrom},
        {"as", Tok::KwAs},
        {"await", Tok::KwAwait},
        {"lambda", Tok::KwLambda},
        {"try", Tok::KwTry},
        {"except", Tok::KwExcept},
        {"raise", Tok::KwRaise},
        {"yield", Tok::KwYield},
    };
    return table;
}

bool isIdentStart(char c)
{
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}

bool isIdentCont(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

bool isDigit(char c)
{
    return c >= '0' && c <= '9';
}

bool isHexDigit(char c)
{
    return isDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

bool isOctDigit(char c)
{
    return c >= '0' && c <= '7';
}

bool isBinDigit(char c)
{
    return c == '0' || c == '1';
}

// Lexer state.
struct Lexer {
    core::pmr::String source;
    core::pmr::String filename;
    std::size_t pos = 0;
    int line = 1;
    int col = 1;

    // Indent stack: stack of indentation column values. Always starts with 0
    // (the base indentation = column 0).
    core::pmr::Vector<int> indentStack;

    // Bracket nesting depth: (), [], {}. When > 0, newlines are ignored and
    // indentation is not significant.
    int bracketDepth = 0;

    // Whether the current position is at the start of a logical line
    // (i.e. we need to check indentation).
    bool atLineStart = true;

    // Whether we've seen any non-blank content on the current line.
    // Used to decide whether to emit Newline at end of line.
    bool lineHasContent = false;

    LexResult result;

    Lexer(core::pmr::String src, core::pmr::String fn)
        : source(std::move(src)), filename(std::move(fn))
    {
        indentStack.push_back(0);
    }

    char peek(std::size_t offset = 0) const
    {
        if (pos + offset >= source.size())
            return '\0';
        return source[pos + offset];
    }

    void advance()
    {
        if (pos < source.size()) {
            if (source[pos] == '\n') {
                ++line;
                col = 1;
            } else {
                ++col;
            }
            ++pos;
        }
    }

    char current() const
    {
        if (pos >= source.size())
            return '\0';
        return source[pos];
    }

    SourcePos makePos() const { return SourcePos{filename, line, col}; }

    void emitError(ScriptDiagnosticCode code, core::pmr::String msg, int l, int c)
    {
        if (result.ok()) {
            result.error = ScriptDiagnostic(code, std::move(msg), filename, l, c);
        }
    }

    // Emit a token.
    void emit(Tok kind, core::pmr::String text, SourcePos pos)
    {
        result.tokens.emplace_back(kind, std::move(text), std::move(pos));
    }

    void emitSimple(Tok kind)
    {
        emit(kind, core::pmr::String(&core::defaultCpuMemoryResource()), makePos());
    }
};

// Process the indentation at the start of a logical line.
// Returns the indentation column (number of leading spaces, treating tab as
// expansion to next multiple of 8 -- but we reject tabs for simplicity, like
// Python's mixed-indent error).
// Emits INDENT/DEDENT tokens as needed.
void processIndentation(Lexer& lx)
{
    // Count leading whitespace.
    int indent = 0;
    while (lx.pos < lx.source.size()) {
        char c = lx.source[lx.pos];
        if (c == ' ') {
            ++indent;
            lx.advance();
        } else if (c == '\t') {
            // Align to next multiple of 8.
            indent = (indent / 8 + 1) * 8;
            lx.advance();
        } else {
            break;
        }
    }

    // If the line is blank or comment-only, don't emit INDENT/DEDENT.
    // Check what's after the whitespace.
    char after = lx.peek();
    if (after == '\0' || after == '\n' || after == '\r' || after == '#') {
        // Blank or comment line: skip without emitting indent tokens.
        return;
    }

    int top = lx.indentStack.back();

    if (indent > top) {
        lx.indentStack.push_back(indent);
        lx.emitSimple(Tok::Indent);
    } else if (indent < top) {
        // Emit DEDENT until we match or run out.
        while (!lx.indentStack.empty() && lx.indentStack.back() > indent) {
            lx.indentStack.pop_back();
            lx.emitSimple(Tok::Dedent);
        }
        // If we didn't find a matching indent, it's an error.
        if (lx.indentStack.empty() || lx.indentStack.back() != indent) {
            lx.emitError(ScriptDiagnosticCode::UnexpectedDedent,
                "unindent does not match any outer level",
                lx.line,
                lx.col);
        }
    }
    // If indent == top, nothing to emit.
}

// Lex a string literal starting at the opening quote.
// The opening quote char is at lx.current().
void lexString(Lexer& lx)
{
    char quote = lx.current();
    SourcePos startPos = lx.makePos();
    lx.advance(); // consume opening quote

    core::pmr::String value{&core::defaultCpuMemoryResource()};
    bool terminated = false;

    while (lx.pos < lx.source.size()) {
        char c = lx.current();
        if (c == quote) {
            lx.advance();
            terminated = true;
            break;
        }
        if (c == '\n' || c == '\0') {
            break; // unterminated
        }
        if (c == '\\') {
            lx.advance();
            char esc = lx.current();
            switch (esc) {
            case 'n':
                value.push_back('\n');
                break;
            case 't':
                value.push_back('\t');
                break;
            case 'r':
                value.push_back('\r');
                break;
            case '\\':
                value.push_back('\\');
                break;
            case '\'':
                value.push_back('\'');
                break;
            case '"':
                value.push_back('"');
                break;
            case '0':
                value.push_back('\0');
                break;
            case 'x': {
                // \xHH
                lx.advance();
                char h1 = lx.current();
                lx.advance();
                char h2 = lx.current();
                if (isHexDigit(h1) && isHexDigit(h2)) {
                    char hex[3] = {h1, h2, '\0'};
                    value.push_back(static_cast<char>(std::strtol(hex, nullptr, 16)));
                } else {
                    lx.emitError(ScriptDiagnosticCode::InvalidEscape,
                        "invalid \\x escape sequence",
                        lx.line,
                        lx.col);
                }
                break;
            }
            default:
                // Unknown escape: keep the char literally (Python behavior).
                value.push_back(esc);
                break;
            }
            lx.advance();
        } else {
            value.push_back(c);
            lx.advance();
        }
    }

    if (!terminated) {
        lx.emitError(ScriptDiagnosticCode::UnterminatedString,
            "unterminated string literal",
            startPos.line,
            startPos.column);
    }

    Token tok(Tok::String, std::move(value), startPos);
    lx.result.tokens.push_back(std::move(tok));
}

// Lex a numeric literal starting at the current digit.
void lexNumber(Lexer& lx)
{
    SourcePos startPos = lx.makePos();
    core::pmr::String text{&core::defaultCpuMemoryResource()};

    bool isFloat = false;
    bool isHex = false;
    bool isOct = false;
    bool isBin = false;
    bool isInt64 = false;

    // Check for hex/octal/binary prefix.
    if (lx.peek() == '0') {
        char next = lx.peek(1);
        if (next == 'x' || next == 'X') {
            isHex = true;
            text.push_back(lx.current());
            lx.advance(); // 0
            text.push_back(lx.current());
            lx.advance(); // x
        } else if (next == 'o' || next == 'O') {
            isOct = true;
            text.push_back(lx.current());
            lx.advance();
            text.push_back(lx.current());
            lx.advance();
        } else if (next == 'b' || next == 'B') {
            isBin = true;
            text.push_back(lx.current());
            lx.advance();
            text.push_back(lx.current());
            lx.advance();
        }
    }

    if (isHex) {
        while (isHexDigit(lx.current())) {
            text.push_back(lx.current());
            lx.advance();
        }
    } else if (isOct) {
        while (isOctDigit(lx.current())) {
            text.push_back(lx.current());
            lx.advance();
        }
    } else if (isBin) {
        while (isBinDigit(lx.current())) {
            text.push_back(lx.current());
            lx.advance();
        }
    } else {
        // Decimal: integer or float.
        while (isDigit(lx.current())) {
            text.push_back(lx.current());
            lx.advance();
        }
        // Fractional part.
        if (lx.current() == '.') {
            isFloat = true;
            text.push_back('.');
            lx.advance();
            while (isDigit(lx.current())) {
                text.push_back(lx.current());
                lx.advance();
            }
        }
        // Exponent: e[+-]?Digits
        if (lx.current() == 'e' || lx.current() == 'E') {
            isFloat = true;
            text.push_back(lx.current());
            lx.advance();
            if (lx.current() == '+' || lx.current() == '-') {
                text.push_back(lx.current());
                lx.advance();
            }
            while (isDigit(lx.current())) {
                text.push_back(lx.current());
                lx.advance();
            }
        }
    }

    // Check for int64 suffix 'L'.
    if (!isFloat && (lx.current() == 'L' || lx.current() == 'l')) {
        isInt64 = true;
        lx.advance();
    }

    Token tok;
    tok.pos = startPos;
    tok.text = text;

    if (isFloat) {
        tok.kind = Tok::Float;
        tok.f64 = std::strtod(text.c_str(), nullptr);
    } else if (isInt64) {
        tok.kind = Tok::Int64;
        int base = isHex ? 16 : (isOct ? 8 : (isBin ? 2 : 10));
        // Skip the prefix for parsing.
        const char* numStart = text.c_str();
        if (isHex || isOct || isBin)
            numStart += 2;
        tok.i64 = std::strtoll(numStart, nullptr, base);
    } else {
        tok.kind = Tok::Int;
        int base = isHex ? 16 : (isOct ? 8 : (isBin ? 2 : 10));
        const char* numStart = text.c_str();
        if (isHex || isOct || isBin)
            numStart += 2;
        long long val = std::strtoll(numStart, nullptr, base);
        tok.i64 = val;
        if (val > std::numeric_limits<int>::max() || val < std::numeric_limits<int>::min()) {
            tok.kind = Tok::Int64;
        }
    }

    lx.result.tokens.push_back(std::move(tok));
}

// Lex an identifier or keyword.
void lexIdent(Lexer& lx)
{
    SourcePos startPos = lx.makePos();
    core::pmr::String text{&core::defaultCpuMemoryResource()};

    while (isIdentCont(lx.current())) {
        text.push_back(lx.current());
        lx.advance();
    }

    // Check if it's a keyword.
    Tok kind = Tok::Ident;
    auto& kw = keywordTable();
    auto it = kw.find(std::string_view(text.data(), text.size()));
    if (it != kw.end()) {
        kind = it->second;
    }

    Token tok(kind, std::move(text), startPos);
    lx.result.tokens.push_back(std::move(tok));
}

// Lex a comment (# to end of line). Does not emit a token.
void lexComment(Lexer& lx)
{
    while (lx.pos < lx.source.size() && lx.current() != '\n') {
        lx.advance();
    }
}

// Try to match a multi-char operator. Returns the Tok and advances if matched.
// Returns Tok::Eof if no operator matches (caller should handle).
bool tryOperator(Lexer& lx)
{
    SourcePos pos = lx.makePos();
    char c = lx.current();
    char next = lx.peek(1);
    char next2 = lx.peek(2);

    // 3-char operators
    if (c == '*' && next == '*') {
        // ** (but not **= which is rare)
        lx.advance();
        lx.advance();
        lx.emit(Tok::StarStar, "**", pos);
        return true;
    }

    // 2-char operators
    auto emit2 = [&](Tok t, const char* s) {
        lx.advance();
        lx.advance();
        lx.emit(t, core::pmr::String(s, &core::defaultCpuMemoryResource()), pos);
    };

    switch (c) {
    case '-':
        if (next == '>') {
            emit2(Tok::Arrow, "->");
            return true;
        }
        if (next == '=') {
            emit2(Tok::MinusAssign, "-=");
            return true;
        }
        break;
    case '=':
        if (next == '=') {
            emit2(Tok::Eq, "==");
            return true;
        }
        break;
    case '!':
        if (next == '=') {
            emit2(Tok::Ne, "!=");
            return true;
        }
        break;
    case '<':
        if (next == '=') {
            emit2(Tok::Le, "<=");
            return true;
        }
        if (next == '<') {
            if (next2 == '=') {
                lx.advance();
                lx.advance();
                lx.advance();
                lx.emit(Tok::Shl, "<<", pos);
                return true;
            } // simplified: no <<=
            emit2(Tok::Shl, "<<");
            return true;
        }
        break;
    case '>':
        if (next == '=') {
            emit2(Tok::Ge, ">=");
            return true;
        }
        if (next == '>') {
            emit2(Tok::Shr, ">>");
            return true;
        }
        break;
    case '+':
        if (next == '=') {
            emit2(Tok::PlusAssign, "+=");
            return true;
        }
        break;
    case '*':
        if (next == '=') {
            emit2(Tok::StarAssign, "*=");
            return true;
        }
        break;
    case '/':
        if (next == '/') {
            emit2(Tok::FloorDiv, "//");
            return true;
        }
        if (next == '=') {
            emit2(Tok::SlashAssign, "/=");
            return true;
        }
        break;
    case '%':
        if (next == '=') {
            emit2(Tok::PercentAssign, "%=");
            return true;
        }
        break;
    }

    // 1-char operators
    auto emit1 = [&](Tok t, const char* s) {
        lx.advance();
        lx.emit(t, core::pmr::String(s, &core::defaultCpuMemoryResource()), pos);
    };

    switch (c) {
    case '(':
        lx.bracketDepth++;
        emit1(Tok::LParen, "(");
        return true;
    case ')':
        lx.bracketDepth--;
        emit1(Tok::RParen, ")");
        return true;
    case '[':
        lx.bracketDepth++;
        emit1(Tok::LBracket, "[");
        return true;
    case ']':
        lx.bracketDepth--;
        emit1(Tok::RBracket, "]");
        return true;
    case '{':
        lx.bracketDepth++;
        emit1(Tok::LBrace, "{");
        return true;
    case '}':
        lx.bracketDepth--;
        emit1(Tok::RBrace, "}");
        return true;
    case ':':
        emit1(Tok::Colon, ":");
        return true;
    case ',':
        emit1(Tok::Comma, ",");
        return true;
    case '.':
        emit1(Tok::Dot, ".");
        return true;
    case '=':
        emit1(Tok::Assign, "=");
        return true;
    case '<':
        emit1(Tok::Lt, "<");
        return true;
    case '>':
        emit1(Tok::Gt, ">");
        return true;
    case '+':
        emit1(Tok::Plus, "+");
        return true;
    case '-':
        emit1(Tok::Minus, "-");
        return true;
    case '*':
        emit1(Tok::Star, "*");
        return true;
    case '/':
        emit1(Tok::Slash, "/");
        return true;
    case '%':
        emit1(Tok::Percent, "%");
        return true;
    case '&':
        emit1(Tok::Amp, "&");
        return true;
    case '|':
        emit1(Tok::Pipe, "|");
        return true;
    case '^':
        emit1(Tok::Caret, "^");
        return true;
    case '~':
        emit1(Tok::Tilde, "~");
        return true;
    case '?':
        emit1(Tok::Question, "?");
        return true;
    }

    return false;
}

} // namespace

LexResult lex(core::pmr::String source, core::pmr::String filename)
{
    Lexer lx(std::move(source), std::move(filename));

    while (lx.pos < lx.source.size() && lx.result.ok()) {
        // At the start of a logical line (and not inside brackets):
        if (lx.atLineStart && lx.bracketDepth == 0) {
            processIndentation(lx);
            lx.atLineStart = false;
            if (!lx.result.ok())
                break;
            // After processing indentation, we might be at a blank/comment line.
            // In that case, the line was skipped -- fall through to the newline
            // handling below.
            char c = lx.current();
            if (c == '\0')
                break;
            if (c == '\r') {
                // Consume the carriage return of a CRLF blank line here;
                // the following LF is handled by the normal newline path.
                lx.advance();
                continue;
            }
            if (c == '\n') {
                // Blank line: consume the newline, stay at line start.
                lx.advance();
                lx.atLineStart = true;
                continue;
            }
            if (c == '#') {
                lexComment(lx);
                // After comment, expect newline or EOF.
                continue;
            }
            lx.lineHasContent = true;
        }

        char c = lx.current();

        if (c == '\0')
            break;

        if (c == '\r') {
            lx.advance();
            continue;
        }

        if (c == '\n') {
            lx.advance();
            if (lx.bracketDepth == 0) {
                if (lx.lineHasContent) {
                    lx.emitSimple(Tok::Newline);
                }
                lx.lineHasContent = false;
                lx.atLineStart = true;
            }
            // Inside brackets: newlines are ignored.
            continue;
        }

        if (c == ' ' || c == '\t' || c == '\r') {
            lx.advance();
            continue;
        }

        if (c == '#') {
            lexComment(lx);
            continue;
        }

        if (c == '"' || c == '\'') {
            lexString(lx);
            lx.lineHasContent = true;
            continue;
        }

        if (isDigit(c)) {
            lexNumber(lx);
            lx.lineHasContent = true;
            continue;
        }

        if (isIdentStart(c)) {
            lexIdent(lx);
            lx.lineHasContent = true;
            continue;
        }

        // Try operators.
        SourcePos pos = lx.makePos();
        if (tryOperator(lx)) {
            lx.lineHasContent = true;
            continue;
        }

        // Unknown character.
        lx.emitError(ScriptDiagnosticCode::UnexpectedChar,
            core::pmr::String("unexpected character: '", &core::defaultCpuMemoryResource())
                + core::pmr::String(1, c) + "'",
            pos.line,
            pos.column);
        break;
    }

    // End of input: emit a final Newline if the last line had content.
    if (lx.lineHasContent && lx.bracketDepth == 0) {
        lx.emitSimple(Tok::Newline);
        lx.lineHasContent = false;
    }

    // Emit DEDENT tokens for any remaining open indentation levels.
    while (lx.indentStack.size() > 1) {
        lx.indentStack.pop_back();
        lx.emitSimple(Tok::Dedent);
    }

    // Emit EOF.
    lx.emitSimple(Tok::Eof);

    return std::move(lx.result);
}

} // namespace arp::scriptvm
