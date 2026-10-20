#include "scriptvm/compile/sema.h"

#include "scriptvm/compile/parser.h"
#include "scriptvm/runtime/value.h"

#include <cstdio>
#include <string>

namespace arp::scriptvm {

namespace {

// Convert a long long to a pmr::String (avoids std::string/pmr::string mismatch).
core::pmr::String toStr(long long v)
{
    return core::pmr::String(std::to_string(v), &core::defaultCpuMemoryResource());
}

// --- Type representation ---
// For sema we use the TypeNode from the AST directly (shared_ptr).
// Type equality is structural: two types are compatible if their kind matches
// and their component types match. Reflected class types match by class_info.

bool typesEqual(const TypePtr& a, const TypePtr& b);
bool containsYield(const core::pmr::Vector<StmtPtr>& body);

bool typesEqual(const TypePtr& a, const TypePtr& b)
{
    if (!a || !b)
        return a == b;
    if (a->kind != b->kind)
        return false;
    switch (a->kind) {
    case TypeKind::Basic:
        return a->name == b->name;
    case TypeKind::Class:
        if (a->reflectedClass != nullptr || b->reflectedClass != nullptr) {
            return a->reflectedClass != nullptr && a->reflectedClass == b->reflectedClass;
        }
        return a->name == b->name;
    case TypeKind::List:
        return typesEqual(a->elem, b->elem);
    case TypeKind::Dict:
        return typesEqual(a->key, b->key) && typesEqual(a->value, b->value);
    case TypeKind::Optional:
        return typesEqual(a->elem, b->elem);
    case TypeKind::Func:
        if (a->isVariadic != b->isVariadic)
            return false;
        if (a->params.size() != b->params.size())
            return false;
        for (std::size_t i = 0; i < a->params.size(); ++i)
            if (!typesEqual(a->params[i], b->params[i]))
                return false;
        return typesEqual(a->result, b->result);
    }
    return false;
}

TypePtr functionTypeFor(const StmtPtr& stmt)
{
    auto funcType = makeType(TypeKind::Func);
    funcType->isVariadic = stmt->variadicParam;
    funcType->isGenerator = containsYield(stmt->body);
    for (std::size_t i = 0; i < stmt->paramTypes.size(); ++i) {
        if (stmt->variadicParam && static_cast<int>(i) == stmt->variadicParamIndex) {
            auto listType = makeType(TypeKind::List);
            listType->elem = stmt->paramTypes[i];
            funcType->params.push_back(listType);
        } else {
            funcType->params.push_back(stmt->paramTypes[i]);
        }
    }
    funcType->result = stmt->retType;
    return funcType;
}

bool containsYield(const core::pmr::Vector<StmtPtr>& body)
{
    for (const auto& stmt : body) {
        if (!stmt)
            continue;
        switch (stmt->kind) {
        case StmtKind::Yield:
            return true;
        case StmtKind::FuncDecl:
        case StmtKind::ClassDecl:
            break;
        case StmtKind::If:
            if (containsYield(stmt->body) || containsYield(stmt->elseBody))
                return true;
            for (const auto& branch : stmt->elifBodies)
                if (containsYield(branch))
                    return true;
            break;
        case StmtKind::While:
        case StmtKind::For:
            if (containsYield(stmt->body))
                return true;
            break;
        case StmtKind::Try:
            if (containsYield(stmt->tryBody))
                return true;
            for (const auto& clause : stmt->exceptClauses)
                if (containsYield(clause.body))
                    return true;
            break;
        default:
            break;
        }
    }
    return false;
}

// Check if `from` is assignable to `to` (type compatibility).
// Rules:
//   - Same type => compatible
//   - nil => any Optional or Class type
//   - int => int64, float
//   - int64 => float
//   - T => T? (optional of T)
//   - a derived script class is assignable to any ancestor class
bool isAssignable(const TypePtr& from, const TypePtr& to)
{
    if (!from || !to)
        return false;
    if (typesEqual(from, to))
        return true;

    if (from->kind == TypeKind::Class && to->kind == TypeKind::Class) {
        if (from->reflectedClass && to->reflectedClass
            && to->reflectedClass->is_base_of(from->reflectedClass))
            return true;
        for (TypePtr base = from->baseType; base; base = base->baseType) {
            if (typesEqual(base, to))
                return true;
        }
    }

    if (from->kind == TypeKind::List && to->kind == TypeKind::List && from->elem && to->elem
        && isAssignable(from->elem, to->elem))
        return true;

    // "any" type accepts everything (for builtins like print)
    if (to->kind == TypeKind::Basic && to->name == "any")
        return true;

    // Imported exports are runtime values whose concrete type is supplied by
    // the module registry, so an unknown imported value is assignable here.
    if (from->kind == TypeKind::Basic && from->name == "any")
        return true;

    // nil literal (represented as Optional with null elem or a special nil type)
    // For now, nil is represented as Basic "nil" in the AST -- but we handle
    // it in expression type inference instead.

    // Numeric widening: int -> int64, int -> float, int64 -> float
    if (from->kind == TypeKind::Basic && to->kind == TypeKind::Basic) {
        if (from->name == "int" && to->name == "int64")
            return true;
        if (from->name == "int" && to->name == "float")
            return true;
        if (from->name == "int64" && to->name == "float")
            return true;
    }

    // T => T?
    if (to->kind == TypeKind::Optional && isAssignable(from, to->elem))
        return true;

    // nil => T? or nil => Class
    if (from->kind == TypeKind::Basic && from->name == "nil") {
        if (to->kind == TypeKind::Optional)
            return true;
        if (to->kind == TypeKind::Class)
            return true;
        if (to->kind == TypeKind::Func)
            return true;
    }

    return false;
}

// Get a human-readable type name for diagnostics.
core::pmr::String typeName(const TypePtr& t)
{
    auto& res = core::defaultCpuMemoryResource();
    if (!t)
        return core::pmr::String("<unknown>", &res);
    switch (t->kind) {
    case TypeKind::Basic:
    case TypeKind::Class:
        return core::pmr::String(t->name, &res);
    case TypeKind::List:
        return core::pmr::String("[", &res) + typeName(t->elem) + "]";
    case TypeKind::Dict:
        return core::pmr::String("{", &res) + typeName(t->key) + ": " + typeName(t->value) + "}";
    case TypeKind::Optional:
        return typeName(t->elem) + "?";
    case TypeKind::Func: {
        core::pmr::String s("(", &res);
        for (std::size_t i = 0; i < t->params.size(); ++i) {
            if (i > 0)
                s += ", ";
            s += typeName(t->params[i]);
        }
        s += ") -> ";
        s += typeName(t->result);
        return s;
    }
    }
    return core::pmr::String("<unknown>", &res);
}

// --- Symbol table ---

struct Symbol {
    core::pmr::String name{&core::defaultCpuMemoryResource()};
    TypePtr type;
    bool isConst = false;
    bool isParam = false;
    bool isField = false;
    bool isFunc = false;
    bool isClass = false;
    bool isImported = false;  // from-import: symbol imported from a module
    bool isModuleRef = false; // import-as: module reference
    core::pmr::String importModule{&core::defaultCpuMemoryResource()}; // source module
    core::pmr::String importName{&core::defaultCpuMemoryResource()};   // exported name for aliases
    bool isCaptured = false; // set during capture analysis
    SourcePos pos;
};

struct Scope {
    enum class Kind { Module, Function, Block, Class };
    Kind kind = Kind::Block;
    Scope* parent = nullptr;
    core::pmr::Vector<Symbol> symbols{&core::defaultCpuMemoryResource()};

    // For Function scope: the return type of the current function.
    TypePtr returnType;

    // For Class scope: the class name (for self field resolution).
    core::pmr::String className{&core::defaultCpuMemoryResource()};

    Symbol* find(const core::pmr::String& name)
    {
        for (auto& s : symbols)
            if (s.name == name)
                return &s;
        if (parent)
            return parent->find(name);
        return nullptr;
    }

    Symbol* findLocal(const core::pmr::String& name)
    {
        for (auto& s : symbols)
            if (s.name == name)
                return &s;
        return nullptr;
    }

    bool declare(const Symbol& sym)
    {
        if (findLocal(sym.name))
            return false;
        symbols.push_back(sym);
        return true;
    }
};

// --- Sema pass ---

struct Sema {
    std::shared_ptr<Module> module;
    ScriptDiagnostic error;
    Scope* currentScope = nullptr;
    ModuleRegistry* moduleRegistry = nullptr;

    // Stack of scopes (owned via unique_ptr for stable addresses).
    core::pmr::Vector<std::unique_ptr<Scope>> scopes{&core::defaultCpuMemoryResource()};

    void setError(ScriptDiagnosticCode code, core::pmr::String msg, SourcePos pos)
    {
        if (error.code == ScriptDiagnosticCode::None) {
            error = ScriptDiagnostic(code,
                std::move(msg),
                core::pmr::String(pos.file, &core::defaultCpuMemoryResource()),
                pos.line,
                pos.column);
        }
    }

    void setError(ScriptDiagnosticCode code, const char* msg, SourcePos pos)
    {
        setError(code, core::pmr::String(msg, &core::defaultCpuMemoryResource()), pos);
    }

    bool ok() const { return error.code == ScriptDiagnosticCode::None; }

    const Stmt* findClass(const core::pmr::String& name) const
    {
        for (const auto& decl : module->decls) {
            if (decl->kind == StmtKind::ClassDecl && decl->className == name)
                return decl.get();
        }
        return nullptr;
    }

    const Stmt* findConstructor(const Stmt* classDecl) const
    {
        const Stmt* current = classDecl;
        // Inherited script constructors are valid MakeInstance targets. Keep
        // the walk bounded so malformed cyclic inheritance cannot loop here;
        // the frontend later reports the cycle with the same diagnostic.
        std::size_t remaining = module->decls.size() + 1u;
        while (current != nullptr && remaining-- != 0u) {
            for (const auto& method : current->methods) {
                if (method->isConstructor)
                    return method.get();
            }
            current = current->baseClassName.empty() ? nullptr : findClass(current->baseClassName);
        }
        return nullptr;
    }

    const Stmt* findMethod(const Stmt* classDecl, const core::pmr::String& name) const
    {
        for (const Stmt* current = classDecl; current != nullptr;) {
            for (const auto& method : current->methods) {
                if (method->funcName == name)
                    return method.get();
            }
            current = current->baseClassName.empty() ? nullptr : findClass(current->baseClassName);
        }
        return nullptr;
    }

    const Stmt::ClassField* findField(const Stmt* classDecl, const core::pmr::String& name) const
    {
        for (const Stmt* current = classDecl; current != nullptr;) {
            for (const auto& field : current->fields) {
                if (field.name == name)
                    return &field;
            }
            current = current->baseClassName.empty() ? nullptr : findClass(current->baseClassName);
        }
        return nullptr;
    }

    TypePtr classType(const core::pmr::String& name) const
    {
        auto type = makeType(TypeKind::Class);
        type->name = name;
        TypePtr* nextBase = &type->baseType;
        const Stmt* decl = findClass(name);
        std::size_t remaining = module->decls.size();
        while (decl != nullptr && !decl->baseClassName.empty() && remaining-- != 0u) {
            auto base = makeType(TypeKind::Class);
            base->name = decl->baseClassName;
            *nextBase = base;
            nextBase = &base->baseType;
            decl = findClass(base->name);
        }
        return type;
    }

    const arp::reflection::class_info* findImportedType(const core::pmr::String& typeName) const
    {
        if (moduleRegistry == nullptr) {
            return nullptr;
        }

        const auto separator = typeName.find('.');
        if (separator != core::pmr::String::npos) {
            const auto moduleBinding = typeName.substr(0u, separator);
            const auto exportedName = typeName.substr(separator + 1u);
            const Symbol* moduleSymbol = nullptr;
            for (Scope* scope = currentScope; scope != nullptr && moduleSymbol == nullptr;
                scope = scope->parent) {
                for (const auto& symbol : scope->symbols) {
                    if (symbol.name == moduleBinding && symbol.isModuleRef) {
                        moduleSymbol = &symbol;
                        break;
                    }
                }
            }
            if (moduleSymbol == nullptr) {
                return nullptr;
            }
            return moduleRegistry->findType(moduleSymbol->importModule, exportedName);
        }

        for (Scope* scope = currentScope; scope != nullptr; scope = scope->parent) {
            for (const auto& symbol : scope->symbols) {
                if (symbol.name == typeName && symbol.isClass && symbol.isImported) {
                    const auto& exportedName =
                        symbol.importName.empty() ? symbol.name : symbol.importName;
                    return moduleRegistry->findType(symbol.importModule, exportedName);
                }
            }
        }
        return nullptr;
    }

    const arp::reflection::class_info* findUnqualifiedModuleType(
        const core::pmr::String& typeName) const
    {
        if (moduleRegistry == nullptr) {
            return nullptr;
        }
        for (Scope* scope = currentScope; scope != nullptr; scope = scope->parent) {
            for (const auto& symbol : scope->symbols) {
                if (!symbol.isModuleRef) {
                    continue;
                }
                if (const auto* reflectedType =
                        moduleRegistry->findType(symbol.importModule, typeName)) {
                    return reflectedType;
                }
            }
        }
        return nullptr;
    }

    bool validateType(const TypePtr& type, SourcePos pos)
    {
        if (!type || !ok()) {
            return false;
        }
        if (type->kind == TypeKind::List || type->kind == TypeKind::Optional) {
            return validateType(type->elem, pos);
        }
        if (type->kind == TypeKind::Dict) {
            return validateType(type->key, pos) && validateType(type->value, pos);
        }
        if (type->kind != TypeKind::Class || moduleRegistry == nullptr) {
            return true;
        }
        if (findClass(type->name) != nullptr) {
            type->baseType = classType(type->name)->baseType;
            return true;
        }
        if (const auto* reflectedType = findImportedType(type->name)) {
            type->reflectedClass = reflectedType;
            // Keep the spelling for diagnostics, but use the exported
            // reflection identity for all later compatibility checks.
            return true;
        }
        if (type->name.find('.') != core::pmr::String::npos) {
            setError(ScriptDiagnosticCode::UndeclaredName,
                core::pmr::String("reflected module type '", &core::defaultCpuMemoryResource())
                    + type->name + "' is not exported by an imported module",
                pos);
            return false;
        }
        if (findUnqualifiedModuleType(type->name) != nullptr) {
            setError(ScriptDiagnosticCode::UndeclaredName,
                core::pmr::String("module type '", &core::defaultCpuMemoryResource()) + type->name
                    + "' must be qualified with its module name",
                pos);
        }
        return ok();
    }

    TypePtr typeFromReflection(const arp::reflection::typeinfo* typeInfo) const
    {
        if (typeInfo == nullptr || typeInfo->class_() == nullptr) {
            auto anyType = makeType(TypeKind::Basic);
            anyType->name = "any";
            return anyType;
        }
        // RTTI names are ABI-specific (MinGW reports C++ float as "f").
        // Use the reflection identity for primitive signatures instead of
        // exposing the native type name to the script analyzer.
        if (typeInfo->is<bool>()) {
            auto type = makeType(TypeKind::Basic);
            type->name = "bool";
            return type;
        }
        if (typeInfo->is<int>() || typeInfo->is<std::uint32_t>() || typeInfo->is<std::int16_t>()
            || typeInfo->is<std::uint16_t>()) {
            auto type = makeType(TypeKind::Basic);
            type->name = "int";
            return type;
        }
        if (typeInfo->is<std::int64_t>() || typeInfo->is<std::uint64_t>()) {
            auto type = makeType(TypeKind::Basic);
            type->name = "int64";
            return type;
        }
        if (typeInfo->is<float>() || typeInfo->is<double>()) {
            auto type = makeType(TypeKind::Basic);
            type->name = "float";
            return type;
        }
        if (typeInfo->is<Value>()) {
            auto type = makeType(TypeKind::Basic);
            type->name = "any";
            return type;
        }
        if (typeInfo->is<arp::reflection::hlist>()) {
            auto type = makeType(TypeKind::List);
            type->elem = makeType(TypeKind::Basic);
            type->elem->name = "any";
            return type;
        }
        if (typeInfo->is<arp::reflection::hdict>()) {
            auto type = makeType(TypeKind::Dict);
            type->key = makeType(TypeKind::Basic);
            type->value = makeType(TypeKind::Basic);
            type->key->name = "any";
            type->value->name = "any";
            return type;
        }
        if (typeInfo->is<ScriptBytes>()) {
            auto type = makeType(TypeKind::Basic);
            type->name = "bytes";
            return type;
        }
        const auto& typeName = typeInfo->class_()->name();
        // Some MinGW reflection records expose primitive RTTI names instead
        // of their source spelling when the type crosses the plugin boundary.
        if (typeName == "b") {
            auto type = makeType(TypeKind::Basic);
            type->name = "bool";
            return type;
        }
        if (typeName == "i") {
            auto type = makeType(TypeKind::Basic);
            type->name = "int";
            return type;
        }
        if (typeName == "f" || typeName == "d") {
            auto type = makeType(TypeKind::Basic);
            type->name = "float";
            return type;
        }
        if (typeName == "list") {
            auto list = makeType(TypeKind::List);
            list->elem = makeType(TypeKind::Basic);
            list->elem->name = "any";
            return list;
        }
        if (typeName == "dict") {
            auto dict = makeType(TypeKind::Dict);
            dict->key = makeType(TypeKind::Basic);
            dict->key->name = "any";
            dict->value = makeType(TypeKind::Basic);
            dict->value->name = "any";
            return dict;
        }
        const bool basic = typeName == "bool" || typeName == "int" || typeName == "int64"
            || typeName == "float" || typeName == "str" || typeName == "string"
            || typeName == "bytes" || typeName == "void";
        auto type = makeType(basic ? TypeKind::Basic : TypeKind::Class);
        type->name = typeName == "str" ? "string" : typeName;
        if (!basic) {
            type->reflectedClass = typeInfo->class_();
        }
        return type;
    }

    TypePtr functionTypeFromReflection(const arp::reflection::function* reflected) const
    {
        if (reflected == nullptr) {
            return nullptr;
        }
        auto functionType = makeType(TypeKind::Func);
        const auto parameters = reflected->params_info();
        functionType->params.reserve(parameters.size());
        for (const auto& parameter : parameters) {
            functionType->params.push_back(typeFromReflection(parameter.ti));
        }
        functionType->result = typeFromReflection(reflected->return_type());
        const auto defaultCount = reflected->default_argument_count();
        functionType->hasDefaultParams = defaultCount != 0u;
        if (defaultCount < functionType->params.size()) {
            functionType->requiredParams = functionType->params.size() - defaultCount;
        }
        return functionType;
    }

    TypePtr functionTypeFromScript(const ScriptFunctionContract* contract) const
    {
        if (contract == nullptr) {
            return nullptr;
        }
        auto functionType = makeType(TypeKind::Func);
        functionType->params = contract->params;
        functionType->result = contract->result;
        functionType->isVariadic = contract->isVariadic;
        return functionType;
    }

    TypePtr checkFunctionCall(const TypePtr& functionType,
        const core::pmr::Vector<ExprPtr>& arguments,
        SourcePos pos,
        bool allowRangeOverload = false)
    {
        if (!functionType || functionType->kind != TypeKind::Func) {
            setError(ScriptDiagnosticCode::NotCallable, "value is not callable", pos);
            return nullptr;
        }
        const int expected = static_cast<int>(functionType->params.size());
        const int actual = static_cast<int>(arguments.size());
        const bool rangeOverload = allowRangeOverload && (actual == 1 || actual == 2);
        const int required = functionType->hasDefaultParams
            ? static_cast<int>(functionType->requiredParams)
            : (functionType->isVariadic ? expected - 1 : expected);
        const bool badCount = functionType->isVariadic
            ? actual < required
            : (functionType->hasDefaultParams ? actual < required || actual > expected
                                              : actual != expected);
        if (!rangeOverload && badCount) {
            setError(ScriptDiagnosticCode::WrongArgCount,
                core::pmr::String(
                    "wrong argument count: expected ", &core::defaultCpuMemoryResource())
                    + toStr(expected) + ", got " + toStr(actual),
                pos);
            return nullptr;
        }
        for (std::size_t i = 0; i < arguments.size(); ++i) {
            const auto argumentType = inferExpr(arguments[i]);
            if (!argumentType) {
                return nullptr;
            }
            if (allowRangeOverload) {
                if (argumentType->kind != TypeKind::Basic || argumentType->name != "int") {
                    setError(ScriptDiagnosticCode::TypeMismatch,
                        "range arguments must be int",
                        arguments[i]->pos);
                    return nullptr;
                }
                continue;
            }
            TypePtr expectedType;
            if (functionType->isVariadic && i >= functionType->params.size() - 1u) {
                expectedType = functionType->params.back()->elem;
            } else if (i < functionType->params.size()) {
                expectedType = functionType->params[i];
            }
            if (expectedType && !isAssignable(argumentType, expectedType)) {
                setError(ScriptDiagnosticCode::TypeMismatch,
                    core::pmr::String("argument ", &core::defaultCpuMemoryResource())
                        + toStr(static_cast<long long>(i + 1u)) + " type mismatch: expected "
                        + typeName(expectedType) + ", got " + typeName(argumentType),
                    arguments[i]->pos);
                return nullptr;
            }
        }
        return functionType->result;
    }

    Scope* pushScope(Scope::Kind kind)
    {
        auto s = std::make_unique<Scope>();
        s->kind = kind;
        s->parent = currentScope;
        Scope* ptr = s.get();
        scopes.push_back(std::move(s));
        currentScope = ptr;
        return ptr;
    }

    void popScope()
    {
        if (currentScope)
            currentScope = currentScope->parent;
    }

    // --- Type inference for expressions ---
    // Returns the type of an expression, or nullptr on error.
    TypePtr inferExpr(const ExprPtr& expr);
    TypePtr inferExprImpl(const ExprPtr& expr);

    // --- Statement analysis ---
    void analyzeStmt(const StmtPtr& stmt);
    void analyzeVarDecl(const StmtPtr& stmt);
    void analyzeFuncDecl(const StmtPtr& stmt);
    void analyzeClassDecl(const StmtPtr& stmt);
    void analyzeIf(const StmtPtr& stmt);
    void analyzeWhile(const StmtPtr& stmt);
    void analyzeFor(const StmtPtr& stmt);
    void analyzeTry(const StmtPtr& stmt);
    void analyzeRaise(const StmtPtr& stmt);
    void analyzeYield(const StmtPtr& stmt);
    void analyzeReturn(const StmtPtr& stmt);
    void analyzeAssign(const StmtPtr& stmt);
    void analyzeImport(const StmtPtr& stmt);
    void analyzeBlock(const core::pmr::Vector<StmtPtr>& body);
};

// Infer the type of an expression in the current scope.
TypePtr Sema::inferExpr(const ExprPtr& expr)
{
    if (!expr)
        return nullptr;
    if (expr->inferredType)
        return expr->inferredType;
    expr->inferredType = inferExprImpl(expr);
    return expr->inferredType;
}

TypePtr Sema::inferExprImpl(const ExprPtr& expr)
{
    if (!expr)
        return nullptr;

    switch (expr->kind) {
    case ExprKind::IntLit: {
        auto t = makeType(TypeKind::Basic);
        t->name = "int";
        return t;
    }
    case ExprKind::Int64Lit: {
        auto t = makeType(TypeKind::Basic);
        t->name = "int64";
        return t;
    }
    case ExprKind::FloatLit: {
        auto t = makeType(TypeKind::Basic);
        t->name = "float";
        return t;
    }
    case ExprKind::StringLit: {
        auto t = makeType(TypeKind::Basic);
        t->name = "string";
        return t;
    }
    case ExprKind::BoolLit: {
        auto t = makeType(TypeKind::Basic);
        t->name = "bool";
        return t;
    }
    case ExprKind::NilLit: {
        auto t = makeType(TypeKind::Basic);
        t->name = "nil";
        return t;
    }
    case ExprKind::Ident: {
        Symbol* sym = currentScope ? currentScope->find(expr->name) : nullptr;
        if (!sym) {
            setError(ScriptDiagnosticCode::UndeclaredName,
                core::pmr::String("undeclared name: ", &core::defaultCpuMemoryResource())
                    + expr->name,
                expr->pos);
            return nullptr;
        }
        return sym->type;
    }
    case ExprKind::Binary: {
        TypePtr lt = inferExpr(expr->left);
        TypePtr rt = inferExpr(expr->right);
        if (!lt || !rt)
            return nullptr;

        // Comparison/logical operators return bool
        const auto& op = expr->op;
        // Comparison/logical operators return bool
        if (op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">="
            || op == "and" || op == "or") {
            auto t = makeType(TypeKind::Basic);
            t->name = "bool";
            return t;
        }
        // Power and floor division have an explicit numeric contract. Keep
        // them out of string/bool/reflection dispatch so both backends use the
        // same deterministic implementation.
        if (op == "**" || op == "//") {
            const auto numeric = [](const TypePtr& type) {
                return type && type->kind == TypeKind::Basic
                    && (type->name == "int" || type->name == "int64" || type->name == "float");
            };
            if (!numeric(lt) || !numeric(rt)) {
                setError(ScriptDiagnosticCode::TypeMismatch,
                    core::pmr::String("operator '", &core::defaultCpuMemoryResource()) + op
                        + " expects numeric operands",
                    expr->pos);
                return nullptr;
            }
        }
        // String concatenation: string + string = string
        if (op == "+" && lt->kind == TypeKind::Basic && rt->kind == TypeKind::Basic
            && lt->name == "string" && rt->name == "string") {
            auto result = makeType(TypeKind::Basic);
            result->name = "string";
            return result;
        }
        // For arithmetic/bitwise, result type = wider of the two operands
        // int + int = int, int + int64 = int64, int/int64 + float = float
        if (lt->kind == TypeKind::Basic && rt->kind == TypeKind::Basic) {
            auto result = makeType(TypeKind::Basic);
            if (lt->name == "float" || rt->name == "float")
                result->name = "float";
            else if (lt->name == "int64" || rt->name == "int64")
                result->name = "int64";
            else
                result->name = "int";
            return result;
        }
        // If either is a list and op is +, result is list (Phase 4: skip)
        setError(ScriptDiagnosticCode::TypeMismatch,
            core::pmr::String("cannot apply operator '", &core::defaultCpuMemoryResource()) + op
                + "' to types " + typeName(lt) + " and " + typeName(rt),
            expr->pos);
        return nullptr;
    }
    case ExprKind::Unary: {
        TypePtr ot = inferExpr(expr->left);
        if (!ot)
            return nullptr;
        if (expr->op == "not") {
            auto t = makeType(TypeKind::Basic);
            t->name = "bool";
            return t;
        }
        // -x, +x, ~x: same type as operand
        return ot;
    }
    case ExprKind::Await:
        return inferExpr(expr->left);
    case ExprKind::Call: {
        // Resolve the callee itself first so the compiler can consume the
        // function type for indirect calls and emit numeric widening at the
        // call boundary. The specialized branches below still perform their
        // receiver, arity, and argument checks.
        if (!inferExpr(expr->left))
            return nullptr;
        // Native calls with dynamically boxed results still need typed argument
        // ASTs for constructor lowering and nested call provenance.
        for (const auto& argument : expr->args)
            if (!inferExpr(argument))
                return nullptr;
        if (expr->left->kind == ExprKind::Ident) {
            Symbol* sym = currentScope ? currentScope->find(expr->left->name) : nullptr;
            if (!sym) {
                setError(ScriptDiagnosticCode::UndeclaredName,
                    core::pmr::String("undeclared function: ", &core::defaultCpuMemoryResource())
                        + expr->left->name,
                    expr->pos);
                return nullptr;
            }
            if (sym->isClass) {
                if (sym->isImported && sym->type && sym->type->reflectedClass != nullptr) {
                    const auto* reflectedClass = sym->type->reflectedClass;
                    const Value constructor = reflectedClass->get_field("__new__");
                    if (constructor.is<arp::reflection::function>()) {
                        if (!checkFunctionCall(functionTypeFromReflection(
                                                   &constructor.cast<arp::reflection::function&>()),
                                expr->args,
                                expr->pos))
                            return nullptr;
                        // __new__ boxes the constructed value as object. Its
                        // C++ carrier return type is not the language result.
                        // The imported registration identifies that result.
                        return sym->type;
                    }
                    return sym->type;
                }
                const Stmt* classDecl = findClass(expr->left->name);
                if (classDecl == nullptr) {
                    setError(ScriptDiagnosticCode::UndeclaredName,
                        core::pmr::String(
                            "class schema not found: ", &core::defaultCpuMemoryResource())
                            + expr->left->name,
                        expr->pos);
                    return nullptr;
                }
                const Stmt* initializer = findConstructor(classDecl);
                const int expected = initializer && initializer->paramTypes.size() > 0
                    ? static_cast<int>(initializer->paramTypes.size() - 1)
                    : 0;
                if (static_cast<int>(expr->args.size()) != expected) {
                    setError(ScriptDiagnosticCode::WrongArgCount,
                        core::pmr::String(
                            "class constructor expects ", &core::defaultCpuMemoryResource())
                            + toStr(expected) + ", got " + toStr(expr->args.size()),
                        expr->pos);
                    return nullptr;
                }
                for (std::size_t i = 0; i < expr->args.size(); ++i) {
                    TypePtr argType = inferExpr(expr->args[i]);
                    if (!argType)
                        return nullptr;
                    if (initializer == nullptr || i + 1 >= initializer->paramTypes.size()
                        || !isAssignable(argType, initializer->paramTypes[i + 1])) {
                        setError(ScriptDiagnosticCode::TypeMismatch,
                            core::pmr::String(
                                "constructor argument ", &core::defaultCpuMemoryResource())
                                + toStr(i + 1) + " type mismatch",
                            expr->args[i]->pos);
                        return nullptr;
                    }
                }
                return sym->type;
            }
            if (sym->isImported) {
                auto dynamicFunctionType = [&]() {
                    auto functionType = makeType(TypeKind::Func);
                    functionType->result = makeType(TypeKind::Basic);
                    functionType->result->name = "any";
                    functionType->params.resize(expr->args.size(), functionType->result);
                    return functionType;
                };
                auto metadataHostFunctionType = [](int fixedArity) {
                    auto functionType = makeType(TypeKind::Func);
                    functionType->result = makeType(TypeKind::Basic);
                    functionType->result->name = "any";
                    functionType->params.resize(
                        static_cast<std::size_t>(fixedArity), functionType->result);
                    return functionType;
                };
                if (moduleRegistry == nullptr)
                    return checkFunctionCall(dynamicFunctionType(), expr->args, expr->pos);
                const auto exportedName =
                    sym->importName.empty() ? expr->left->name : sym->importName;
                const auto* reflected =
                    moduleRegistry->findFunction(sym->importModule, exportedName);
                if (reflected != nullptr) {
                    return checkFunctionCall(
                        functionTypeFromReflection(reflected), expr->args, expr->pos);
                }
                const auto* scriptFunction =
                    moduleRegistry->findScriptFunction(sym->importModule, exportedName);
                if (scriptFunction == nullptr) {
                    const auto* hostContract =
                        moduleRegistry->findHostCallableContract(sym->importModule, exportedName);
                    if (hostContract != nullptr && hostContract->valid()) {
                        return checkFunctionCall(metadataHostFunctionType(hostContract->fixedArity),
                            expr->args,
                            expr->pos);
                    }
                    const auto* exportValue =
                        moduleRegistry->findExport(sym->importModule, exportedName);
                    if (exportValue != nullptr && exportValue->is<arp::reflection::property>())
                        return checkFunctionCall(dynamicFunctionType(), expr->args, expr->pos);
                    setError(ScriptDiagnosticCode::TypeMismatch,
                        core::pmr::String("imported function '", &core::defaultCpuMemoryResource())
                            + sym->importModule + "." + exportedName
                            + " has no reflection signature",
                        expr->pos);
                    return nullptr;
                }
                return checkFunctionCall(
                    functionTypeFromScript(scriptFunction), expr->args, expr->pos);
            }
            if (!sym->isFunc && sym->type && sym->type->kind != TypeKind::Func) {
                setError(ScriptDiagnosticCode::NotCallable,
                    core::pmr::String("'", &core::defaultCpuMemoryResource()) + expr->left->name
                        + "' is not callable",
                    expr->pos);
                return nullptr;
            }
            // Check arg count + types
            TypePtr funcType = sym->type;
            if (funcType && funcType->kind == TypeKind::Func) {
                if (expr->left->name == "len") {
                    if (expr->args.size() != 1u) {
                        setError(ScriptDiagnosticCode::WrongArgCount,
                            "len expects one argument",
                            expr->pos);
                        return nullptr;
                    }
                    const auto argumentType = inferExpr(expr->args.front());
                    if (!argumentType) {
                        return nullptr;
                    }
                    const bool sized = (argumentType->kind == TypeKind::List
                                           || argumentType->kind == TypeKind::Dict)
                        || (argumentType->kind == TypeKind::Basic
                            && (argumentType->name == "string" || argumentType->name == "bytes"));
                    if (!sized) {
                        setError(ScriptDiagnosticCode::TypeMismatch,
                            "len expects string, bytes, list, or dict",
                            expr->args.front()->pos);
                        return nullptr;
                    }
                    return funcType->result;
                }
                if (expr->left->name == "append") {
                    if (expr->args.size() != 2u) {
                        setError(ScriptDiagnosticCode::WrongArgCount,
                            "append expects a list and one value",
                            expr->pos);
                        return nullptr;
                    }
                    const auto listType = inferExpr(expr->args[0]);
                    const auto valueType = inferExpr(expr->args[1]);
                    if (!listType || !valueType)
                        return nullptr;
                    if (listType->kind != TypeKind::List) {
                        setError(ScriptDiagnosticCode::TypeMismatch,
                            "append expects a list as its first argument",
                            expr->args[0]->pos);
                        return nullptr;
                    }
                    if (listType->elem && !isAssignable(valueType, listType->elem)) {
                        setError(ScriptDiagnosticCode::TypeMismatch,
                            "append value is incompatible with the list element type",
                            expr->args[1]->pos);
                        return nullptr;
                    }
                    return listType;
                }
                if (expr->left->name == "delete") {
                    if (expr->args.size() != 2u) {
                        setError(ScriptDiagnosticCode::WrongArgCount,
                            "delete expects a dict and one key",
                            expr->pos);
                        return nullptr;
                    }
                    const auto dictType = inferExpr(expr->args[0]);
                    const auto keyType = inferExpr(expr->args[1]);
                    if (!dictType || !keyType)
                        return nullptr;
                    if (dictType->kind != TypeKind::Dict) {
                        setError(ScriptDiagnosticCode::TypeMismatch,
                            "delete expects a dict as its first argument",
                            expr->args[0]->pos);
                        return nullptr;
                    }
                    if (dictType->key && !isAssignable(keyType, dictType->key)) {
                        setError(ScriptDiagnosticCode::TypeMismatch,
                            "delete key is incompatible with the dict key type",
                            expr->args[1]->pos);
                        return nullptr;
                    }
                    return funcType->result;
                }
                if (expr->left->name == "clear") {
                    if (expr->args.size() != 1u) {
                        setError(ScriptDiagnosticCode::WrongArgCount,
                            "clear expects one container",
                            expr->pos);
                        return nullptr;
                    }
                    const auto containerType = inferExpr(expr->args[0]);
                    if (!containerType)
                        return nullptr;
                    if (containerType->kind != TypeKind::List
                        && containerType->kind != TypeKind::Dict) {
                        setError(ScriptDiagnosticCode::TypeMismatch,
                            "clear expects a list or dict",
                            expr->args[0]->pos);
                        return nullptr;
                    }
                    return funcType->result;
                }
                return checkFunctionCall(
                    funcType, expr->args, expr->pos, expr->left->name == "range");
            }
            // Built-in functions (range, print, etc.) -- for Phase 4, just return int
            // TODO: properly resolve builtins
            return funcType ? funcType->result : nullptr;
        }
        if (expr->left->kind == ExprKind::Field) {
            const auto calleeType = inferExpr(expr->left);
            if (!calleeType) {
                return nullptr;
            }
            if (calleeType->kind == TypeKind::Func) {
                return checkFunctionCall(calleeType, expr->args, expr->pos);
            }
            TypePtr receiverType = inferExpr(expr->left->left);
            if (!receiverType)
                return nullptr;
            if (receiverType->kind == TypeKind::Class) {
                const Stmt* classDecl = findClass(receiverType->name);
                if (classDecl != nullptr) {
                    const Stmt* method = findMethod(classDecl, expr->left->name);
                    if (method != nullptr) {
                        const std::size_t expected =
                            method->paramTypes.empty() ? 0 : method->paramTypes.size() - 1;
                        if (expr->args.size() != expected) {
                            setError(ScriptDiagnosticCode::WrongArgCount,
                                core::pmr::String("method ", &core::defaultCpuMemoryResource())
                                    + method->funcName + " expects " + toStr(expected) + ", got "
                                    + toStr(expr->args.size()),
                                expr->pos);
                            return nullptr;
                        }
                        for (std::size_t i = 0; i < expr->args.size(); ++i) {
                            TypePtr argType = inferExpr(expr->args[i]);
                            if (!argType)
                                return nullptr;
                            if (!isAssignable(argType, method->paramTypes[i + 1])) {
                                setError(ScriptDiagnosticCode::TypeMismatch,
                                    core::pmr::String(
                                        "method argument ", &core::defaultCpuMemoryResource())
                                        + toStr(i + 1) + " type mismatch",
                                    expr->args[i]->pos);
                                return nullptr;
                            }
                        }
                        return method->retType;
                    }
                }
            }
        }
        TypePtr calleeType = inferExpr(expr->left);
        return calleeType;
    }
    case ExprKind::Index: {
        TypePtr baseType = inferExpr(expr->left);
        if (!baseType)
            return nullptr;
        if (baseType->kind == TypeKind::List)
            return baseType->elem;
        if (baseType->kind == TypeKind::Dict)
            return baseType->value;
        if (baseType->kind == TypeKind::Basic && baseType->name == "bytes") {
            auto indexType = makeType(TypeKind::Basic);
            indexType->name = "int";
            return indexType;
        }
        setError(ScriptDiagnosticCode::NotIndexable,
            core::pmr::String("type '", &core::defaultCpuMemoryResource()) + typeName(baseType)
                + "' is not indexable",
            expr->pos);
        return nullptr;
    }
    case ExprKind::Field: {
        if (expr->left && expr->left->kind == ExprKind::Ident) {
            const auto* moduleSymbol =
                currentScope ? currentScope->find(expr->left->name) : nullptr;
            if (moduleSymbol != nullptr && moduleSymbol->isModuleRef && moduleRegistry != nullptr) {
                if (const auto* reflected =
                        moduleRegistry->findFunction(moduleSymbol->importModule, expr->name)) {
                    return functionTypeFromReflection(reflected);
                }
                if (const auto* scriptFunction = moduleRegistry->findScriptFunction(
                        moduleSymbol->importModule, expr->name)) {
                    return functionTypeFromScript(scriptFunction);
                }
                if (const auto* hostContract = moduleRegistry->findHostCallableContract(
                        moduleSymbol->importModule, expr->name)) {
                    if (hostContract->valid()) {
                        auto functionType = makeType(TypeKind::Func);
                        functionType->result = makeType(TypeKind::Basic);
                        functionType->result->name = "any";
                        functionType->params.resize(
                            static_cast<std::size_t>(hostContract->fixedArity),
                            functionType->result);
                        return functionType;
                    }
                }
                if (const auto* reflectedType =
                        moduleRegistry->findType(moduleSymbol->importModule, expr->name)) {
                    auto type = makeType(TypeKind::Class);
                    type->name = expr->name;
                    type->reflectedClass = reflectedType;
                    return type;
                }
                if (!moduleRegistry->hasExport(moduleSymbol->importModule, expr->name)) {
                    setError(ScriptDiagnosticCode::UndeclaredName,
                        core::pmr::String("module '", &core::defaultCpuMemoryResource())
                            + moduleSymbol->importModule + "' has no export '" + expr->name + "'",
                        expr->pos);
                    return nullptr;
                }
            }
        }
        TypePtr baseType = inferExpr(expr->left);
        if (!baseType)
            return nullptr;
        if (baseType->kind == TypeKind::Class) {
            const Stmt* classDecl = findClass(baseType->name);
            if (classDecl != nullptr) {
                const auto* field = findField(classDecl, expr->name);
                if (field != nullptr)
                    return field->type;
                const auto* method = findMethod(classDecl, expr->name);
                if (method != nullptr) {
                    auto functionType = makeType(TypeKind::Func);
                    for (std::size_t i = 1; i < method->paramTypes.size(); ++i)
                        functionType->params.push_back(method->paramTypes[i]);
                    functionType->result = method->retType;
                    return functionType;
                }
            }
            const auto* reflectedType = findImportedType(baseType->name);
            if (reflectedType != nullptr) {
                const auto fields = reflectedType->get_all_properties();
                const auto field = fields.find(expr->name);
                if (field != fields.end() && field->second.is_property()) {
                    return typeFromReflection(
                        field->second.cast<arp::reflection::property&>().type());
                }
            }
        }
        auto anyType = makeType(TypeKind::Basic);
        anyType->name = "any";
        return anyType;
    }
    case ExprKind::ListLit: {
        auto t = makeType(TypeKind::List);
        if (expr->args.empty()) {
            t->elem = makeType(TypeKind::Basic);
            t->elem->name = "int"; // default for empty list
        } else {
            t->elem = inferExpr(expr->args[0]);
        }
        return t;
    }
    case ExprKind::DictLit: {
        auto t = makeType(TypeKind::Dict);
        if (expr->dictKeys.empty()) {
            t->key = makeType(TypeKind::Basic);
            t->key->name = "string";
            t->value = makeType(TypeKind::Basic);
            t->value->name = "int";
        } else {
            t->key = inferExpr(expr->dictKeys[0]);
            t->value = inferExpr(expr->dictValues[0]);
        }
        return t;
    }
    case ExprKind::AsCast: {
        if (!expr->castType || expr->castType->kind != TypeKind::Basic
            || (expr->castType->name != "int" && expr->castType->name != "int64"
                && expr->castType->name != "float" && expr->castType->name != "string"
                && expr->castType->name != "bool" && expr->castType->name != "bytes")) {
            setError(ScriptDiagnosticCode::TypeMismatch,
                "as supports int, int64, float, string, bool, and bytes",
                expr->pos);
            return nullptr;
        }
        if (!inferExpr(expr->left))
            return nullptr;
        return expr->castType;
    }
    case ExprKind::Lambda: {
        auto functionType = makeType(TypeKind::Func);
        for (const auto& parameterType : expr->paramTypes)
            functionType->params.push_back(parameterType);
        functionType->result = expr->retType;

        pushScope(Scope::Kind::Function);
        for (std::size_t i = 0; i < expr->paramNames.size(); ++i) {
            Symbol parameter;
            parameter.name = expr->paramNames[i];
            parameter.type = expr->paramTypes[i];
            parameter.isParam = true;
            parameter.pos = expr->pos;
            if (!currentScope->declare(parameter)) {
                setError(
                    ScriptDiagnosticCode::RedeclaredName, "redeclared lambda parameter", expr->pos);
                popScope();
                return nullptr;
            }
        }
        TypePtr bodyType = inferExpr(expr->left);
        if (!bodyType) {
            popScope();
            return nullptr;
        }
        if (!isAssignable(bodyType, expr->retType)) {
            setError(ScriptDiagnosticCode::TypeMismatch,
                core::pmr::String(
                    "lambda return type mismatch: expected ", &core::defaultCpuMemoryResource())
                    + typeName(expr->retType) + ", got " + typeName(bodyType),
                expr->pos);
            popScope();
            return nullptr;
        }
        popScope();
        return functionType;
    }
    case ExprKind::Assign:
    case ExprKind::Selector:
        // Not fully supported in Phase 4 sema
        return makeType(TypeKind::Basic);
    }
    return nullptr;
}

void Sema::analyzeVarDecl(const StmtPtr& stmt)
{
    // var/const name: Type = expr
    if (!validateType(stmt->varType, stmt->varType->pos)) {
        return;
    }
    Symbol sym;
    sym.name = stmt->varName;
    sym.type = stmt->varType;
    sym.isConst = stmt->isConst;
    sym.pos = stmt->pos;

    if (!currentScope->declare(sym)) {
        setError(ScriptDiagnosticCode::RedeclaredName,
            core::pmr::String("redeclared name: ", &core::defaultCpuMemoryResource())
                + stmt->varName,
            stmt->pos);
        return;
    }

    // Check initializer type compatibility
    if (stmt->expr) {
        if ((stmt->varType->kind == TypeKind::List && stmt->expr->kind == ExprKind::ListLit
                && stmt->expr->args.empty())
            || (stmt->varType->kind == TypeKind::Dict && stmt->expr->kind == ExprKind::DictLit
                && stmt->expr->dictKeys.empty()))
            stmt->expr->inferredType = stmt->varType;
        TypePtr initType = inferExpr(stmt->expr);
        if (!initType)
            return;
        if (!isAssignable(initType, stmt->varType)) {
            setError(ScriptDiagnosticCode::TypeMismatch,
                core::pmr::String("cannot assign ", &core::defaultCpuMemoryResource())
                    + typeName(initType) + " to " + typeName(stmt->varType),
                stmt->expr->pos);
            return;
        }
    }
}

void Sema::analyzeFuncDecl(const StmtPtr& stmt)
{
    if (!validateType(stmt->retType, stmt->retType ? stmt->retType->pos : stmt->pos)) {
        return;
    }
    for (const auto& paramType : stmt->paramTypes) {
        if (!validateType(paramType, paramType ? paramType->pos : stmt->pos)) {
            return;
        }
    }
    stmt->isGenerator = containsYield(stmt->body);
    if (stmt->variadicParam
        && stmt->variadicParamIndex != static_cast<int>(stmt->paramNames.size()) - 1) {
        setError(ScriptDiagnosticCode::WrongArgCount, "variadic parameter must be last", stmt->pos);
        return;
    }
    // Build function type
    auto funcType = functionTypeFor(stmt);

    // Declare function if not already declared (top-level functions are
    // pre-declared in the forward pass; methods inside classes are not).
    Symbol* existing = currentScope ? currentScope->findLocal(stmt->funcName) : nullptr;
    if (!existing) {
        Symbol sym;
        sym.name = stmt->funcName;
        sym.type = funcType;
        sym.isFunc = true;
        sym.pos = stmt->pos;
        if (!currentScope->declare(sym)) {
            setError(ScriptDiagnosticCode::RedeclaredName,
                core::pmr::String("redeclared name: ", &core::defaultCpuMemoryResource())
                    + stmt->funcName,
                stmt->pos);
            return;
        }
    }

    // Enter function scope
    Scope* funcScope = pushScope(Scope::Kind::Function);
    funcScope->returnType = stmt->retType;

    // Declare parameters
    for (std::size_t i = 0; i < stmt->paramNames.size(); ++i) {
        Symbol paramSym;
        paramSym.name = stmt->paramNames[i];
        if (stmt->variadicParam && static_cast<int>(i) == stmt->variadicParamIndex) {
            paramSym.type = makeType(TypeKind::List);
            paramSym.type->elem = stmt->paramTypes[i];
        } else {
            paramSym.type = stmt->paramTypes[i];
        }
        paramSym.isParam = true;
        paramSym.pos = stmt->pos;
        if (!funcScope->declare(paramSym)) {
            setError(ScriptDiagnosticCode::RedeclaredName,
                core::pmr::String("redeclared parameter: ", &core::defaultCpuMemoryResource())
                    + stmt->paramNames[i],
                stmt->pos);
            return;
        }
    }

    // Analyze body
    analyzeBlock(stmt->body);

    popScope();
}

void Sema::analyzeClassDecl(const StmtPtr& stmt)
{
    // Class is already declared in the forward pass; enter class scope.
    Scope* classScope = pushScope(Scope::Kind::Class);
    classScope->className = stmt->className;

    // Declare fields
    for (auto& field : stmt->fields) {
        if (!validateType(field.type, field.pos)) {
            return;
        }
        Symbol fsym;
        fsym.name = field.name;
        fsym.type = field.type;
        fsym.isField = true;
        fsym.pos = field.pos;
        if (!classScope->declare(fsym)) {
            setError(ScriptDiagnosticCode::RedeclaredName,
                core::pmr::String("redeclared field: ", &core::defaultCpuMemoryResource())
                    + field.name,
                field.pos);
            return;
        }
    }

    // Analyze methods
    for (auto& method : stmt->methods) {
        if (!method->paramNames.empty() && method->paramNames[0] == "self") {
            auto selfType = makeType(TypeKind::Class);
            selfType->name = stmt->className;
            selfType->baseType = classType(stmt->className)->baseType;
            method->paramTypes[0] = selfType;
        }
        analyzeFuncDecl(method);
    }

    popScope();
}

void Sema::analyzeIf(const StmtPtr& stmt)
{
    TypePtr condType = inferExpr(stmt->cond);
    if (condType && !ok())
        return;
    // TODO: check cond is bool

    pushScope(Scope::Kind::Block);
    analyzeBlock(stmt->body);
    popScope();

    for (std::size_t i = 0; i < stmt->elifConds.size(); ++i) {
        inferExpr(stmt->elifConds[i]);
        pushScope(Scope::Kind::Block);
        analyzeBlock(stmt->elifBodies[i]);
        popScope();
    }

    if (!stmt->elseBody.empty()) {
        pushScope(Scope::Kind::Block);
        analyzeBlock(stmt->elseBody);
        popScope();
    }
}

void Sema::analyzeWhile(const StmtPtr& stmt)
{
    inferExpr(stmt->cond);
    pushScope(Scope::Kind::Block);
    analyzeBlock(stmt->body);
    popScope();
}

void Sema::analyzeFor(const StmtPtr& stmt)
{
    TypePtr iterableType = inferExpr(stmt->iterable);
    if (!iterableType)
        return;

    pushScope(Scope::Kind::Block);
    // Declare iterator variable
    Symbol iterSym;
    iterSym.name = stmt->iterName;
    if (stmt->iterType) {
        if (!validateType(stmt->iterType, stmt->iterType->pos)) {
            popScope();
            return;
        }
        iterSym.type = stmt->iterType;
    } else {
        // Infer from iterable: if list [T], iter is T
        if (iterableType->kind == TypeKind::List)
            iterSym.type = iterableType->elem;
        else if (iterableType->kind == TypeKind::Dict)
            iterSym.type = iterableType->value;
        else if (iterableType->kind == TypeKind::Basic && iterableType->name == "bytes") {
            iterSym.type = makeType(TypeKind::Basic);
            iterSym.type->name = "int";
        } else {
            TypePtr generatorElement;
            if (stmt->iterable && stmt->iterable->kind == ExprKind::Call && stmt->iterable->left
                && stmt->iterable->left->kind == ExprKind::Ident) {
                Symbol* callee = currentScope->find(stmt->iterable->left->name);
                if (callee && callee->type && callee->type->kind == TypeKind::Func
                    && callee->type->isGenerator) {
                    generatorElement = callee->type->result;
                }
            }
            iterSym.type = generatorElement ? generatorElement : makeType(TypeKind::Basic);
            if (!generatorElement)
                iterSym.type->name = "any";
        }
    }
    iterSym.pos = stmt->pos;
    currentScope->declare(iterSym);

    if (!stmt->iterKeyName.empty()) {
        Symbol keySym;
        keySym.name = stmt->iterKeyName;
        if (stmt->keyType) {
            if (!validateType(stmt->keyType, stmt->keyType->pos)) {
                popScope();
                return;
            }
            keySym.type = stmt->keyType;
        } else if (iterableType->kind == TypeKind::List
            || (iterableType->kind == TypeKind::Basic && iterableType->name == "bytes")) {
            keySym.type = makeType(TypeKind::Basic);
            keySym.type->name = "int";
        } else if (iterableType->kind == TypeKind::Dict) {
            keySym.type = iterableType->key;
        } else {
            keySym.type = makeType(TypeKind::Basic);
            keySym.type->name = "any";
        }
        keySym.pos = stmt->pos;
        currentScope->declare(keySym);
    }

    analyzeBlock(stmt->body);
    popScope();
}

void Sema::analyzeTry(const StmtPtr& stmt)
{
    if (stmt->exceptClauses.empty()) {
        setError(ScriptDiagnosticCode::UnexpectedToken, "try requires an except clause", stmt->pos);
        return;
    }
    pushScope(Scope::Kind::Block);
    analyzeBlock(stmt->tryBody);
    popScope();

    bool catchAllSeen = false;
    for (std::size_t index = 0; index < stmt->exceptClauses.size(); ++index) {
        const auto& clause = stmt->exceptClauses[index];
        if (clause.errorName.empty()) {
            if (catchAllSeen || index + 1u != stmt->exceptClauses.size()) {
                setError(ScriptDiagnosticCode::UnexpectedToken,
                    "catch-all except must be the final clause",
                    stmt->pos);
                return;
            }
            catchAllSeen = true;
        } else {
            ScriptDiagnosticCode filter = ScriptDiagnosticCode::None;
            if (!scriptDiagnosticCodeFromName(clause.errorName, filter)) {
                setError(ScriptDiagnosticCode::UnexpectedToken,
                    "unknown typed except diagnostic; use a ScriptDiagnosticCode name",
                    stmt->pos);
                return;
            }
            for (std::size_t previous = 0; previous < index; ++previous) {
                if (stmt->exceptClauses[previous].errorName == clause.errorName) {
                    setError(ScriptDiagnosticCode::UnexpectedToken,
                        "duplicate typed except diagnostic",
                        stmt->pos);
                    return;
                }
            }
        }
        pushScope(Scope::Kind::Block);
        if (!clause.varName.empty()) {
            Symbol errorSymbol;
            errorSymbol.name = clause.varName;
            errorSymbol.type = makeType(TypeKind::Basic);
            errorSymbol.type->name = "string";
            errorSymbol.pos = stmt->pos;
            currentScope->declare(errorSymbol);
        }
        analyzeBlock(clause.body);
        popScope();
    }
}

void Sema::analyzeRaise(const StmtPtr& stmt)
{
    if (stmt->yieldFrom && !stmt->expr) {
        setError(ScriptDiagnosticCode::UnexpectedToken,
            "yield from requires an iterable expression",
            stmt->pos);
        return;
    }
    if (stmt->expr)
        inferExpr(stmt->expr);
}

void Sema::analyzeYield(const StmtPtr& stmt)
{
    Scope* scope = currentScope;
    while (scope && scope->kind != Scope::Kind::Function && scope->kind != Scope::Kind::Module) {
        scope = scope->parent;
    }
    if (!scope || scope->kind != Scope::Kind::Function) {
        setError(ScriptDiagnosticCode::UnexpectedToken, "yield outside function", stmt->pos);
        return;
    }
    if (stmt->expr)
        inferExpr(stmt->expr);
}

void Sema::analyzeReturn(const StmtPtr& stmt)
{
    // Find the enclosing function scope
    Scope* s = currentScope;
    while (s && s->kind != Scope::Kind::Function && s->kind != Scope::Kind::Module)
        s = s->parent;

    if (!s || s->kind != Scope::Kind::Function) {
        setError(ScriptDiagnosticCode::UnexpectedToken, "return outside function", stmt->pos);
        return;
    }

    if (stmt->expr) {
        TypePtr retType = inferExpr(stmt->expr);
        if (!retType)
            return;
        if (s->returnType && s->returnType->name == "void") {
            setError(ScriptDiagnosticCode::TypeMismatch,
                core::pmr::String(
                    "void function cannot return a value", &core::defaultCpuMemoryResource()),
                stmt->expr->pos);
            return;
        }
        if (s->returnType && s->returnType->name != "void") {
            if (!isAssignable(retType, s->returnType)) {
                setError(ScriptDiagnosticCode::TypeMismatch,
                    core::pmr::String(
                        "return type mismatch: expected ", &core::defaultCpuMemoryResource())
                        + typeName(s->returnType) + ", got " + typeName(retType),
                    stmt->expr->pos);
                return;
            }
        }
    } else {
        if (s->returnType && s->returnType->name != "void") {
            setError(ScriptDiagnosticCode::TypeMismatch,
                core::pmr::String(
                    "function expects return value of type ", &core::defaultCpuMemoryResource())
                    + typeName(s->returnType),
                stmt->pos);
            return;
        }
    }
}

void Sema::analyzeAssign(const StmtPtr& stmt)
{
    // target = value
    TypePtr targetType = inferExpr(stmt->target);
    if (targetType && stmt->value
        && ((targetType->kind == TypeKind::List && stmt->value->kind == ExprKind::ListLit
                && stmt->value->args.empty())
            || (targetType->kind == TypeKind::Dict && stmt->value->kind == ExprKind::DictLit
                && stmt->value->dictKeys.empty())))
        stmt->value->inferredType = targetType;
    TypePtr valueType = inferExpr(stmt->value);
    if (!targetType || !valueType)
        return;

    // Check if target is const
    if (stmt->target->kind == ExprKind::Ident) {
        Symbol* sym = currentScope ? currentScope->find(stmt->target->name) : nullptr;
        if (sym && sym->isConst) {
            setError(ScriptDiagnosticCode::ImmutableAssign,
                core::pmr::String("cannot assign to const '", &core::defaultCpuMemoryResource())
                    + stmt->target->name + "'",
                stmt->pos);
            return;
        }
    }

    if (stmt->target->kind == ExprKind::Field && stmt->target->left != nullptr
        && stmt->target->left->inferredType != nullptr
        && stmt->target->left->inferredType->reflectedClass != nullptr) {
        const auto member =
            stmt->target->left->inferredType->reflectedClass->get_field(stmt->target->name);
        if (member.is_property() && member.cast<arp::reflection::property&>().is_readonly()) {
            setError(ScriptDiagnosticCode::ImmutableAssign,
                core::pmr::String(
                    "cannot assign to readonly property '", &core::defaultCpuMemoryResource())
                    + stmt->target->name + "'",
                stmt->pos);
            return;
        }
    }

    if (stmt->target->kind == ExprKind::Index && stmt->target->left
        && stmt->target->left->inferredType
        && stmt->target->left->inferredType->kind == TypeKind::Basic
        && stmt->target->left->inferredType->name == "bytes") {
        setError(ScriptDiagnosticCode::ImmutableAssign, "bytes values are immutable", stmt->pos);
        return;
    }

    // For +=, -=, etc., the types must match
    if (stmt->assignOp != "=") {
        if (!typesEqual(valueType, targetType) && !isAssignable(valueType, targetType)) {
            setError(ScriptDiagnosticCode::TypeMismatch,
                core::pmr::String(
                    "compound assignment type mismatch: ", &core::defaultCpuMemoryResource())
                    + typeName(targetType) + " " + stmt->assignOp + " " + typeName(valueType),
                stmt->pos);
            return;
        }
    } else {
        if (!isAssignable(valueType, targetType)) {
            setError(ScriptDiagnosticCode::TypeMismatch,
                core::pmr::String("cannot assign ", &core::defaultCpuMemoryResource())
                    + typeName(valueType) + " to " + typeName(targetType),
                stmt->pos);
            return;
        }
    }
}

void Sema::analyzeImport(const StmtPtr& stmt)
{
    auto anyType = makeType(TypeKind::Basic);
    anyType->name = "any";

    // If a ModuleRegistry is available, verify the module exists and its
    // exports contain the requested names. Without a registry, import
    // validation is deferred to runtime (the VM's LoadModule will fail).
    if (moduleRegistry) {
        auto exports = moduleRegistry->queryExports(stmt->importModule);
        if (exports.empty()) {
            // Module not found or has no exports.
            core::pmr::String diagnostic =
                core::pmr::String("cannot import: module '", &core::defaultCpuMemoryResource())
                + stmt->importModule + "' not found or empty";
            if (!moduleRegistry->lastDiagnostics().empty()) {
                diagnostic += ": ";
                diagnostic += moduleRegistry->lastDiagnostics();
            }
            setError(ScriptDiagnosticCode::UndeclaredName, std::move(diagnostic), stmt->pos);
            return;
        }
        if (!stmt->importIsModuleImport) {
            for (const auto& item : stmt->importNames) {
                bool found = false;
                for (const auto& exp : exports) {
                    if (exp == item.name) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    setError(ScriptDiagnosticCode::UndeclaredName,
                        core::pmr::String("cannot import name '", &core::defaultCpuMemoryResource())
                            + item.name + "' from module '" + stmt->importModule + "'",
                        stmt->pos);
                    return;
                }
            }
        }
    }

    auto declareImported = [&](const core::pmr::String& binding, bool isModuleRef) {
        Symbol symbol;
        symbol.name = binding;
        symbol.type = anyType;
        symbol.isImported = true;
        symbol.isModuleRef = isModuleRef;
        symbol.importModule = stmt->importModule;
        symbol.pos = stmt->pos;
        if (!currentScope->declare(symbol)) {
            setError(ScriptDiagnosticCode::RedeclaredName,
                core::pmr::String("redeclared imported name: ", &core::defaultCpuMemoryResource())
                    + binding,
                stmt->pos);
        }
    };

    if (stmt->importIsModuleImport) {
        const core::pmr::String& binding =
            stmt->importAlias.empty() ? stmt->importModule : stmt->importAlias;
        declareImported(binding, true);
        return;
    }

    for (const auto& item : stmt->importNames) {
        const core::pmr::String& binding = item.alias.empty() ? item.name : item.alias;
        declareImported(binding, false);
        if (!ok())
            return;
        if (Symbol* importedSymbol = currentScope->findLocal(binding)) {
            importedSymbol->importName = item.name;
        }
        if (moduleRegistry != nullptr) {
            const auto* reflectedType = moduleRegistry->findType(stmt->importModule, item.name);
            if (reflectedType != nullptr) {
                Symbol* typeSymbol = currentScope->findLocal(binding);
                if (typeSymbol != nullptr) {
                    typeSymbol->type = makeType(TypeKind::Class);
                    typeSymbol->type->name = binding;
                    typeSymbol->type->reflectedClass = reflectedType;
                    typeSymbol->isClass = true;
                    typeSymbol->importName = item.name;
                }
            }
        }
    }
}

void Sema::analyzeBlock(const core::pmr::Vector<StmtPtr>& body)
{
    for (const auto& stmt : body) {
        if (!ok())
            break;
        analyzeStmt(stmt);
    }
}

void Sema::analyzeStmt(const StmtPtr& stmt)
{
    switch (stmt->kind) {
    case StmtKind::VarDecl:
        analyzeVarDecl(stmt);
        break;
    case StmtKind::FuncDecl:
        analyzeFuncDecl(stmt);
        break;
    case StmtKind::ClassDecl:
        analyzeClassDecl(stmt);
        break;
    case StmtKind::If:
        analyzeIf(stmt);
        break;
    case StmtKind::While:
        analyzeWhile(stmt);
        break;
    case StmtKind::For:
        analyzeFor(stmt);
        break;
    case StmtKind::Try:
        analyzeTry(stmt);
        break;
    case StmtKind::Raise:
        analyzeRaise(stmt);
        break;
    case StmtKind::Yield:
        analyzeYield(stmt);
        break;
    case StmtKind::Return:
        analyzeReturn(stmt);
        break;
    case StmtKind::Assign:
        analyzeAssign(stmt);
        break;
    case StmtKind::Expr:
        inferExpr(stmt->expr);
        break;
    case StmtKind::ImportDecl:
        analyzeImport(stmt);
        break;
    case StmtKind::Break:
    case StmtKind::Continue:
    case StmtKind::Pass:
        break;
    }
}

SemaResult runSema(std::shared_ptr<Module> mod, ModuleRegistry* moduleRegistry)
{
    Sema sema;
    sema.module = mod;
    sema.moduleRegistry = moduleRegistry;

    // Module scope
    Scope* modScope = sema.pushScope(Scope::Kind::Module);

    // Declare built-in functions (range, print, etc.)
    {
        // range(end: int) -> [int]  (1-arg form: 0..end)
        // Also accepts range(start: int, end: int) -> [int] (2-arg form)
        // We declare the 1-arg form; the 2-arg call will produce WrongArgCount
        // in sema but will work at runtime (VM handles both). For now, declare
        // a variadic-friendly form: accept 1 or 2 args by declaring the 1-arg
        // version and relaxing the arg count check for builtins.
        auto rangeType = makeType(TypeKind::Func);
        auto intType = makeType(TypeKind::Basic);
        intType->name = "int";
        auto listInt = makeType(TypeKind::List);
        listInt->elem = intType;
        rangeType->params.push_back(intType);
        rangeType->result = listInt;
        Symbol rangeSym;
        rangeSym.name = "range";
        rangeSym.type = rangeType;
        rangeSym.isFunc = true;
        modScope->declare(rangeSym);

        // print(value: ?) -> void
        auto printType = makeType(TypeKind::Func);
        auto anyType = makeType(TypeKind::Basic);
        anyType->name = "any";
        printType->params.push_back(anyType);
        auto voidType = makeType(TypeKind::Basic);
        voidType->name = "void";
        printType->result = voidType;
        Symbol printSym;
        printSym.name = "print";
        printSym.type = printType;
        printSym.isFunc = true;
        modScope->declare(printSym);

        auto lenType = makeType(TypeKind::Func);
        auto lenArgument = makeType(TypeKind::Basic);
        lenArgument->name = "any";
        auto lenResult = makeType(TypeKind::Basic);
        lenResult->name = "int";
        lenType->params.push_back(lenArgument);
        lenType->result = lenResult;
        Symbol lenSym;
        lenSym.name = "len";
        lenSym.type = lenType;
        lenSym.isFunc = true;
        modScope->declare(lenSym);

        auto appendType = makeType(TypeKind::Func);
        appendType->params.push_back(lenArgument);
        appendType->params.push_back(lenArgument);
        appendType->result = lenArgument;
        Symbol appendSym;
        appendSym.name = "append";
        appendSym.type = appendType;
        appendSym.isFunc = true;
        modScope->declare(appendSym);

        auto mutationType = makeType(TypeKind::Func);
        mutationType->params.push_back(lenArgument);
        mutationType->params.push_back(lenArgument);
        mutationType->result = voidType;
        Symbol deleteSym;
        deleteSym.name = "delete";
        deleteSym.type = mutationType;
        deleteSym.isFunc = true;
        modScope->declare(deleteSym);

        auto clearType = makeType(TypeKind::Func);
        clearType->params.push_back(lenArgument);
        clearType->result = voidType;
        Symbol clearSym;
        clearSym.name = "clear";
        clearSym.type = clearType;
        clearSym.isFunc = true;
        modScope->declare(clearSym);
    }

    // Pass 0: process imports before anything else so imported names
    // are visible to all subsequent declarations.
    for (const auto& decl : mod->decls) {
        if (decl->kind == StmtKind::ImportDecl) {
            sema.analyzeImport(decl);
            if (!sema.ok())
                break;
        }
    }

    // First pass: declare all top-level functions and classes (forward references)
    for (const auto& decl : mod->decls) {
        if (decl->kind == StmtKind::FuncDecl) {
            auto funcType = functionTypeFor(decl);
            Symbol sym;
            sym.name = decl->funcName;
            sym.type = funcType;
            sym.isFunc = true;
            sym.pos = decl->pos;
            modScope->declare(sym);
        } else if (decl->kind == StmtKind::ClassDecl) {
            auto declaredType = makeType(TypeKind::Class);
            declaredType->name = decl->className;
            declaredType->baseType =
                decl->baseClassName.empty() ? nullptr : sema.classType(decl->baseClassName);
            Symbol sym;
            sym.name = decl->className;
            sym.type = declaredType;
            sym.isClass = true;
            sym.pos = decl->pos;
            modScope->declare(sym);
        }
    }

    for (const auto& decl : mod->decls) {
        if (decl->kind != StmtKind::ClassDecl || decl->baseClassName.empty())
            continue;
        if (sema.findClass(decl->baseClassName) == nullptr) {
            sema.setError(ScriptDiagnosticCode::UndeclaredName,
                core::pmr::String("base class '", &core::defaultCpuMemoryResource())
                    + decl->baseClassName + "' is not declared",
                decl->pos);
            break;
        }
    }

    // Resolve the language-level constructor convention once on the typed AST.
    // Parser syntax remains uniform for all methods; later stages consume this
    // semantic role instead of matching the method name themselves.
    for (const auto& decl : mod->decls) {
        if (decl->kind != StmtKind::ClassDecl)
            continue;
        for (const auto& method : decl->methods) {
            if (method->funcName == "__init__") {
                method->isConstructor = true;
                break;
            }
        }
    }

    // Second pass: analyze bodies (skip imports -- already processed in pass 0)
    for (const auto& decl : mod->decls) {
        if (!sema.ok())
            break;
        if (decl->kind == StmtKind::ImportDecl)
            continue; // already handled in pass 0
        sema.analyzeStmt(decl);
    }

    sema.popScope();

    SemaResult result;
    result.error = std::move(sema.error);
    return result;
}

} // namespace

SemaResult analyze(std::shared_ptr<Module> module, ModuleRegistry* moduleRegistry)
{
    return runSema(module, moduleRegistry);
}

std::shared_ptr<Module> compileToTypedAst(core::pmr::String source,
    core::pmr::String filename,
    ScriptDiagnostic* error,
    ModuleRegistry* moduleRegistry)
{
    auto pr = parseSource(std::move(source), std::move(filename));
    if (!pr.ok()) {
        if (error)
            *error = std::move(pr.error);
        return nullptr;
    }
    auto sr = analyze(pr.module, moduleRegistry);
    if (!sr.ok()) {
        if (error)
            *error = std::move(sr.error);
        return nullptr;
    }
    return pr.module;
}

} // namespace arp::scriptvm
