#include "scriptvm/compile/parser.h"

#include <cctype>
#include <cstdio>

namespace arp::scriptvm {

namespace {

// Operator precedence levels (higher = binds tighter).
// Mirrors Python's precedence (simplified).
struct OpInfo {
    int prec;
    bool rightAssoc;
};

OpInfo binaryPrec(const core::pmr::String& op)
{
    // or
    if (op == "or")
        return {1, false};
    // and
    if (op == "and")
        return {2, false};
    // not (unary, handled separately)
    // comparison
    if (op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=")
        return {3, false};
    // bitwise or
    if (op == "|")
        return {4, false};
    // bitwise xor
    if (op == "^")
        return {5, false};
    // bitwise and
    if (op == "&")
        return {6, false};
    // shift
    if (op == "<<" || op == ">>")
        return {7, false};
    // additive
    if (op == "+" || op == "-")
        return {8, false};
    // multiplicative
    if (op == "*" || op == "/" || op == "//" || op == "%")
        return {9, false};
    // power (right-assoc)
    if (op == "**")
        return {10, true};
    return {0, false};
}

// Assignment operators are handled at a lower level (statement, not expression).
bool isAssignOp(Tok t)
{
    return t == Tok::Assign || t == Tok::PlusAssign || t == Tok::MinusAssign || t == Tok::StarAssign
        || t == Tok::SlashAssign || t == Tok::PercentAssign;
}

core::pmr::String assignOpText(Tok t)
{
    switch (t) {
    case Tok::Assign:
        return "=";
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
    default:
        return "=";
    }
}

// Convert a token to its operator text (for binary/unary operators).
core::pmr::String tokOpText(Tok t)
{
    switch (t) {
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
    case Tok::FloorDiv:
        return "//";
    case Tok::Amp:
        return "&";
    case Tok::Pipe:
        return "|";
    case Tok::Caret:
        return "^";
    case Tok::Shl:
        return "<<";
    case Tok::Shr:
        return ">>";
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
    case Tok::KwAnd:
        return "and";
    case Tok::KwOr:
        return "or";
    default:
        return "";
    }
}

bool isBinaryOp(Tok t)
{
    switch (t) {
    case Tok::Plus:
    case Tok::Minus:
    case Tok::Star:
    case Tok::Slash:
    case Tok::Percent:
    case Tok::StarStar:
    case Tok::FloorDiv:
    case Tok::Amp:
    case Tok::Pipe:
    case Tok::Caret:
    case Tok::Shl:
    case Tok::Shr:
    case Tok::Eq:
    case Tok::Ne:
    case Tok::Lt:
    case Tok::Le:
    case Tok::Gt:
    case Tok::Ge:
    case Tok::KwAnd:
    case Tok::KwOr:
        return true;
    default:
        return false;
    }
}

// Check if a keyword can be used as a type name.
bool isBasicTypeName(const core::pmr::String& name)
{
    return name == "int" || name == "int64" || name == "float" || name == "string"
        || name == "bytes" || name == "bool" || name == "void" || name == "any";
}

// The parser state.
struct Parser {
    const core::pmr::Vector<Token>* tokens = nullptr;
    std::size_t pos = 0;
    ScriptDiagnostic error;

    explicit Parser(const core::pmr::Vector<Token>& toks) : tokens(&toks) {}

    const Token& peek(std::size_t offset = 0) const
    {
        std::size_t idx = pos + offset;
        if (idx >= tokens->size())
            return tokens->back(); // Eof
        return (*tokens)[idx];
    }

    const Token& current() const { return peek(0); }

    bool atEof() const { return current().kind == Tok::Eof; }

    // Advance and return the previous token.
    const Token& advance()
    {
        const Token& t = current();
        if (pos < tokens->size() - 1)
            ++pos;
        return t;
    }

    // Check if the current token matches.
    bool check(Tok kind) const { return current().kind == kind; }

    // If current matches, consume and return true.
    bool match(Tok kind)
    {
        if (check(kind)) {
            advance();
            return true;
        }
        return false;
    }

    // Expect a specific token; on mismatch, set error.
    bool expect(Tok kind, ScriptDiagnosticCode errCode, const char* msg)
    {
        if (!check(kind)) {
            setError(errCode, msg);
            return false;
        }
        advance();
        return true;
    }

    void setError(ScriptDiagnosticCode code, core::pmr::String msg)
    {
        if (error.code == ScriptDiagnosticCode::None) {
            error = ScriptDiagnostic(code,
                std::move(msg),
                core::pmr::String(current().pos.file, &core::defaultCpuMemoryResource()),
                current().pos.line,
                current().pos.column);
        }
    }

    void setError(ScriptDiagnosticCode code, const char* msg)
    {
        setError(code, core::pmr::String(msg, &core::defaultCpuMemoryResource()));
    }

    bool ok() const { return error.code == ScriptDiagnosticCode::None; }

    // Skip past the current statement (for error recovery).
    void skipToNewline()
    {
        while (!atEof() && current().kind != Tok::Newline && current().kind != Tok::Dedent)
            advance();
        if (check(Tok::Newline))
            advance();
    }

    // --- Block parsing ---
    // A block is: NEWLINE INDENT (stmt)+ DEDENT
    // Returns a list of statements.
    bool parseBlock(core::pmr::Vector<StmtPtr>& out)
    {
        if (!expect(Tok::Newline,
                ScriptDiagnosticCode::UnexpectedToken,
                "expected newline after ':'")) {
            return false;
        }
        if (!expect(
                Tok::Indent, ScriptDiagnosticCode::UnexpectedIndent, "expected indented block")) {
            return false;
        }
        while (!atEof() && check(Tok::Dedent) == false && ok()) {
            auto stmt = parseStmt();
            if (stmt)
                out.push_back(stmt);
            else
                break;
        }
        if (ok())
            match(Tok::Dedent);
        return ok();
    }

    // --- Type parsing ---
    TypePtr parseType()
    {
        SourcePos p = current().pos;
        TypePtr t;

        if (match(Tok::LParen)) {
            // Function type: (T1, T2) -> R
            t = makeType(TypeKind::Func);
            if (!check(Tok::RParen)) {
                do {
                    auto pt = parseType();
                    t->params.push_back(pt);
                } while (match(Tok::Comma) && ok());
            }
            if (!expect(Tok::RParen,
                    ScriptDiagnosticCode::MissingCloseParen,
                    "expected ')' in function type")) {
                return nullptr;
            }
            if (!expect(Tok::Arrow,
                    ScriptDiagnosticCode::UnexpectedToken,
                    "expected '->' in function type")) {
                return nullptr;
            }
            t->result = parseType();
            t->pos = p;
            return t;
        }

        if (match(Tok::LBracket)) {
            // List type: [T]
            t = makeType(TypeKind::List);
            t->elem = parseType();
            if (!expect(Tok::RBracket,
                    ScriptDiagnosticCode::MissingCloseBracket,
                    "expected ']' in list type")) {
                return nullptr;
            }
            t->pos = p;
            return t;
        }

        if (match(Tok::LBrace)) {
            // Dict type: {K: V}
            t = makeType(TypeKind::Dict);
            t->key = parseType();
            if (!expect(
                    Tok::Colon, ScriptDiagnosticCode::MissingColon, "expected ':' in dict type")) {
                return nullptr;
            }
            t->value = parseType();
            if (!expect(Tok::RBrace,
                    ScriptDiagnosticCode::MissingCloseBrace,
                    "expected '}' in dict type")) {
                return nullptr;
            }
            t->pos = p;
            return t;
        }

        if (check(Tok::Ident)) {
            t = makeType(TypeKind::Basic);
            t->name = current().text;
            t->pos = p;
            advance();
            // Check for generic/class type: if not a basic type, mark as Class.
            if (!isBasicTypeName(t->name))
                t->kind = TypeKind::Class;
            while (match(Tok::Dot)) {
                if (!check(Tok::Ident)) {
                    setError(ScriptDiagnosticCode::UnexpectedToken, "expected type name after '.'");
                    return nullptr;
                }
                t->name.push_back('.');
                t->name += current().text;
                t->kind = TypeKind::Class;
                advance();
            }
        } else {
            setError(ScriptDiagnosticCode::UnexpectedToken, "expected type name");
            return nullptr;
        }

        // Optional: ?T
        if (match(Tok::Question)) {
            auto opt = makeType(TypeKind::Optional);
            opt->elem = t;
            opt->pos = p;
            return opt;
        }

        return t;
    }

    // --- Expression parsing (Pratt-style) ---

    ExprPtr parseExpr() { return parseBinary(0); }

    ExprPtr parseAsCast()
    {
        ExprPtr value = parseUnary();
        if (!ok() || !value)
            return nullptr;

        // `as` is consumed here only after an expression has been parsed.
        // Import aliases are handled by parseImport/parseFromImport and never
        // enter this expression path.
        while (match(Tok::KwAs)) {
            TypePtr target = parseType();
            if (!ok() || !target)
                return nullptr;
            auto cast = makeExpr(ExprKind::AsCast, value->pos);
            cast->left = value;
            cast->castType = target;
            value = cast;
        }
        return value;
    }

    ExprPtr parseBinary(int minPrec)
    {
        ExprPtr left = parseAsCast();
        if (!ok() || !left)
            return nullptr;

        while (ok()) {
            Tok t = current().kind;
            if (!isBinaryOp(t))
                break;

            core::pmr::String op = tokOpText(t);
            OpInfo info = binaryPrec(op);
            if (info.prec < minPrec)
                break;

            advance(); // consume operator

            // For right-assoc (e.g. **), next minPrec = info.prec
            int nextMinPrec = info.rightAssoc ? info.prec : info.prec + 1;
            ExprPtr right = parseBinary(nextMinPrec);
            if (!ok() || !right)
                return nullptr;

            auto bin = makeExpr(ExprKind::Binary, left->pos);
            bin->op = op;
            bin->left = left;
            bin->right = right;
            left = bin;
        }

        return left;
    }

    ExprPtr parseUnary()
    {
        // Unary: -x, +x, not x, ~x. Await binds like a unary operator so
        // `await load()` awaits the complete call expression.
        Tok t = current().kind;
        if (t == Tok::KwAwait) {
            SourcePos p = current().pos;
            advance();
            ExprPtr operand = parseUnary();
            if (!ok() || !operand)
                return nullptr;
            auto awaited = makeExpr(ExprKind::Await, p);
            awaited->left = operand;
            return awaited;
        }
        if (t == Tok::Minus || t == Tok::Plus || t == Tok::Tilde || t == Tok::KwNot) {
            SourcePos p = current().pos;
            advance();
            ExprPtr operand = parseUnary();
            if (!ok() || !operand)
                return nullptr;
            auto unary = makeExpr(ExprKind::Unary, p);
            unary->op = (t == Tok::KwNot) ? "not" : tokOpText(t);
            unary->left = operand;
            return unary;
        }
        return parsePostfix();
    }

    ExprPtr parseLambda()
    {
        SourcePos p = current().pos;
        advance(); // consume lambda

        auto lambda = makeExpr(ExprKind::Lambda, p);
        if (!expect(Tok::LParen,
                ScriptDiagnosticCode::MissingCloseParen,
                "expected '(' after lambda")) {
            return nullptr;
        }
        if (!check(Tok::RParen)) {
            do {
                if (!check(Tok::Ident)) {
                    setError(
                        ScriptDiagnosticCode::UnexpectedToken, "expected lambda parameter name");
                    return nullptr;
                }
                lambda->paramNames.push_back(current().text);
                advance();
                if (!expect(Tok::Colon,
                        ScriptDiagnosticCode::MissingTypeHint,
                        "expected ':' and type hint in lambda parameter")) {
                    return nullptr;
                }
                auto parameterType = parseType();
                if (!ok() || !parameterType)
                    return nullptr;
                lambda->paramTypes.push_back(parameterType);
            } while (match(Tok::Comma) && ok());
        }
        if (!expect(Tok::RParen,
                ScriptDiagnosticCode::MissingCloseParen,
                "expected ')' after lambda parameters")) {
            return nullptr;
        }
        if (!expect(Tok::Arrow,
                ScriptDiagnosticCode::UnexpectedToken,
                "expected '->' after lambda parameters")) {
            return nullptr;
        }
        lambda->retType = parseType();
        if (!ok() || !lambda->retType)
            return nullptr;
        if (!expect(Tok::Colon,
                ScriptDiagnosticCode::MissingColon,
                "expected ':' before lambda body")) {
            return nullptr;
        }
        lambda->left = parseExpr();
        return ok() ? lambda : nullptr;
    }

    ExprPtr parsePostfix()
    {
        ExprPtr expr = parsePrimary();
        if (!ok() || !expr)
            return nullptr;

        while (ok()) {
            if (check(Tok::LParen)) {
                // Function call
                advance();
                auto call = makeExpr(ExprKind::Call, expr->pos);
                call->left = expr;
                if (!check(Tok::RParen)) {
                    do {
                        auto arg = parseExpr();
                        if (!ok() || !arg)
                            return nullptr;
                        call->args.push_back(arg);
                    } while (match(Tok::Comma) && ok());
                }
                if (!expect(Tok::RParen,
                        ScriptDiagnosticCode::MissingCloseParen,
                        "expected ')' after arguments")) {
                    return nullptr;
                }
                expr = call;
            } else if (check(Tok::LBracket)) {
                // Index access
                advance();
                auto idx = makeExpr(ExprKind::Index, expr->pos);
                idx->left = expr;
                idx->right = parseExpr();
                if (!ok() || !idx->right)
                    return nullptr;
                if (!expect(Tok::RBracket,
                        ScriptDiagnosticCode::MissingCloseBracket,
                        "expected ']' after index")) {
                    return nullptr;
                }
                expr = idx;
            } else if (check(Tok::Dot)) {
                // Field access
                advance();
                if (!check(Tok::Ident)) {
                    setError(
                        ScriptDiagnosticCode::UnexpectedToken, "expected field name after '.'");
                    return nullptr;
                }
                auto field = makeExpr(ExprKind::Field, expr->pos);
                field->left = expr;
                field->name = current().text;
                advance();
                expr = field;
            } else {
                break;
            }
        }

        return expr;
    }

    ExprPtr parsePrimary()
    {
        const Token& tok = current();

        switch (tok.kind) {
        case Tok::Int: {
            auto e = makeExpr(ExprKind::IntLit, tok.pos);
            e->intVal = tok.i64;
            advance();
            return e;
        }
        case Tok::Int64: {
            auto e = makeExpr(ExprKind::Int64Lit, tok.pos);
            e->intVal = tok.i64;
            advance();
            return e;
        }
        case Tok::Float: {
            auto e = makeExpr(ExprKind::FloatLit, tok.pos);
            e->floatVal = tok.f64;
            advance();
            return e;
        }
        case Tok::String: {
            auto e = makeExpr(ExprKind::StringLit, tok.pos);
            e->strVal = tok.text;
            advance();
            return e;
        }
        case Tok::KwTrue: {
            auto e = makeExpr(ExprKind::BoolLit, tok.pos);
            e->boolVal = true;
            advance();
            return e;
        }
        case Tok::KwFalse: {
            auto e = makeExpr(ExprKind::BoolLit, tok.pos);
            e->boolVal = false;
            advance();
            return e;
        }
        case Tok::KwNil: {
            auto e = makeExpr(ExprKind::NilLit, tok.pos);
            advance();
            return e;
        }
        case Tok::KwLambda:
            return parseLambda();
        case Tok::Ident: {
            auto e = makeExpr(ExprKind::Ident, tok.pos);
            e->name = tok.text;
            advance();
            return e;
        }
        case Tok::LParen: {
            // Parenthesized expression
            advance();
            ExprPtr inner = parseExpr();
            if (!ok() || !inner)
                return nullptr;
            if (!expect(Tok::RParen,
                    ScriptDiagnosticCode::MissingCloseParen,
                    "expected ')' after expression")) {
                return nullptr;
            }
            return inner;
        }
        case Tok::LBracket: {
            // List literal: [e1, e2, ...]
            advance();
            auto e = makeExpr(ExprKind::ListLit, tok.pos);
            if (!check(Tok::RBracket)) {
                do {
                    auto elem = parseExpr();
                    if (!ok() || !elem)
                        return nullptr;
                    e->args.push_back(elem);
                } while (match(Tok::Comma) && ok());
            }
            if (!expect(Tok::RBracket,
                    ScriptDiagnosticCode::MissingCloseBracket,
                    "expected ']' after list elements")) {
                return nullptr;
            }
            return e;
        }
        case Tok::LBrace: {
            // Dict literal: {k1: v1, k2: v2, ...}
            advance();
            auto e = makeExpr(ExprKind::DictLit, tok.pos);
            if (!check(Tok::RBrace)) {
                do {
                    auto key = parseExpr();
                    if (!ok() || !key)
                        return nullptr;
                    if (!expect(Tok::Colon,
                            ScriptDiagnosticCode::MissingColon,
                            "expected ':' in dict literal")) {
                        return nullptr;
                    }
                    auto val = parseExpr();
                    if (!ok() || !val)
                        return nullptr;
                    e->dictKeys.push_back(key);
                    e->dictValues.push_back(val);
                } while (match(Tok::Comma) && ok());
            }
            if (!expect(Tok::RBrace,
                    ScriptDiagnosticCode::MissingCloseBrace,
                    "expected '}' after dict entries")) {
                return nullptr;
            }
            return e;
        }
        default:
            setError(ScriptDiagnosticCode::UnexpectedToken, "unexpected token in expression");
            return nullptr;
        }
    }

    // --- Statement parsing ---

    // Parse: from <module> import <name> [as <alias>] (, <name> [as <alias>])*
    StmtPtr parseFromImport()
    {
        SourcePos pos = current().pos;
        advance(); // consume 'from'

        if (!check(Tok::Ident)) {
            setError(ScriptDiagnosticCode::UnexpectedToken, "expected module name after 'from'");
            return nullptr;
        }
        core::pmr::String moduleName = current().text;
        advance();

        if (!expect(Tok::KwImport,
                ScriptDiagnosticCode::UnexpectedToken,
                "expected 'import' after module name")) {
            return nullptr;
        }

        auto stmt = makeStmt(StmtKind::ImportDecl, pos);
        stmt->importModule = moduleName;
        stmt->importIsModuleImport = false;

        // Parse import list: name [as alias] (, name [as alias])*
        do {
            if (!check(Tok::Ident)) {
                setError(
                    ScriptDiagnosticCode::UnexpectedToken, "expected imported name after 'import'");
                return nullptr;
            }
            Stmt::ImportName item;
            item.name = current().text;
            advance();

            if (match(Tok::KwAs)) {
                if (!check(Tok::Ident)) {
                    setError(ScriptDiagnosticCode::UnexpectedToken, "expected alias after 'as'");
                    return nullptr;
                }
                item.alias = current().text;
                advance();
            }
            stmt->importNames.push_back(item);
        } while (match(Tok::Comma) && ok());

        if (!ok())
            return nullptr;

        match(Tok::Newline);
        return stmt;
    }

    // Parse: import <module> [as <alias>]
    StmtPtr parseImport()
    {
        SourcePos pos = current().pos;
        advance(); // consume 'import'

        if (!check(Tok::Ident)) {
            setError(ScriptDiagnosticCode::UnexpectedToken, "expected module name after 'import'");
            return nullptr;
        }

        auto stmt = makeStmt(StmtKind::ImportDecl, pos);
        stmt->importModule = current().text;
        stmt->importIsModuleImport = true;
        advance();

        if (match(Tok::KwAs)) {
            if (!check(Tok::Ident)) {
                setError(ScriptDiagnosticCode::UnexpectedToken, "expected alias after 'as'");
                return nullptr;
            }
            stmt->importAlias = current().text;
            advance();
        }

        match(Tok::Newline);
        return stmt;
    }

    StmtPtr parseStmt()
    {
        const Token& tok = current();

        switch (tok.kind) {
        case Tok::KwVar:
        case Tok::KwConst:
            return parseVarDecl();
        case Tok::KwDef:
            return parseFuncDecl();
        case Tok::KwClass:
            return parseClassDecl();
        case Tok::KwIf:
            return parseIfStmt();
        case Tok::KwWhile:
            return parseWhileStmt();
        case Tok::KwFor:
            return parseForStmt();
        case Tok::KwTry:
            return parseTryStmt();
        case Tok::KwRaise:
            return parseRaiseStmt();
        case Tok::KwYield:
            return parseYieldStmt();
        case Tok::KwReturn:
            return parseReturnStmt();
        case Tok::KwFrom:
            return parseFromImport();
        case Tok::KwImport:
            return parseImport();
        case Tok::KwBreak: {
            auto s = makeStmt(StmtKind::Break, tok.pos);
            advance();
            match(Tok::Newline);
            return s;
        }
        case Tok::KwContinue: {
            auto s = makeStmt(StmtKind::Continue, tok.pos);
            advance();
            match(Tok::Newline);
            return s;
        }
        case Tok::KwPass: {
            auto s = makeStmt(StmtKind::Pass, tok.pos);
            advance();
            match(Tok::Newline);
            return s;
        }
        default:
            return parseExprOrAssignStmt();
        }
    }

    // var/const name: Type = expr
    StmtPtr parseVarDecl()
    {
        const Token& kw = current();
        bool isConst = (kw.kind == Tok::KwConst);
        SourcePos p = kw.pos;
        advance(); // consume var/const

        if (!check(Tok::Ident)) {
            setError(ScriptDiagnosticCode::UnexpectedToken, "expected variable name");
            return nullptr;
        }
        core::pmr::String name = current().text;
        advance();

        if (!expect(Tok::Colon,
                ScriptDiagnosticCode::MissingTypeHint,
                "expected ':' and type hint in var/const declaration")) {
            return nullptr;
        }

        TypePtr type = parseType();
        if (!ok() || !type)
            return nullptr;

        ExprPtr init;
        if (match(Tok::Assign)) {
            init = parseExpr();
            if (!ok() || !init)
                return nullptr;
        }

        match(Tok::Newline);

        auto s = makeStmt(StmtKind::VarDecl, p);
        s->varName = name;
        s->varType = type;
        s->expr = init;
        s->isConst = isConst;
        return s;
    }

    // def name(param: Type, ...) -> RetType: body
    StmtPtr parseFuncDecl()
    {
        SourcePos p = current().pos;
        advance(); // consume def

        if (!check(Tok::Ident)) {
            setError(ScriptDiagnosticCode::UnexpectedToken, "expected function name");
            return nullptr;
        }
        core::pmr::String name = current().text;
        advance();

        if (!expect(Tok::LParen,
                ScriptDiagnosticCode::MissingCloseParen,
                "expected '(' after function name")) {
            return nullptr;
        }

        auto s = makeStmt(StmtKind::FuncDecl, p);
        s->funcName = name;

        if (!check(Tok::RParen)) {
            do {
                bool isVariadic = match(Tok::Star);
                if (isVariadic) {
                    if (s->variadicParam) {
                        setError(ScriptDiagnosticCode::UnexpectedToken,
                            "only one variadic parameter is allowed");
                        return nullptr;
                    }
                    s->variadicParam = true;
                    s->variadicParamIndex = static_cast<int>(s->paramNames.size());
                }
                if (!check(Tok::Ident)) {
                    setError(ScriptDiagnosticCode::UnexpectedToken, "expected parameter name");
                    return nullptr;
                }
                core::pmr::String pname = current().text;
                advance();

                // 'self' parameter in methods has no type hint.
                if (pname == "self" && (check(Tok::Comma) || check(Tok::RParen))) {
                    auto selfType = makeType(TypeKind::Class);
                    selfType->name = "self";
                    s->paramNames.push_back(pname);
                    s->paramTypes.push_back(selfType);
                    s->paramIsSelf.push_back(false);
                    continue;
                }

                if (!expect(Tok::Colon,
                        ScriptDiagnosticCode::MissingTypeHint,
                        "expected ':' and type hint in parameter")) {
                    return nullptr;
                }
                auto ptype = parseType();
                if (!ok() || !ptype)
                    return nullptr;

                s->paramNames.push_back(pname);
                s->paramTypes.push_back(ptype);
                s->paramIsSelf.push_back(false);
            } while (match(Tok::Comma) && ok());
        }

        if (!expect(Tok::RParen,
                ScriptDiagnosticCode::MissingCloseParen,
                "expected ')' after parameters")) {
            return nullptr;
        }

        // Return type
        if (match(Tok::Arrow)) {
            s->retType = parseType();
            if (!ok() || !s->retType)
                return nullptr;
        } else {
            // Default to void if no return type
            s->retType = makeType(TypeKind::Basic);
            s->retType->name = "void";
        }

        if (!expect(Tok::Colon,
                ScriptDiagnosticCode::MissingColon,
                "expected ':' after function signature")) {
            return nullptr;
        }

        if (!parseBlock(s->body))
            return nullptr;

        return s;
    }

    // class Name: body (fields + methods)
    StmtPtr parseClassDecl()
    {
        SourcePos p = current().pos;
        advance(); // consume class

        if (!check(Tok::Ident)) {
            setError(ScriptDiagnosticCode::UnexpectedToken, "expected class name");
            return nullptr;
        }
        core::pmr::String name = current().text;
        advance();
        core::pmr::String baseClassName;

        if (match(Tok::LParen)) {
            if (!check(Tok::Ident)) {
                setError(ScriptDiagnosticCode::UnexpectedToken, "expected base class name");
                return nullptr;
            }
            baseClassName = current().text;
            advance();
            if (!expect(Tok::RParen,
                    ScriptDiagnosticCode::UnexpectedToken,
                    "expected ')' after base class name")) {
                return nullptr;
            }
        }

        if (!expect(
                Tok::Colon, ScriptDiagnosticCode::MissingColon, "expected ':' after class name")) {
            return nullptr;
        }

        if (!expect(Tok::Newline,
                ScriptDiagnosticCode::UnexpectedToken,
                "expected newline after class declaration")) {
            return nullptr;
        }
        if (!expect(Tok::Indent,
                ScriptDiagnosticCode::UnexpectedIndent,
                "expected indented class body")) {
            return nullptr;
        }

        auto s = makeStmt(StmtKind::ClassDecl, p);
        s->className = name;
        s->baseClassName = baseClassName;

        while (!atEof() && !check(Tok::Dedent) && ok()) {
            const Token& tok = current();
            if (tok.kind == Tok::KwDef) {
                // Method
                auto method = parseFuncDecl();
                if (method) {
                    // Mark first param as self
                    if (!method->paramIsSelf.empty())
                        method->paramIsSelf[0] = true;
                    s->methods.push_back(method);
                } else {
                    break;
                }
            } else if (tok.kind == Tok::Ident) {
                // Field: name: Type
                Stmt::ClassField field;
                field.name = tok.text;
                field.pos = tok.pos;
                advance();

                if (!expect(Tok::Colon,
                        ScriptDiagnosticCode::MissingTypeHint,
                        "expected ':' and type hint in field declaration")) {
                    return nullptr;
                }
                field.type = parseType();
                if (!ok() || !field.type)
                    return nullptr;
                match(Tok::Newline);
                s->fields.push_back(field);
            } else if (tok.kind == Tok::KwVar || tok.kind == Tok::KwConst) {
                // Field with var/const prefix (alternative syntax)
                auto fieldDecl = parseVarDecl();
                if (fieldDecl) {
                    Stmt::ClassField field;
                    field.name = fieldDecl->varName;
                    field.type = fieldDecl->varType;
                    field.pos = fieldDecl->pos;
                    s->fields.push_back(field);
                } else {
                    break;
                }
            } else {
                setError(ScriptDiagnosticCode::UnexpectedToken,
                    "expected field or method in class body");
                return nullptr;
            }
        }

        if (ok())
            match(Tok::Dedent);

        return ok() ? s : nullptr;
    }

    // if cond: body elif cond: body else: body
    StmtPtr parseIfStmt()
    {
        SourcePos p = current().pos;
        advance(); // consume if

        auto s = makeStmt(StmtKind::If, p);
        s->cond = parseExpr();
        if (!ok() || !s->cond)
            return nullptr;

        if (!expect(Tok::Colon,
                ScriptDiagnosticCode::MissingColon,
                "expected ':' after if condition")) {
            return nullptr;
        }
        if (!parseBlock(s->body))
            return nullptr;

        // elif
        while (check(Tok::KwElif) && ok()) {
            SourcePos elifPos = current().pos;
            advance();
            ExprPtr elifCond = parseExpr();
            if (!ok() || !elifCond)
                return nullptr;
            if (!expect(Tok::Colon,
                    ScriptDiagnosticCode::MissingColon,
                    "expected ':' after elif condition")) {
                return nullptr;
            }
            core::pmr::Vector<StmtPtr> elifBody{&core::defaultCpuMemoryResource()};
            if (!parseBlock(elifBody))
                return nullptr;
            s->elifConds.push_back(elifCond);
            s->elifBodies.push_back(elifBody);
        }

        // else
        if (match(Tok::KwElse)) {
            if (!expect(
                    Tok::Colon, ScriptDiagnosticCode::MissingColon, "expected ':' after else")) {
                return nullptr;
            }
            if (!parseBlock(s->elseBody))
                return nullptr;
        }

        return s;
    }

    // while cond: body
    StmtPtr parseWhileStmt()
    {
        SourcePos p = current().pos;
        advance(); // consume while

        auto s = makeStmt(StmtKind::While, p);
        s->cond = parseExpr();
        if (!ok() || !s->cond)
            return nullptr;

        if (!expect(Tok::Colon,
                ScriptDiagnosticCode::MissingColon,
                "expected ':' after while condition")) {
            return nullptr;
        }
        if (!parseBlock(s->body))
            return nullptr;

        return s;
    }

    StmtPtr parseTryStmt()
    {
        SourcePos p = current().pos;
        advance();
        auto stmt = makeStmt(StmtKind::Try, p);
        if (!expect(Tok::Colon, ScriptDiagnosticCode::MissingColon, "expected ':' after try"))
            return nullptr;
        if (!parseBlock(stmt->tryBody))
            return nullptr;
        if (!check(Tok::KwExcept)) {
            setError(ScriptDiagnosticCode::UnexpectedToken, "expected except after try block");
            return nullptr;
        }
        while (match(Tok::KwExcept)) {
            Stmt::ExceptClause clause;
            if (check(Tok::Ident)) {
                clause.errorName = current().text;
                advance();
            }
            if (match(Tok::KwAs)) {
                if (!check(Tok::Ident)) {
                    setError(
                        ScriptDiagnosticCode::UnexpectedToken, "expected name after except as");
                    return nullptr;
                }
                clause.varName = current().text;
                advance();
            }
            if (!expect(
                    Tok::Colon, ScriptDiagnosticCode::MissingColon, "expected ':' after except"))
                return nullptr;
            if (!parseBlock(clause.body))
                return nullptr;
            stmt->exceptClauses.push_back(std::move(clause));
        }
        return stmt;
    }

    StmtPtr parseYieldStmt()
    {
        SourcePos p = current().pos;
        advance();
        auto stmt = makeStmt(StmtKind::Yield, p);
        if (match(Tok::KwFrom)) {
            stmt->yieldFrom = true;
            if (check(Tok::Newline) || check(Tok::Dedent) || atEof()) {
                setError(
                    ScriptDiagnosticCode::UnexpectedToken, "expected iterable after yield from");
                return nullptr;
            }
        }
        if (!check(Tok::Newline) && !check(Tok::Dedent) && !atEof()) {
            stmt->expr = parseExpr();
            if (!ok())
                return nullptr;
        }
        match(Tok::Newline);
        return stmt;
    }

    StmtPtr parseRaiseStmt()
    {
        SourcePos p = current().pos;
        advance();
        auto stmt = makeStmt(StmtKind::Raise, p);
        if (!check(Tok::Newline) && !check(Tok::Dedent) && !atEof()) {
            stmt->expr = parseExpr();
            if (!ok())
                return nullptr;
        }
        match(Tok::Newline);
        return stmt;
    }

    // for var: Type in iterable: body
    StmtPtr parseForStmt()
    {
        SourcePos p = current().pos;
        advance(); // consume for

        if (!check(Tok::Ident)) {
            setError(ScriptDiagnosticCode::UnexpectedToken, "expected iterator variable");
            return nullptr;
        }
        auto s = makeStmt(StmtKind::For, p);
        core::pmr::String firstName = current().text;
        advance();

        // Tuple binding: for key, value in iterable.
        if (match(Tok::Comma)) {
            if (!check(Tok::Ident)) {
                setError(
                    ScriptDiagnosticCode::UnexpectedToken, "expected value iterator after ','");
                return nullptr;
            }
            s->iterKeyName = firstName;
            s->iterName = current().text;
            advance();
        } else {
            s->iterName = firstName;
        }

        // Optional type hint
        if (match(Tok::Colon)) {
            s->iterType = parseType();
            if (!ok() || !s->iterType)
                return nullptr;
        }

        if (!expect(Tok::KwIn,
                ScriptDiagnosticCode::UnexpectedToken,
                "expected 'in' after for variable")) {
            return nullptr;
        }

        s->iterable = parseExpr();
        if (!ok() || !s->iterable)
            return nullptr;

        if (!expect(Tok::Colon,
                ScriptDiagnosticCode::MissingColon,
                "expected ':' after for iterable")) {
            return nullptr;
        }
        if (!parseBlock(s->body))
            return nullptr;

        return s;
    }

    // return expr?
    StmtPtr parseReturnStmt()
    {
        SourcePos p = current().pos;
        advance(); // consume return

        auto s = makeStmt(StmtKind::Return, p);
        if (!check(Tok::Newline) && !check(Tok::Dedent) && !atEof()) {
            s->expr = parseExpr();
            if (!ok())
                return nullptr;
        }
        match(Tok::Newline);
        return s;
    }

    // Expression or assignment statement.
    // If the expression is followed by an assignment operator, it's an assignment.
    StmtPtr parseExprOrAssignStmt()
    {
        SourcePos p = current().pos;
        ExprPtr expr = parseExpr();
        if (!ok() || !expr)
            return nullptr;

        if (isAssignOp(current().kind)) {
            // Assignment
            Tok opTok = current().kind;
            advance();
            ExprPtr value = parseExpr();
            if (!ok() || !value)
                return nullptr;
            match(Tok::Newline);

            auto s = makeStmt(StmtKind::Assign, p);
            s->target = expr;
            s->assignOp = assignOpText(opTok);
            s->value = value;
            return s;
        }

        match(Tok::Newline);
        auto s = makeStmt(StmtKind::Expr, p);
        s->expr = expr;
        return s;
    }

    // --- Module parsing ---
    std::shared_ptr<Module> parseModule()
    {
        auto mod = std::make_shared<Module>();
        while (!atEof() && ok()) {
            // Skip stray newlines at top level.
            if (match(Tok::Newline))
                continue;
            if (check(Tok::Dedent) || check(Tok::Indent)) {
                // Stray indent/dedent at top level shouldn't happen, skip.
                advance();
                continue;
            }
            auto stmt = parseStmt();
            if (stmt)
                mod->decls.push_back(stmt);
            else
                break;
        }
        return mod;
    }
};

} // namespace

ParseResult parse(const core::pmr::Vector<Token>& tokens)
{
    Parser p(tokens);
    auto mod = p.parseModule();
    ParseResult result;
    result.module = std::move(mod);
    result.error = std::move(p.error);
    return result;
}

ParseResult parseSource(core::pmr::String source, core::pmr::String filename)
{
    auto lexResult = lex(std::move(source), std::move(filename));
    if (!lexResult.ok()) {
        ParseResult result;
        result.error = std::move(lexResult.error);
        return result;
    }
    return parse(lexResult.tokens);
}

} // namespace arp::scriptvm
