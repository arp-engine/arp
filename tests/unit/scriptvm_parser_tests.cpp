// ArpScript Parser unit tests (Phase 3).
//
// Tests the recursive-descent parser: AST structure for var/const decls,
// function decls, class decls, control flow (if/elif/else, for, while),
// expressions (binary, unary, call, index, field, list/dict literals),
// and error reporting with line/column.
//
// Test style: self-contained main() + require() helpers, no framework.

#include "scriptvm/compile/ast.h"
#include "scriptvm/compile/lexer.h"
#include "scriptvm/compile/parser.h"

#include <cstdio>
#include <cstdlib>

using namespace arp::scriptvm;

namespace {

int g_failures = 0;

void require(bool cond, const char* label)
{
    if (!cond) {
        std::printf("FAIL: %s\n", label);
        ++g_failures;
    }
}

// Parse source, assert success.
ParseResult parseOk(const char* src, const char* label)
{
    arp::core::pmr::String source(src, &arp::core::defaultCpuMemoryResource());
    arp::core::pmr::String fname("test", &arp::core::defaultCpuMemoryResource());
    auto result = parseSource(std::move(source), std::move(fname));
    if (!result.ok()) {
        std::printf("FAIL: %s -- parse error: %s (line %d, col %d)\n",
            label,
            result.error.message.c_str(),
            result.error.line,
            result.error.column);
        ++g_failures;
    }
    return result;
}

void testVarDecl()
{
    auto pr = parseOk("var x: int = 42\n", "var decl");
    require(pr.module != nullptr, "var decl: module exists");
    require(pr.module->decls.size() == 1, "var decl: one decl");
    auto& stmt = pr.module->decls[0];
    require(stmt->kind == StmtKind::VarDecl, "var decl: kind");
    require(stmt->varName == arp::core::pmr::String("x", &arp::core::defaultCpuMemoryResource()),
        "var decl: name");
    require(!stmt->isConst, "var decl: not const");
    require(stmt->varType != nullptr, "var decl: has type");
    require(stmt->varType->kind == TypeKind::Basic, "var decl: basic type");
    require(stmt->varType->name
            == arp::core::pmr::String("int", &arp::core::defaultCpuMemoryResource()),
        "var decl: type int");
    require(stmt->expr != nullptr, "var decl: has init");
    require(stmt->expr->kind == ExprKind::IntLit, "var decl: init is int lit");
    require(stmt->expr->intVal == 42, "var decl: init value 42");
}

void testConstDecl()
{
    auto pr = parseOk("const pi: float = 3.14\n", "const decl");
    require(pr.module->decls.size() == 1, "const decl: one decl");
    auto& stmt = pr.module->decls[0];
    require(stmt->kind == StmtKind::VarDecl, "const decl: kind");
    require(stmt->isConst, "const decl: is const");
    require(stmt->varType->name
            == arp::core::pmr::String("float", &arp::core::defaultCpuMemoryResource()),
        "const decl: type float");
    require(stmt->expr->kind == ExprKind::FloatLit, "const decl: init is float lit");
}

void testAsCastExpr()
{
    auto pr = parseOk("def test() -> int:\n"
                      "    return \"42\" as int\n",
        "as cast");
    auto& value = pr.module->decls[0]->body[0]->expr;
    require(value->kind == ExprKind::AsCast, "as cast: kind");
    require(value->left && value->left->kind == ExprKind::StringLit, "as cast: source expression");
    require(value->castType && value->castType->name == "int", "as cast: target type");
}

void testForTupleBinding()
{
    auto pr = parseOk("def test() -> int:\n"
                      "    for key, value in items:\n"
                      "        pass\n",
        "for tuple");
    if (!pr.ok() || !pr.module || pr.module->decls.empty() || pr.module->decls[0]->body.empty())
        return;
    auto& loop = pr.module->decls[0]->body[0];
    require(loop->kind == StmtKind::For, "for tuple: kind");
    require(loop->iterKeyName == "key", "for tuple: key name");
    require(loop->iterName == "value", "for tuple: value name");
}

void testLambdaExpr()
{
    auto pr = parseOk("def test() -> int:\n"
                      "    return (lambda(x: int) -> int: x * 2)(21)\n",
        "lambda");
    if (!pr.ok() || !pr.module || pr.module->decls.empty() || pr.module->decls[0]->body.empty())
        return;
    auto& call = pr.module->decls[0]->body[0]->expr;
    require(call->kind == ExprKind::Call, "lambda: call kind");
    require(call->left->kind == ExprKind::Lambda, "lambda: callee kind");
    require(call->left->paramNames.size() == 1, "lambda: parameter count");
    require(call->left->retType && call->left->retType->name == "int", "lambda: return type");
}

void testVariadicParam()
{
    auto pr = parseOk("def sum_all(base: int, *args: int) -> int:\n"
                      "    return base\n",
        "variadic");
    if (!pr.ok() || !pr.module || pr.module->decls.empty())
        return;
    auto& fn = pr.module->decls[0];
    require(fn->variadicParam, "variadic: flag");
    require(fn->variadicParamIndex == 1, "variadic: index");
    require(fn->paramNames[1] == "args", "variadic: name");
}

void testTryRaise()
{
    auto pr = parseOk("def test() -> int:\n"
                      "    try:\n"
                      "        raise \"boom\"\n"
                      "    except as error:\n"
                      "        return 1\n",
        "try raise");
    if (!pr.ok() || !pr.module || pr.module->decls.empty() || pr.module->decls[0]->body.empty())
        return;
    auto& stmt = pr.module->decls[0]->body[0];
    require(stmt->kind == StmtKind::Try, "try: kind");
    require(
        stmt->tryBody.size() == 1 && stmt->tryBody[0]->kind == StmtKind::Raise, "try: raise body");
    require(stmt->exceptClauses.size() == 1 && stmt->exceptClauses[0].varName == "error",
        "try: except binding");
}

void testYield()
{
    auto pr = parseOk("def gen() -> int:\n"
                      "    yield 1\n"
                      "    yield\n",
        "yield");
    if (!pr.ok() || !pr.module || pr.module->decls.empty())
        return;
    const auto& body = pr.module->decls[0]->body;
    require(body.size() == 2, "yield: two statements");
    if (body.size() == 2) {
        require(body[0]->kind == StmtKind::Yield && body[0]->expr, "yield: expression form");
        require(body[1]->kind == StmtKind::Yield && !body[1]->expr, "yield: bare form");
    }
}

void testFuncDecl()
{
    const char* src = "def add(a: int, b: int) -> int:\n"
                      "    return a + b\n";
    auto pr = parseOk(src, "func decl");
    require(pr.module->decls.size() == 1, "func decl: one decl");
    auto& stmt = pr.module->decls[0];
    require(stmt->kind == StmtKind::FuncDecl, "func decl: kind");
    require(stmt->funcName == arp::core::pmr::String("add", &arp::core::defaultCpuMemoryResource()),
        "func decl: name");
    require(stmt->paramNames.size() == 2, "func decl: 2 params");
    require(
        stmt->paramNames[0] == arp::core::pmr::String("a", &arp::core::defaultCpuMemoryResource()),
        "func decl: param 0 name");
    require(stmt->paramTypes[0]->name
            == arp::core::pmr::String("int", &arp::core::defaultCpuMemoryResource()),
        "func decl: param 0 type");
    require(stmt->retType->name
            == arp::core::pmr::String("int", &arp::core::defaultCpuMemoryResource()),
        "func decl: return type");
    require(stmt->body.size() == 1, "func decl: body 1 stmt");
    require(stmt->body[0]->kind == StmtKind::Return, "func decl: body is return");
    require(stmt->body[0]->expr->kind == ExprKind::Binary, "func decl: return is binary");
    require(stmt->body[0]->expr->op
            == arp::core::pmr::String("+", &arp::core::defaultCpuMemoryResource()),
        "func decl: return op +");
}

void testClassDecl()
{
    const char* src = "class Vec3:\n"
                      "    x: float\n"
                      "    y: float\n"
                      "    z: float\n"
                      "    def length(self) -> float:\n"
                      "        return 1.0\n";
    auto pr = parseOk(src, "class decl");
    require(pr.module->decls.size() == 1, "class decl: one decl");
    auto& stmt = pr.module->decls[0];
    require(stmt->kind == StmtKind::ClassDecl, "class decl: kind");
    require(
        stmt->className == arp::core::pmr::String("Vec3", &arp::core::defaultCpuMemoryResource()),
        "class decl: name");
    require(stmt->fields.size() == 3, "class decl: 3 fields");
    require(
        stmt->fields[0].name == arp::core::pmr::String("x", &arp::core::defaultCpuMemoryResource()),
        "class decl: field 0 name");
    require(stmt->fields[0].type->name
            == arp::core::pmr::String("float", &arp::core::defaultCpuMemoryResource()),
        "class decl: field 0 type");
    require(stmt->methods.size() == 1, "class decl: 1 method");
    require(stmt->methods[0]->funcName
            == arp::core::pmr::String("length", &arp::core::defaultCpuMemoryResource()),
        "class decl: method name");
    require(stmt->methods[0]->paramIsSelf[0] == true, "class decl: first param is self");
}

void testClassInheritance()
{
    const char* src = "class Importer:\n"
                      "    def read(self) -> int:\n"
                      "        return 7\n"
                      "class GlbImporter(Importer):\n"
                      "    def value(self) -> int:\n"
                      "        return self.read()\n";
    auto pr = parseOk(src, "class inheritance");
    if (!pr.ok() || pr.module->decls.size() < 2)
        return;
    require(pr.module->decls[1]->baseClassName == "Importer", "class inheritance: base name");
}

void testCrLfBlankLineInClass()
{
    const char* src = "class Importer:\r\n"
                      "    def read(self) -> int:\r\n"
                      "        return 7\r\n"
                      "\r\n"
                      "    def write(self) -> int:\r\n"
                      "        return self.read()\r\n";
    auto pr = parseOk(src, "CRLF blank line in class");
    if (!pr.ok() || pr.module->decls.empty())
        return;
    require(pr.module->decls[0]->methods.size() == 2, "CRLF blank line in class: two methods");
}

void testIfElifElse()
{
    const char* src = "if x > 0:\n"
                      "    y = 1\n"
                      "elif x == 0:\n"
                      "    y = 0\n"
                      "else:\n"
                      "    y = -1\n";
    auto pr = parseOk(src, "if/elif/else");
    require(pr.module->decls.size() == 1, "if: one decl");
    auto& stmt = pr.module->decls[0];
    require(stmt->kind == StmtKind::If, "if: kind");
    require(stmt->cond != nullptr, "if: has cond");
    require(stmt->body.size() == 1, "if: body 1 stmt");
    require(stmt->elifConds.size() == 1, "if: 1 elif");
    require(stmt->elifBodies.size() == 1, "if: 1 elif body");
    require(stmt->elifBodies[0].size() == 1, "if: elif body 1 stmt");
    require(stmt->elseBody.size() == 1, "if: else body 1 stmt");
}

void testWhileStmt()
{
    const char* src = "while x > 0:\n"
                      "    x = x - 1\n"
                      "    if x == 5:\n"
                      "        continue\n";
    auto pr = parseOk(src, "while");
    auto& stmt = pr.module->decls[0];
    require(stmt->kind == StmtKind::While, "while: kind");
    require(stmt->cond != nullptr, "while: has cond");
    require(stmt->body.size() == 2, "while: body 2 stmts");
    require(stmt->body[1]->kind == StmtKind::If, "while: second stmt is if");
}

void testForStmt()
{
    const char* src = "for i: int in range(10):\n"
                      "    print(i)\n";
    auto pr = parseOk(src, "for");
    auto& stmt = pr.module->decls[0];
    require(stmt->kind == StmtKind::For, "for: kind");
    require(stmt->iterName == arp::core::pmr::String("i", &arp::core::defaultCpuMemoryResource()),
        "for: iter name");
    require(stmt->iterType != nullptr, "for: has iter type");
    require(stmt->iterable != nullptr, "for: has iterable");
    require(stmt->iterable->kind == ExprKind::Call, "for: iterable is call");
    require(stmt->body.size() == 1, "for: body 1 stmt");
}

void testBinaryExpr()
{
    auto pr = parseOk("x = 1 + 2 * 3\n", "binary expr");
    auto& stmt = pr.module->decls[0];
    require(stmt->kind == StmtKind::Assign, "binary: assign kind");
    require(stmt->value->kind == ExprKind::Binary, "binary: value is binary");
    require(stmt->value->op == arp::core::pmr::String("+", &arp::core::defaultCpuMemoryResource()),
        "binary: top op +");
    // 1 + (2 * 3): left=1, right=(2*3)
    require(stmt->value->left->kind == ExprKind::IntLit, "binary: left is int");
    require(stmt->value->right->kind == ExprKind::Binary, "binary: right is binary");
    require(stmt->value->right->op
            == arp::core::pmr::String("*", &arp::core::defaultCpuMemoryResource()),
        "binary: right op *");
}

void testUnaryExpr()
{
    auto pr = parseOk("x = -5\n", "unary expr");
    auto& stmt = pr.module->decls[0];
    require(stmt->value->kind == ExprKind::Unary, "unary: kind");
    require(stmt->value->op == arp::core::pmr::String("-", &arp::core::defaultCpuMemoryResource()),
        "unary: op -");
    require(stmt->value->left->kind == ExprKind::IntLit, "unary: operand int");
    require(stmt->value->left->intVal == 5, "unary: operand 5");
}

void testCallExpr()
{
    auto pr = parseOk("fib(10)\n", "call expr");
    auto& stmt = pr.module->decls[0];
    require(stmt->kind == StmtKind::Expr, "call: expr stmt");
    require(stmt->expr->kind == ExprKind::Call, "call: kind");
    require(stmt->expr->left->kind == ExprKind::Ident, "call: callee is ident");
    require(stmt->expr->left->name
            == arp::core::pmr::String("fib", &arp::core::defaultCpuMemoryResource()),
        "call: callee name");
    require(stmt->expr->args.size() == 1, "call: 1 arg");
    require(stmt->expr->args[0]->kind == ExprKind::IntLit, "call: arg int");
    require(stmt->expr->args[0]->intVal == 10, "call: arg 10");
}

void testIndexExpr()
{
    auto pr = parseOk("x = arr[0]\n", "index expr");
    auto& stmt = pr.module->decls[0];
    require(stmt->value->kind == ExprKind::Index, "index: kind");
    require(stmt->value->left->kind == ExprKind::Ident, "index: base ident");
    require(stmt->value->right->kind == ExprKind::IntLit, "index: index int");
}

void testFieldExpr()
{
    auto pr = parseOk("x = vec.x\n", "field expr");
    auto& stmt = pr.module->decls[0];
    require(stmt->value->kind == ExprKind::Field, "field: kind");
    require(stmt->value->left->kind == ExprKind::Ident, "field: base ident");
    require(
        stmt->value->name == arp::core::pmr::String("x", &arp::core::defaultCpuMemoryResource()),
        "field: name");
}

void testListLiteral()
{
    auto pr = parseOk("x = [1, 2, 3]\n", "list literal");
    auto& stmt = pr.module->decls[0];
    require(stmt->value->kind == ExprKind::ListLit, "list: kind");
    require(stmt->value->args.size() == 3, "list: 3 elements");
    require(stmt->value->args[0]->kind == ExprKind::IntLit, "list: elem 0 int");
}

void testDictLiteral()
{
    auto pr = parseOk("x = {\"a\": 1, \"b\": 2}\n", "dict literal");
    auto& stmt = pr.module->decls[0];
    require(stmt->value->kind == ExprKind::DictLit, "dict: kind");
    require(stmt->value->dictKeys.size() == 2, "dict: 2 keys");
    require(stmt->value->dictValues.size() == 2, "dict: 2 values");
    require(stmt->value->dictKeys[0]->kind == ExprKind::StringLit, "dict: key 0 string");
    require(stmt->value->dictValues[0]->kind == ExprKind::IntLit, "dict: val 0 int");
}

void testListType()
{
    auto pr = parseOk("var arr: [int] = [1, 2]\n", "list type");
    auto& stmt = pr.module->decls[0];
    require(stmt->varType->kind == TypeKind::List, "list type: kind");
    require(stmt->varType->elem->name
            == arp::core::pmr::String("int", &arp::core::defaultCpuMemoryResource()),
        "list type: elem int");
}

void testBytesType()
{
    auto pr = parseOk("var payload: bytes = value\n", "bytes type");
    auto& stmt = pr.module->decls[0];
    require(stmt->varType->kind == TypeKind::Basic, "bytes type: basic kind");
    require(stmt->varType->name
            == arp::core::pmr::String("bytes", &arp::core::defaultCpuMemoryResource()),
        "bytes type: name");
}

void testDictType()
{
    auto pr = parseOk("var m: {string: int} = {}\n", "dict type");
    auto& stmt = pr.module->decls[0];
    require(stmt->varType->kind == TypeKind::Dict, "dict type: kind");
    require(stmt->varType->key->name
            == arp::core::pmr::String("string", &arp::core::defaultCpuMemoryResource()),
        "dict type: key");
    require(stmt->varType->value->name
            == arp::core::pmr::String("int", &arp::core::defaultCpuMemoryResource()),
        "dict type: value");
}

void testOptionalType()
{
    auto pr = parseOk("var x: int? = nil\n", "optional type");
    auto& stmt = pr.module->decls[0];
    require(stmt->varType->kind == TypeKind::Optional, "optional: kind");
    require(stmt->varType->elem->name
            == arp::core::pmr::String("int", &arp::core::defaultCpuMemoryResource()),
        "optional: inner int");
}

void testFuncType()
{
    const char* src = "var f: (int) -> int = g\n";
    auto pr = parseOk(src, "func type");
    auto& stmt = pr.module->decls[0];
    require(stmt->varType->kind == TypeKind::Func, "func type: kind");
    require(stmt->varType->params.size() == 1, "func type: 1 param");
    require(stmt->varType->result->name
            == arp::core::pmr::String("int", &arp::core::defaultCpuMemoryResource()),
        "func type: result");
}

void testAssignOps()
{
    auto pr = parseOk("x += 1\n", "assign += ");
    auto& stmt = pr.module->decls[0];
    require(stmt->kind == StmtKind::Assign, "assign: kind");
    require(stmt->assignOp == arp::core::pmr::String("+=", &arp::core::defaultCpuMemoryResource()),
        "assign: op +=");
}

void testNestedCall()
{
    auto pr = parseOk("print(fib(n - 1) + fib(n - 2))\n", "nested call");
    auto& stmt = pr.module->decls[0];
    require(stmt->expr->kind == ExprKind::Call, "nested: top is call");
    require(stmt->expr->args[0]->kind == ExprKind::Binary, "nested: arg is binary");
    require(stmt->expr->args[0]->left->kind == ExprKind::Call, "nested: left is call");
    require(stmt->expr->args[0]->right->kind == ExprKind::Call, "nested: right is call");
}

void testChainedField()
{
    auto pr = parseOk("x = a.b.c\n", "chained field");
    auto& stmt = pr.module->decls[0];
    require(stmt->value->kind == ExprKind::Field, "chained: top field");
    require(
        stmt->value->name == arp::core::pmr::String("c", &arp::core::defaultCpuMemoryResource()),
        "chained: top name c");
    require(stmt->value->left->kind == ExprKind::Field, "chained: inner field");
    require(stmt->value->left->name
            == arp::core::pmr::String("b", &arp::core::defaultCpuMemoryResource()),
        "chained: inner name b");
}

void testBreakContinuePass()
{
    auto pr = parseOk("break\n", "break");
    require(pr.module->decls[0]->kind == StmtKind::Break, "break: kind");
    pr = parseOk("continue\n", "continue");
    require(pr.module->decls[0]->kind == StmtKind::Continue, "continue: kind");
    pr = parseOk("pass\n", "pass");
    require(pr.module->decls[0]->kind == StmtKind::Pass, "pass: kind");
}

void testPowerRightAssoc()
{
    // 2 ** 3 ** 2 = 2 ** (3 ** 2) = 2 ** 9 = 512
    auto pr = parseOk("x = 2 ** 3 ** 2\n", "power right assoc");
    auto& stmt = pr.module->decls[0];
    require(stmt->value->kind == ExprKind::Binary, "power: top binary");
    require(stmt->value->op == arp::core::pmr::String("**", &arp::core::defaultCpuMemoryResource()),
        "power: top op **");
    require(stmt->value->left->kind == ExprKind::IntLit, "power: left int");
    require(stmt->value->right->kind == ExprKind::Binary, "power: right is binary (right-assoc)");
    require(stmt->value->right->op
            == arp::core::pmr::String("**", &arp::core::defaultCpuMemoryResource()),
        "power: right op **");
}

void testMissingColon()
{
    arp::core::pmr::String source("if x\n", &arp::core::defaultCpuMemoryResource());
    arp::core::pmr::String fname("test", &arp::core::defaultCpuMemoryResource());
    auto result = parseSource(std::move(source), std::move(fname));
    require(!result.ok(), "missing colon: error");
    require(result.error.code == ScriptDiagnosticCode::MissingColon, "missing colon: code");
}

void testMissingTypeHint()
{
    arp::core::pmr::String source("var x = 5\n", &arp::core::defaultCpuMemoryResource());
    arp::core::pmr::String fname("test", &arp::core::defaultCpuMemoryResource());
    auto result = parseSource(std::move(source), std::move(fname));
    require(!result.ok(), "missing type hint: error");
    require(result.error.code == ScriptDiagnosticCode::MissingTypeHint, "missing type hint: code");
}

void testReturnWithoutValue()
{
    const char* src = "def f() -> void:\n"
                      "    return\n";
    auto pr = parseOk(src, "return no value");
    auto& stmt = pr.module->decls[0];
    require(stmt->body[0]->kind == StmtKind::Return, "return no val: kind");
    require(stmt->body[0]->expr == nullptr, "return no val: null expr");
}

void testMultipleStatements()
{
    const char* src = "var x: int = 1\n"
                      "var y: int = 2\n"
                      "var z: int = x + y\n";
    auto pr = parseOk(src, "multiple stmts");
    require(pr.module->decls.size() == 3, "multiple: 3 decls");
    require(pr.module->decls[0]->varName
            == arp::core::pmr::String("x", &arp::core::defaultCpuMemoryResource()),
        "multiple: decl 0 x");
    require(pr.module->decls[1]->varName
            == arp::core::pmr::String("y", &arp::core::defaultCpuMemoryResource()),
        "multiple: decl 1 y");
    require(pr.module->decls[2]->varName
            == arp::core::pmr::String("z", &arp::core::defaultCpuMemoryResource()),
        "multiple: decl 2 z");
}

void testStringLiteral()
{
    auto pr = parseOk("var s: string = \"hello\"\n", "string literal");
    auto& stmt = pr.module->decls[0];
    require(stmt->expr->kind == ExprKind::StringLit, "string lit: kind");
    require(stmt->expr->strVal
            == arp::core::pmr::String("hello", &arp::core::defaultCpuMemoryResource()),
        "string lit: value");
}

void testBoolAndNil()
{
    auto pr = parseOk("var t: bool = true\n", "bool true");
    require(pr.module->decls[0]->expr->kind == ExprKind::BoolLit, "bool: kind");
    require(pr.module->decls[0]->expr->boolVal == true, "bool: value true");

    pr = parseOk("var n: int? = nil\n", "nil");
    require(pr.module->decls[0]->expr->kind == ExprKind::NilLit, "nil: kind");
}

void testFromImport()
{
    // from math import sqrt
    auto pr = parseOk("from math import sqrt\n", "from-import single");
    require(pr.module->decls[0]->kind == StmtKind::ImportDecl, "from-import: kind");
    require(!pr.module->decls[0]->importIsModuleImport, "from-import: is not module import");
    require(pr.module->decls[0]->importModule
            == arp::core::pmr::String("math", &arp::core::defaultCpuMemoryResource()),
        "from-import: module name");
    require(pr.module->decls[0]->importNames.size() == 1, "from-import: 1 name");
    require(pr.module->decls[0]->importNames[0].name
            == arp::core::pmr::String("sqrt", &arp::core::defaultCpuMemoryResource()),
        "from-import: name sqrt");
    require(pr.module->decls[0]->importNames[0].alias
            == arp::core::pmr::String("", &arp::core::defaultCpuMemoryResource()),
        "from-import: no alias");
}

void testFromImportWithAlias()
{
    // from math import sqrt as s, abs as a
    auto pr = parseOk("from math import sqrt as s, abs as a\n", "from-import with alias");
    require(pr.module->decls[0]->kind == StmtKind::ImportDecl, "from-alias: kind");
    require(pr.module->decls[0]->importNames.size() == 2, "from-alias: 2 names");
    require(pr.module->decls[0]->importNames[0].name
            == arp::core::pmr::String("sqrt", &arp::core::defaultCpuMemoryResource()),
        "from-alias: name sqrt");
    require(pr.module->decls[0]->importNames[0].alias
            == arp::core::pmr::String("s", &arp::core::defaultCpuMemoryResource()),
        "from-alias: alias s");
    require(pr.module->decls[0]->importNames[1].name
            == arp::core::pmr::String("abs", &arp::core::defaultCpuMemoryResource()),
        "from-alias: name abs");
    require(pr.module->decls[0]->importNames[1].alias
            == arp::core::pmr::String("a", &arp::core::defaultCpuMemoryResource()),
        "from-alias: alias a");
}

void testImportModuleAs()
{
    // import mymod as m
    auto pr = parseOk("import mymod as m\n", "import as");
    require(pr.module->decls[0]->kind == StmtKind::ImportDecl, "import-as: kind");
    require(pr.module->decls[0]->importIsModuleImport, "import-as: is module import");
    require(pr.module->decls[0]->importModule
            == arp::core::pmr::String("mymod", &arp::core::defaultCpuMemoryResource()),
        "import-as: module name");
    require(pr.module->decls[0]->importAlias
            == arp::core::pmr::String("m", &arp::core::defaultCpuMemoryResource()),
        "import-as: alias m");
}

void testImportModuleNoAlias()
{
    // import mymod
    auto pr = parseOk("import mymod\n", "import no alias");
    require(pr.module->decls[0]->kind == StmtKind::ImportDecl, "import-noalias: kind");
    require(pr.module->decls[0]->importIsModuleImport, "import-noalias: is module import");
    require(pr.module->decls[0]->importModule
            == arp::core::pmr::String("mymod", &arp::core::defaultCpuMemoryResource()),
        "import-noalias: module name");
    require(pr.module->decls[0]->importAlias
            == arp::core::pmr::String("", &arp::core::defaultCpuMemoryResource()),
        "import-noalias: no alias");
}

void testImportError()
{
    // "from import x" should fail: no module name after 'from'
    arp::core::pmr::String source("from import x\n", &arp::core::defaultCpuMemoryResource());
    arp::core::pmr::String fname("test", &arp::core::defaultCpuMemoryResource());
    auto result = parseSource(std::move(source), std::move(fname));
    require(!result.ok(), "from-import error: should fail");
}

} // namespace

int main()
{
    testVarDecl();
    testConstDecl();
    testAsCastExpr();
    testForTupleBinding();
    testLambdaExpr();
    testVariadicParam();
    testTryRaise();
    testYield();
    testFuncDecl();
    testClassDecl();
    testClassInheritance();
    testCrLfBlankLineInClass();
    testIfElifElse();
    testWhileStmt();
    testForStmt();
    testBinaryExpr();
    testUnaryExpr();
    testCallExpr();
    testIndexExpr();
    testFieldExpr();
    testListLiteral();
    testDictLiteral();
    testListType();
    testBytesType();
    testDictType();
    testOptionalType();
    testFuncType();
    testAssignOps();
    testNestedCall();
    testChainedField();
    testBreakContinuePass();
    testPowerRightAssoc();
    testMissingColon();
    testMissingTypeHint();
    testReturnWithoutValue();
    testMultipleStatements();
    testStringLiteral();
    testBoolAndNil();
    testFromImport();
    testFromImportWithAlias();
    testImportModuleAs();
    testImportModuleNoAlias();
    testImportError();

    if (g_failures > 0) {
        std::printf("\n%d test(s) FAILED\n", g_failures);
        return 1;
    }
    std::printf("\nAll ArpScript parser tests passed\n");
    return 0;
}
