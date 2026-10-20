// ArpScript Sema unit tests (Phase 4).
//
// Tests semantic analysis: scope resolution, type checking, mandatory hints,
// assignment compatibility, return type matching, redeclaration detection,
// const immutability, and closure/forward-reference handling.
//
// Test style: self-contained main() + require() helpers, no framework.

#include "scriptvm/compile/lexer.h"
#include "scriptvm/compile/parser.h"
#include "scriptvm/compile/sema.h"
#include "scriptvm/runtime/module_registry.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>

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

// Parse + sema in one step; assert success.
SemaResult analyzeOk(const char* src, const char* label)
{
    arp::core::pmr::String source(src, &arp::core::defaultCpuMemoryResource());
    arp::core::pmr::String fname("test", &arp::core::defaultCpuMemoryResource());
    auto pr = parseSource(std::move(source), std::move(fname));
    if (!pr.ok()) {
        std::printf("FAIL: %s -- parse error: %s (line %d)\n",
            label,
            pr.error.message.c_str(),
            pr.error.line);
        ++g_failures;
        return SemaResult{};
    }
    auto sr = analyze(pr.module);
    if (!sr.ok()) {
        std::printf("FAIL: %s -- sema error: %s (line %d, col %d)\n",
            label,
            sr.error.message.c_str(),
            sr.error.line,
            sr.error.column);
        ++g_failures;
    }
    return sr;
}

// Parse + sema; assert error with expected code.
void analyzeError(const char* src, ScriptDiagnosticCode expectedCode, const char* label)
{
    arp::core::pmr::String source(src, &arp::core::defaultCpuMemoryResource());
    arp::core::pmr::String fname("test", &arp::core::defaultCpuMemoryResource());
    auto pr = parseSource(std::move(source), std::move(fname));
    if (!pr.ok()) {
        // Parse error is also acceptable if the test expects an error
        if (pr.error.code != expectedCode) {
            std::printf("FAIL: %s -- parse error code %d, expected %d\n",
                label,
                (int)pr.error.code,
                (int)expectedCode);
            ++g_failures;
        }
        return;
    }
    auto sr = analyze(pr.module);
    if (sr.ok()) {
        std::printf("FAIL: %s -- expected error but sema succeeded\n", label);
        ++g_failures;
        return;
    }
    if (sr.error.code != expectedCode) {
        std::printf("FAIL: %s -- sema error code %d (%s), expected %d\n",
            label,
            (int)sr.error.code,
            sr.error.message.c_str(),
            (int)expectedCode);
        ++g_failures;
    }
}

void testValidProgram()
{
    const char* src = "var x: int = 5\n"
                      "var y: int = x + 3\n"
                      "var z: float = x\n";
    analyzeOk(src, "valid program");
}

void testUndeclaredName()
{
    analyzeError("var y: int = x\n", ScriptDiagnosticCode::UndeclaredName, "undeclared name");
}

void testTypeMismatch()
{
    analyzeError(
        "var x: int = \"hello\"\n", ScriptDiagnosticCode::TypeMismatch, "type mismatch int=string");
}

void testNumericWidening()
{
    const char* src = "var i: int = 5\n"
                      "var l: int64 = i\n"
                      "var f: float = i\n"
                      "var g: float = l\n";
    analyzeOk(src, "numeric widening");
}

void testRedeclaredName()
{
    const char* src = "var x: int = 5\n"
                      "var x: int = 10\n";
    analyzeError(src, ScriptDiagnosticCode::RedeclaredName, "redeclared name");
}

void testConstImmutable()
{
    const char* src = "const c: int = 5\n"
                      "c = 10\n";
    analyzeError(src, ScriptDiagnosticCode::ImmutableAssign, "const immutable");
}

void testForwardFunctionRef()
{
    // Functions can reference each other (forward declaration).
    const char* src = "def f(n: int) -> int:\n"
                      "    return g(n)\n"
                      "def g(n: int) -> int:\n"
                      "    return n + 1\n";
    analyzeOk(src, "forward function ref");
}

void testReturnMismatch()
{
    const char* src = "def f() -> int:\n"
                      "    return \"hello\"\n";
    analyzeError(src, ScriptDiagnosticCode::TypeMismatch, "return mismatch");
}

void testReturnVoidWithValue()
{
    const char* src = "def f() -> void:\n"
                      "    return 5\n";
    analyzeError(src, ScriptDiagnosticCode::TypeMismatch, "void return with value");
}

void testFunctionCallArgCount()
{
    const char* src = "def f(a: int, b: int) -> int:\n"
                      "    return a + b\n"
                      "var x: int = f(1)\n";
    analyzeError(src, ScriptDiagnosticCode::WrongArgCount, "wrong arg count");
}

void testFunctionCallArgType()
{
    const char* src = "def f(a: int) -> int:\n"
                      "    return a\n"
                      "var x: int = f(\"hello\")\n";
    analyzeError(src, ScriptDiagnosticCode::TypeMismatch, "arg type mismatch");
}

void testClassDeclValid()
{
    const char* src = "class Vec3:\n"
                      "    x: float\n"
                      "    y: float\n"
                      "    z: float\n"
                      "    def length(self) -> float:\n"
                      "        return 1.0\n";
    analyzeOk(src, "class decl valid");
}

void testClassInheritanceValid()
{
    const char* src = "class Importer:\n"
                      "    value: int\n"
                      "    def read(self) -> int:\n"
                      "        return self.value\n"
                      "class GlbImporter(Importer):\n"
                      "    def value_from_base(self) -> int:\n"
                      "        return self.read()\n"
                      "def make() -> Importer:\n"
                      "    return GlbImporter()\n";
    analyzeOk(src, "class inheritance valid");
}

void testTransitiveClassAssignability()
{
    const char* src = "class Action:\n"
                      "    def apply(self) -> int:\n"
                      "        return 1\n"
                      "class ResourceAction(Action):\n"
                      "    cost: int\n"
                      "class SpellAction(ResourceAction):\n"
                      "    cooldown: int\n"
                      "class FastSpellAction(SpellAction):\n"
                      "    def apply(self) -> int:\n"
                      "        return 2\n"
                      "def run() -> int:\n"
                      "    var action: Action = FastSpellAction()\n"
                      "    return action.apply()\n";
    analyzeOk(src, "transitive class assignability");
}

void testDerivedClassListInitialization()
{
    const char* src = "class Action:\n"
                      "    def apply(self) -> int:\n"
                      "        return 1\n"
                      "class AttackAction(Action):\n"
                      "    def apply(self) -> int:\n"
                      "        return 2\n"
                      "var actions: [Action] = [AttackAction()]\n";
    analyzeOk(src, "derived class list initialization");
}

void testClassForwardMethodRef()
{
    const char* src = "class ForwardMethods:\n"
                      "    def first(self, value: int) -> int:\n"
                      "        return self.second(value)\n"
                      "    def second(self, value: int) -> int:\n"
                      "        return value + 1\n";
    analyzeOk(src, "class forward method ref");
}

void testClassRedeclaredField()
{
    const char* src = "class Bad:\n"
                      "    x: int\n"
                      "    x: float\n";
    analyzeError(src, ScriptDiagnosticCode::RedeclaredName, "class redeclared field");
}

void testIfScopeValid()
{
    const char* src = "def f(n: int) -> int:\n"
                      "    if n > 0:\n"
                      "        var x: int = 1\n"
                      "    else:\n"
                      "        var x: int = 2\n"
                      "    return n\n";
    analyzeOk(src, "if scope valid");
}

void testForIterValid()
{
    const char* src = "var sum: int = 0\n"
                      "for i: int in range(10):\n"
                      "    sum = sum + i\n";
    analyzeOk(src, "for iter valid");
}

void testAssignCompatible()
{
    const char* src = "var x: int = 5\n"
                      "x = 10\n"
                      "x = x + 1\n";
    analyzeOk(src, "assign compatible");
}

void testAssignIncompatible()
{
    const char* src = "var x: int = 5\n"
                      "x = \"hello\"\n";
    analyzeError(src, ScriptDiagnosticCode::TypeMismatch, "assign incompatible");
}

void testCompoundAssignValid()
{
    const char* src = "var x: int = 5\n"
                      "x += 3\n"
                      "x -= 1\n";
    analyzeOk(src, "compound assign valid");
}

void testRedeclaredParam()
{
    const char* src = "def f(a: int, a: int) -> int:\n"
                      "    return a\n";
    analyzeError(src, ScriptDiagnosticCode::RedeclaredName, "redeclared param");
}

void testReturnOutsideFunction()
{
    analyzeError("return 5\n", ScriptDiagnosticCode::UnexpectedToken, "return outside function");
}

void testListTypeValid()
{
    const char* src = "var arr: [int] = [1, 2, 3]\n"
                      "var x: int = arr[0]\n";
    analyzeOk(src, "list type valid");
}

void testBytesTypeValid()
{
    const char* src = "def size(data: bytes) -> int:\n"
                      "    return len(data)\n"
                      "var payload: bytes = \"abc\" as bytes\n"
                      "var n: int = size(payload)\n";
    analyzeOk(src, "bytes type valid");
}

void testBytesImmutable()
{
    const char* src = "var payload: bytes = \"abc\" as bytes\n"
                      "payload[0] = 1\n";
    analyzeError(src, ScriptDiagnosticCode::ImmutableAssign, "bytes immutable");
}

void testDictTypeValid()
{
    const char* src = "var m: {string: int} = {\"a\": 1}\n"
                      "var x: int = m[\"a\"]\n";
    analyzeOk(src, "dict type valid");
}

void testContainerMutationBuiltins()
{
    analyzeOk("var values: [int] = [1]\n"
              "append(values, 2)\n"
              "clear(values)\n"
              "var entries: {int: int} = {1: 2}\n"
              "delete(entries, 1)\n",
        "container mutation builtins accept their declared container contracts");
    analyzeError(
        "append(1, 2)\n", ScriptDiagnosticCode::TypeMismatch, "append rejects a non-list receiver");
    analyzeError("var values: [int] = [1]\n"
                 "append(values, \"bad\")\n",
        ScriptDiagnosticCode::TypeMismatch,
        "append rejects an incompatible list value");
    analyzeError("var entries: {int: int} = {1: 2}\n"
                 "delete(entries, \"bad\")\n",
        ScriptDiagnosticCode::TypeMismatch,
        "delete rejects an incompatible dictionary key");
    analyzeError(
        "clear(1)\n", ScriptDiagnosticCode::TypeMismatch, "clear rejects a non-container receiver");
    analyzeError("var values: [int] = [1]\n"
                 "append(values)\n",
        ScriptDiagnosticCode::WrongArgCount,
        "append rejects an incomplete argument list");
}

void testOptionalAssignNil()
{
    const char* src = "var x: int? = nil\n";
    analyzeOk(src, "optional assign nil");
}

void testClosureValid()
{
    const char* src = "def make_counter(start: int) -> () -> int:\n"
                      "    var count: int = start\n"
                      "    def inc() -> int:\n"
                      "        count = count + 1\n"
                      "        return count\n"
                      "    return inc\n";
    analyzeOk(src, "closure valid");
}

void testFibProgram()
{
    const char* src = "def fib(n: int) -> int:\n"
                      "    if n < 2:\n"
                      "        return n\n"
                      "    return fib(n - 1) + fib(n - 2)\n"
                      "var result: int = fib(10)\n";
    analyzeOk(src, "fib program");
}

void testBoolExprValid()
{
    const char* src = "var t: bool = true\n"
                      "var f: bool = false\n"
                      "var x: bool = t and f\n"
                      "var y: bool = t or f\n"
                      "var z: bool = not t\n";
    analyzeOk(src, "bool expr valid");
}

void testStringConcatValid()
{
    const char* src = "var a: string = \"hello\"\n"
                      "var b: string = \"world\"\n"
                      "var c: string = a + b\n";
    analyzeOk(src, "string concat valid");
}

void testExtendedArithmeticOperatorsAreNumericOnly()
{
    analyzeError("var power: int = \"x\" ** 3\n",
        ScriptDiagnosticCode::TypeMismatch,
        "power operator rejects non-numeric operands");
    analyzeError("var floor: int = 7 // \"x\"\n",
        ScriptDiagnosticCode::TypeMismatch,
        "floor division rejects non-numeric operands");
    analyzeOk("var power: int = 2 ** 3\n"
              "var floor: int = -7 // 2\n",
        "extended arithmetic operators analyze successfully");
}

void testReflectedFunctionSignature()
{
    ScriptPluginRegistrar typed(
        arp::core::pmr::String("typed", &arp::core::defaultCpuMemoryResource()));
    typed.exportFunction(arp::core::pmr::String("add", &arp::core::defaultCpuMemoryResource()),
        arp::reflection::function("add", [](int lhs, int rhs) -> int {
            return lhs + rhs;
        }));
    auto optionalFunction = arp::reflection::function("optional", [](int, bool) -> int {
        return 0;
    });
    optionalFunction.set_default_arguments(true);
    typed.exportFunction(arp::core::pmr::String("optional", &arp::core::defaultCpuMemoryResource()),
        std::move(optionalFunction));

    ScriptPluginRegistrar dynamic(
        arp::core::pmr::String("dynamic", &arp::core::defaultCpuMemoryResource()));
    dynamic.exportFunction(arp::core::pmr::String("add", &arp::core::defaultCpuMemoryResource()),
        [](const arp::core::pmr::Vector<Value>&) -> Value {
            return makeInt(0);
        });

    ScriptPluginRegistrar metadataBound(
        arp::core::pmr::String("metadata", &arp::core::defaultCpuMemoryResource()));
    HostCallableContract metadataContract;
    metadataContract.bindingId = "metadata.add.v1";
    metadataContract.signatureHash = "sha256:metadata-add";
    metadataContract.fixedArity = 2;
    metadataContract.lifetimeVersion = 1;
    require(metadataBound.exportFunction(
                arp::core::pmr::String("add", &arp::core::defaultCpuMemoryResource()),
                [](const arp::core::pmr::Vector<Value>&) -> Value {
                    return makeInt(0);
                },
                std::move(metadataContract)),
        "metadata-bound host function registers");

    ModuleRegistry registry;
    registry.installPluginModule(
        arp::core::pmr::String("typed", &arp::core::defaultCpuMemoryResource()), typed.build());
    registry.installPluginModule(
        arp::core::pmr::String("dynamic", &arp::core::defaultCpuMemoryResource()), dynamic.build());
    registry.installPluginModule(
        arp::core::pmr::String("metadata", &arp::core::defaultCpuMemoryResource()),
        metadataBound.build());

    const auto analyzeWithRegistry = [&](const char* source) {
        auto parsed = parseSource(
            arp::core::pmr::String(source, &arp::core::defaultCpuMemoryResource()),
            arp::core::pmr::String("reflection_signature", &arp::core::defaultCpuMemoryResource()));
        require(parsed.ok(), "reflection signature source parses");
        return parsed.ok() ? analyze(parsed.module, &registry) : SemaResult{};
    };

    const auto qualified = analyzeWithRegistry("import typed\n"
                                               "var value: int = typed.add(1, 2)\n");
    require(qualified.ok(), "reflection signature qualified call");

    const auto defaults = analyzeWithRegistry("import typed\n"
                                              "var value: int = typed.optional(1)\n");
    require(defaults.ok(), "reflection signature default argument");

    const auto missingQualified = analyzeWithRegistry("import typed\n"
                                                      "var value: int = typed.missing()\n");
    require(!missingQualified.ok()
            && missingQualified.error.message.find("has no export 'missing'") != std::string::npos,
        "qualified module export must be rejected when missing");

    const auto wrongType = analyzeWithRegistry("from typed import add\n"
                                               "var value: int = add(\"bad\", 2)\n");
    require(!wrongType.ok() && wrongType.error.code == ScriptDiagnosticCode::TypeMismatch,
        "reflection signature rejects wrong argument type");

    const auto missing = analyzeWithRegistry("from dynamic import add\n"
                                             "var value: int = add(1)\n");
    require(
        !missing.ok() && missing.error.message.find("no reflection signature") != std::string::npos,
        "dynamic host function without signature is rejected");

    const auto metadataCall = analyzeWithRegistry("from metadata import add\n"
                                                  "var value: int = add(1, \"two\")\n");
    require(metadataCall.ok(),
        "metadata-bound host function admits its declared fixed arity without reflection types");

    const auto metadataWrongArity = analyzeWithRegistry("from metadata import add\n"
                                                        "var value: int = add(1)\n");
    require(!metadataWrongArity.ok()
            && metadataWrongArity.error.code == ScriptDiagnosticCode::WrongArgCount,
        "metadata-bound host function rejects an arity outside its declared contract");

    const auto qualifiedMetadataCall =
        analyzeWithRegistry("import metadata\n"
                            "var value: int = metadata.add(1, \"two\")\n");
    require(qualifiedMetadataCall.ok(),
        "module-qualified metadata host function admits its declared fixed arity");

    const auto qualifiedMetadataWrongArity =
        analyzeWithRegistry("import metadata\n"
                            "var value: int = metadata.add(1)\n");
    require(!qualifiedMetadataWrongArity.ok()
            && qualifiedMetadataWrongArity.error.code == ScriptDiagnosticCode::WrongArgCount,
        "module-qualified metadata host function rejects wrong arity");
}

} // namespace

int main()
{
    testValidProgram();
    testUndeclaredName();
    testTypeMismatch();
    testNumericWidening();
    testRedeclaredName();
    testConstImmutable();
    testForwardFunctionRef();
    testReturnMismatch();
    testReturnVoidWithValue();
    testFunctionCallArgCount();
    testFunctionCallArgType();
    testClassDeclValid();
    testClassInheritanceValid();
    testTransitiveClassAssignability();
    testDerivedClassListInitialization();
    testClassForwardMethodRef();
    testClassRedeclaredField();
    testIfScopeValid();
    testForIterValid();
    testAssignCompatible();
    testAssignIncompatible();
    testCompoundAssignValid();
    testRedeclaredParam();
    testReturnOutsideFunction();
    testListTypeValid();
    testBytesTypeValid();
    testBytesImmutable();
    testDictTypeValid();
    testContainerMutationBuiltins();
    testOptionalAssignNil();
    testClosureValid();
    testFibProgram();
    testBoolExprValid();
    testStringConcatValid();
    testExtendedArithmeticOperatorsAreNumericOnly();
    testReflectedFunctionSignature();

    if (g_failures > 0) {
        std::printf("\n%d test(s) FAILED\n", g_failures);
        return 1;
    }
    std::printf("\nAll ArpScript sema tests passed\n");
    return 0;
}
