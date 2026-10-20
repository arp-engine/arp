#pragma once

// ArpScript: AST node definitions.
//
// The AST is a tree of heap-allocated nodes owned via shared_ptr (core::makeShared).
// Nodes are immutable after construction -- the parser builds them, sema annotates
// them (via side tables), and the compiler walks them to emit bytecode.
//
// Node hierarchy:
//   Module
//     Decl*: ClassDecl, FuncDecl, VarDecl
//   Stmt:
//     ExprStmt, IfStmt, WhileStmt, ForStmt, ReturnStmt, BreakStmt,
//     ContinueStmt, PassStmt, AssignStmt
//   Expr:
//     IntLit, Int64Lit, FloatLit, StringLit, BoolLit, NilLit, IdentExpr,
//     BinaryExpr, UnaryExpr, CallExpr, IndexExpr, FieldExpr,
//     ListLit, DictLit, LambdaExpr, SelectorExpr

#include "core/cpu_allocator.h"
#include "scriptvm/runtime/source_pos.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace arp::reflection {
class class_info;
}

namespace arp::scriptvm {

// Forward declarations.
struct Expr;
struct Stmt;
struct TypeNode;

using ExprPtr = std::shared_ptr<Expr>;
using StmtPtr = std::shared_ptr<Stmt>;
using TypePtr = std::shared_ptr<TypeNode>;

// --- Type AST ---
// Represents a type annotation: int, float, string, bytes, bool, [T], {K: V},
// ?T (optional), (T1, T2) -> R, ClassName, func type.

enum class TypeKind {
    Basic,    // int, int64, float, string, bytes, bool, void
    List,     // [T]
    Dict,     // {K: V}
    Optional, // ?T
    Func,     // (T1, T2) -> R
    Class,    // user-defined class name
};

struct TypeNode {
    TypeKind kind = TypeKind::Basic;
    core::pmr::String name{&core::defaultCpuMemoryResource()}; // basic type name or class name
    TypePtr elem;  // for List: element type; for Optional: inner type
    TypePtr key;   // for Dict: key type
    TypePtr value; // for Dict: value type
    core::pmr::Vector<TypePtr> params{
        &core::defaultCpuMemoryResource()}; // for Func: parameter types
    TypePtr result;                         // for Func: result type
    TypePtr baseType;                       // for Class: direct base class type
    bool isVariadic = false;
    bool hasDefaultParams = false;
    std::size_t requiredParams = 0;
    bool isGenerator = false;
    const arp::reflection::class_info* reflectedClass = nullptr;
    SourcePos pos;

    TypeNode() = default;
};

// --- Expression AST ---

enum class ExprKind {
    IntLit,
    Int64Lit,
    FloatLit,
    StringLit,
    BoolLit,
    NilLit,
    Ident,
    Binary,
    Unary,
    Await,
    Call,
    Index,
    Field,
    ListLit,
    DictLit,
    Lambda,
    Selector, // a.b.c (chained field access)
    Assign,   // assignment as expression (for chained assignments)
    AsCast,   // value as primitive type
};

struct Expr {
    ExprKind kind;
    SourcePos pos;
    // Filled by semantic analysis and consumed by the bytecode compiler.
    // Keeping this on the AST lets typed arithmetic lower without repeating
    // reflection type checks in the VM.
    TypePtr inferredType;

    // Literal values.
    std::int64_t intVal = 0;
    double floatVal = 0.0;
    core::pmr::String strVal{&core::defaultCpuMemoryResource()};
    bool boolVal = false;

    // Identifier name.
    core::pmr::String name{&core::defaultCpuMemoryResource()};

    // Binary/Unary operator text (e.g. "+", "==", "not").
    core::pmr::String op{&core::defaultCpuMemoryResource()};

    // Children.
    ExprPtr left;
    ExprPtr right;
    core::pmr::Vector<ExprPtr> args{&core::defaultCpuMemoryResource()};       // for Call, ListLit
    core::pmr::Vector<ExprPtr> dictKeys{&core::defaultCpuMemoryResource()};   // for DictLit
    core::pmr::Vector<ExprPtr> dictValues{&core::defaultCpuMemoryResource()}; // for DictLit

    // Lambda: parameter names + types + body + return type.
    core::pmr::Vector<core::pmr::String> paramNames{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<TypePtr> paramTypes{&core::defaultCpuMemoryResource()};
    TypePtr retType;
    // AsCast: target type for the `expr as Type` conversion.
    TypePtr castType;
    // Lambda body: a list of statements (single-expression lambda not supported;
    // use `def` with explicit return).

    Expr() = default;

    Expr(ExprKind k, SourcePos p) : kind(k), pos(std::move(p)) {}
};

// --- Statement AST ---

enum class StmtKind {
    Expr,
    If,
    While,
    For,
    Return,
    Break,
    Continue,
    Pass,
    Assign,
    VarDecl,
    FuncDecl,
    ClassDecl,
    ImportDecl,
    Try,
    Raise,
    Yield,
};

struct Stmt {
    StmtKind kind;
    SourcePos pos;

    // For ExprStmt.
    ExprPtr expr;

    // For YieldStmt: true selects the bounded iterator-delegation lowering.
    // The delegated source remains in expr so both compiler paths share the
    // same AST contract.
    bool yieldFrom = false;

    // For IfStmt: condition, then-body, elif-conditions/bodies, else-body.
    ExprPtr cond;
    core::pmr::Vector<StmtPtr> body{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<ExprPtr> elifConds{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<core::pmr::Vector<StmtPtr>> elifBodies{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<StmtPtr> elseBody{&core::defaultCpuMemoryResource()};

    struct ExceptClause {
        core::pmr::String errorName{&core::defaultCpuMemoryResource()};
        core::pmr::String varName{&core::defaultCpuMemoryResource()};
        core::pmr::Vector<StmtPtr> body{&core::defaultCpuMemoryResource()};
    };

    core::pmr::Vector<StmtPtr> tryBody{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<ExceptClause> exceptClauses{&core::defaultCpuMemoryResource()};

    // For WhileStmt: cond + body (reuses `cond` and `body`).

    // For ForStmt: iterator var name, iterable expr, body.
    core::pmr::String iterKeyName{&core::defaultCpuMemoryResource()};
    core::pmr::String iterName{&core::defaultCpuMemoryResource()};
    TypePtr keyType;
    TypePtr iterType;
    ExprPtr iterable;

    // For ReturnStmt: value expr (reuses `expr`).

    // For AssignStmt: target, operator (=, +=, etc.), value.
    ExprPtr target;
    core::pmr::String assignOp{&core::defaultCpuMemoryResource()}; // "=", "+=", "-=", etc.
    ExprPtr value;

    // For VarDecl: name, type, initializer, isConst.
    core::pmr::String varName{&core::defaultCpuMemoryResource()};
    TypePtr varType;
    bool isConst = false;

    // For FuncDecl: name, params, return type, body.
    core::pmr::String funcName{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<core::pmr::String> paramNames{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<TypePtr> paramTypes{&core::defaultCpuMemoryResource()};
    bool variadicParam = false;
    int variadicParamIndex = -1;
    core::pmr::Vector<bool> paramIsSelf; // for methods: param[0] is self
    // Semantic annotation for the class constructor method. The parser keeps
    // all methods syntactically uniform; sema assigns this role by convention.
    bool isConstructor = false;
    bool isGenerator = false;
    TypePtr retType;

    // For ClassDecl: name, optional base class, fields, methods.
    core::pmr::String className{&core::defaultCpuMemoryResource()};
    core::pmr::String baseClassName{&core::defaultCpuMemoryResource()};

    struct ClassField {
        core::pmr::String name{&core::defaultCpuMemoryResource()};
        TypePtr type;
        SourcePos pos;
    };

    core::pmr::Vector<ClassField> fields{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<StmtPtr> methods{&core::defaultCpuMemoryResource()};

    // For ImportDecl: module import declarations.
    // Two forms:
    //   from <module> import <name> [as <alias>] (, <name> [as <alias>])*
    //   import <module> [as <alias>]
    struct ImportName {
        core::pmr::String name{&core::defaultCpuMemoryResource()};
        core::pmr::String alias{&core::defaultCpuMemoryResource()}; // empty = no alias
    };

    core::pmr::String importModule{&core::defaultCpuMemoryResource()};
    bool importIsModuleImport =
        false; // true = "import X" (module ref); false = "from X import ..."
    core::pmr::Vector<ImportName> importNames{
        &core::defaultCpuMemoryResource()};                           // from-import names
    core::pmr::String importAlias{&core::defaultCpuMemoryResource()}; // module import alias

    Stmt() = default;

    Stmt(StmtKind k, SourcePos p) : kind(k), pos(std::move(p))
    {
        paramIsSelf = core::pmr::Vector<bool>(&core::defaultCpuMemoryResource());
    }
};

// --- Module AST ---

struct Module {
    core::pmr::String filename{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<StmtPtr> decls{&core::defaultCpuMemoryResource()}; // top-level declarations
};

// Factory helpers for creating AST nodes (using core allocator).
inline ExprPtr makeExpr(ExprKind k, SourcePos pos)
{
    return std::make_shared<Expr>(k, std::move(pos));
}

inline StmtPtr makeStmt(StmtKind k, SourcePos pos)
{
    return std::make_shared<Stmt>(k, std::move(pos));
}

inline TypePtr makeType(TypeKind k)
{
    auto t = std::make_shared<TypeNode>();
    t->kind = k;
    return t;
}

} // namespace arp::scriptvm
