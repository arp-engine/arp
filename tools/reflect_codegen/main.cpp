#include "reflection/registration_contract_format.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct PropertyModel {
    std::string name;
    std::string cppType;
    std::string access;
    std::string doc;
    std::vector<std::string> flags;
    bool accessor = false;
    std::size_t getterIndex = 0;
    std::size_t setterIndex = 0;
};

struct ParameterModel {
    std::string name;
    std::string cppType;
    std::string referenceKind = "value";
    bool nullable = false;
};

struct MethodModel {
    std::string name;
    std::string reflectName;
    std::string returnType;
    std::string args;
    std::string access;
    std::vector<std::string> flags;
    std::vector<ParameterModel> parameters;
    std::string returnReferenceKind = "value";
    bool nullableReturn = false;
    bool staticMethod = false;
    bool maySuspend = false;
    bool constMethod = false;
    bool volatileMethod = false;
    bool noexceptMethod = false;
    std::string refQualifier;
    bool accessorGetter = false;
};

struct FunctionModel {
    std::string namespaceName;
    std::string name;
    std::string cppName;
    std::string reflectName;
    std::string returnType;
    std::string args;
    std::string defaultArguments;
    std::vector<std::string> flags;
    std::vector<ParameterModel> parameters;
    std::string returnReferenceKind = "value";
    bool nullableReturn = false;
    bool maySuspend = false;
};

struct ClassModel {
    std::string kind;
    std::string namespaceName;
    std::string name;
    std::string cppName;
    std::string reflectName;
    bool defaultConstruct = false;
    std::string ownership = "unspecified";
    std::string lifecycle = "unspecified";
    std::string moduleExportName;
    std::vector<std::string> bases;
    std::vector<std::vector<ParameterModel>> constructors;
    std::vector<PropertyModel> properties;
    std::vector<MethodModel> methods;
};

struct EnumModel {
    std::string namespaceName;
    std::string name;
    std::string cppName;
    std::string reflectName;
    std::vector<std::string> values;
};

struct AliasModel {
    std::string namespaceName;
    std::string name;
    std::string target;
};

struct HeaderModel {
    std::string path;
    std::vector<ClassModel> classes;
    std::vector<FunctionModel> functions;
    std::vector<EnumModel> enums;
    std::vector<AliasModel> aliases;
};

struct ProgramOptions {
    std::string module;
    std::filesystem::path outJson;
    std::filesystem::path outCpp;
    std::vector<std::filesystem::path> headers;
};

struct ParseResult {
    bool success = false;
    std::string diagnostics;
    std::vector<HeaderModel> headers;
};

void appendBoolField(std::string& output, std::string_view name, bool value)
{
    arp::reflection::appendRegistrationContractField(
        output, name, value ? std::string_view("1") : std::string_view("0"));
}

std::string parameterContract(const ParameterModel& parameter)
{
    std::string result;
    arp::reflection::appendRegistrationContractField(result, "name", parameter.name);
    arp::reflection::appendRegistrationContractField(result, "cpp-type", parameter.cppType);
    arp::reflection::appendRegistrationContractField(
        result, "reference-kind", parameter.referenceKind);
    appendBoolField(result, "nullable", parameter.nullable);
    return result;
}

template <typename Values>
void appendSortedValues(std::string& output, std::string_view name, const Values& source)
{
    std::vector<std::string> values(source.begin(), source.end());
    std::sort(values.begin(), values.end());
    for (const auto& value : values)
        arp::reflection::appendRegistrationContractField(output, name, value);
}

std::string methodContract(const MethodModel& method)
{
    std::string result;
    arp::reflection::appendRegistrationContractField(result, "name", method.name);
    arp::reflection::appendRegistrationContractField(result, "reflect-name", method.reflectName);
    arp::reflection::appendRegistrationContractField(result, "return-type", method.returnType);
    arp::reflection::appendRegistrationContractField(result, "args", method.args);
    arp::reflection::appendRegistrationContractField(result, "access", method.access);
    appendBoolField(result, "static", method.staticMethod);
    appendBoolField(result, "may-suspend", method.maySuspend);
    appendBoolField(result, "const", method.constMethod);
    appendBoolField(result, "volatile", method.volatileMethod);
    appendBoolField(result, "noexcept", method.noexceptMethod);
    arp::reflection::appendRegistrationContractField(result, "ref-qualifier", method.refQualifier);
    appendBoolField(result, "nullable-return", method.nullableReturn);
    arp::reflection::appendRegistrationContractField(
        result, "return-reference-kind", method.returnReferenceKind);
    for (const auto& parameter : method.parameters) {
        const auto value = parameterContract(parameter);
        arp::reflection::appendRegistrationContractField(result, "parameter", value);
    }
    appendSortedValues(result, "flag", method.flags);
    return result;
}

std::string classDescriptorContract(const ClassModel& cls)
{
    std::string result;
    arp::reflection::appendRegistrationContractField(result, "kind", cls.kind);
    arp::reflection::appendRegistrationContractField(result, "cpp-name", cls.cppName);
    arp::reflection::appendRegistrationContractField(result, "reflect-name", cls.reflectName);
    appendBoolField(result, "default-construct", cls.defaultConstruct);
    arp::reflection::appendRegistrationContractField(result, "ownership", cls.ownership);
    arp::reflection::appendRegistrationContractField(result, "lifecycle", cls.lifecycle);
    arp::reflection::appendRegistrationContractField(result, "module-export", cls.moduleExportName);
    appendSortedValues(result, "base", cls.bases);
    std::vector<std::string> constructors;
    constructors.reserve(cls.constructors.size());
    for (const auto& constructor : cls.constructors) {
        std::string value;
        for (const auto& parameter : constructor) {
            const auto parameterValue = parameterContract(parameter);
            arp::reflection::appendRegistrationContractField(value, "parameter", parameterValue);
        }
        constructors.push_back(std::move(value));
    }
    appendSortedValues(result, "constructor", constructors);
    std::vector<std::string> properties;
    properties.reserve(cls.properties.size());
    for (const auto& property : cls.properties) {
        std::string value;
        arp::reflection::appendRegistrationContractField(value, "name", property.name);
        arp::reflection::appendRegistrationContractField(value, "cpp-type", property.cppType);
        arp::reflection::appendRegistrationContractField(value, "access", property.access);
        if (!property.doc.empty())
            arp::reflection::appendRegistrationContractField(value, "doc", property.doc);
        arp::reflection::appendRegistrationContractField(
            value, "source", property.accessor ? "accessor" : "field");
        if (property.accessor) {
            arp::reflection::appendRegistrationContractField(
                value, "getter", cls.methods[property.getterIndex].reflectName);
            arp::reflection::appendRegistrationContractField(
                value, "setter", cls.methods[property.setterIndex].reflectName);
        }
        appendSortedValues(value, "flag", property.flags);
        properties.push_back(std::move(value));
    }
    appendSortedValues(result, "property", properties);
    std::vector<std::string> methods;
    methods.reserve(cls.methods.size());
    for (const auto& method : cls.methods) {
        if (!method.accessorGetter)
            methods.push_back(methodContract(method));
    }
    appendSortedValues(result, "method", methods);
    return result;
}

std::string functionDescriptorContract(const FunctionModel& function)
{
    std::string result;
    arp::reflection::appendRegistrationContractField(result, "cpp-name", function.cppName);
    arp::reflection::appendRegistrationContractField(result, "reflect-name", function.reflectName);
    arp::reflection::appendRegistrationContractField(result, "return-type", function.returnType);
    arp::reflection::appendRegistrationContractField(result, "args", function.args);
    arp::reflection::appendRegistrationContractField(
        result, "default-arguments", function.defaultArguments);
    appendBoolField(result, "may-suspend", function.maySuspend);
    appendBoolField(result, "nullable-return", function.nullableReturn);
    arp::reflection::appendRegistrationContractField(
        result, "return-reference-kind", function.returnReferenceKind);
    for (const auto& parameter : function.parameters) {
        const auto value = parameterContract(parameter);
        arp::reflection::appendRegistrationContractField(result, "parameter", value);
    }
    appendSortedValues(result, "flag", function.flags);
    return result;
}

std::string enumDescriptorContract(const EnumModel& enumeration)
{
    std::string result;
    arp::reflection::appendRegistrationContractField(result, "cpp-name", enumeration.cppName);
    arp::reflection::appendRegistrationContractField(
        result, "reflect-name", enumeration.reflectName);
    appendSortedValues(result, "value", enumeration.values);
    return result;
}

std::string trim(std::string value)
{
    auto isSpace = [](unsigned char ch) {
        return std::isspace(ch) != 0;
    };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [&](char ch) {
        return !isSpace(static_cast<unsigned char>(ch));
    }));
    value.erase(std::find_if(value.rbegin(),
                    value.rend(),
                    [&](char ch) {
                        return !isSpace(static_cast<unsigned char>(ch));
                    })
                    .base(),
        value.end());
    return value;
}

std::string stripLineComment(const std::string& line)
{
    const std::size_t comment = line.find("//");
    if (comment == std::string::npos) {
        return line;
    }
    return line.substr(0, comment);
}

std::string cleanLine(const std::string& line)
{
    return trim(stripLineComment(line));
}

bool startsWith(const std::string& text, const std::string& prefix)
{
    return text.rfind(prefix, 0) == 0;
}

bool markerStartsWith(const std::string& text, const char* marker)
{
    if (!startsWith(text, marker)) {
        return false;
    }
    const std::size_t markerLength = std::string(marker).size();
    return text.size() == markerLength || text[markerLength] == '('
        || std::isspace(static_cast<unsigned char>(text[markerLength])) != 0;
}

bool isTypeCloseLine(const std::string& line)
{
    return line == "};";
}

bool isNamespaceCloseLine(const std::string& line)
{
    return startsWith(line, "} // namespace");
}

std::string joinNamespace(const std::vector<std::string>& namespaces)
{
    std::string result;
    for (const auto& item : namespaces) {
        if (!result.empty()) {
            result += "::";
        }
        result += item;
    }
    return result;
}

std::string qualifiedName(const std::string& namespaceName, const std::string& name)
{
    if (namespaceName.empty()) {
        return name;
    }
    return namespaceName + "::" + name;
}

std::string jsonEscape(const std::string& value)
{
    std::string out;
    for (char ch : value) {
        switch (ch) {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out += ch;
            break;
        }
    }
    return out;
}

std::string parseMarkerBody(const std::string& marker)
{
    const std::size_t open = marker.find('(');
    const std::size_t close = marker.rfind(')');
    if (open == std::string::npos || close == std::string::npos || close <= open) {
        return {};
    }
    return marker.substr(open + 1, close - open - 1);
}

std::string parseNameOverride(const std::string& marker, const std::string& fallback)
{
    const std::string body = parseMarkerBody(marker);
    std::size_t start = 0;
    while (start < body.size()) {
        const std::size_t comma = body.find(',', start);
        std::string token = trim(
            body.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
        const std::size_t equals = token.find('=');
        if (equals != std::string::npos) {
            const std::string key = trim(token.substr(0, equals));
            if (key == "Name") {
                const std::size_t firstQuote = token.find('"', equals);
                if (firstQuote == std::string::npos) {
                    return fallback;
                }
                const std::size_t secondQuote = token.find('"', firstQuote + 1);
                if (secondQuote == std::string::npos) {
                    return fallback;
                }
                return token.substr(firstQuote + 1, secondQuote - firstQuote - 1);
            }
        }
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }
    return fallback;
}

std::string parseMarkerValue(const std::string& marker, const std::string& key)
{
    const std::string body = parseMarkerBody(marker);
    std::size_t start = 0;
    while (start < body.size()) {
        const std::size_t comma = body.find(',', start);
        const std::string token = trim(
            body.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
        const std::size_t equals = token.find('=');
        if (equals != std::string::npos && trim(token.substr(0, equals)) == key) {
            return trim(token.substr(equals + 1));
        }
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }
    return {};
}

std::string parseMarkerStringValue(const std::string& marker, const std::string& key)
{
    const std::string value = parseMarkerValue(marker, key);
    const std::size_t firstQuote = value.find('"');
    const std::size_t lastQuote = value.rfind('"');
    if (firstQuote == std::string::npos || lastQuote <= firstQuote)
        return {};
    return value.substr(firstQuote + 1u, lastQuote - firstQuote - 1u);
}

bool markerContainsDefaultConstruct(const std::string& marker)
{
    const std::string body = parseMarkerBody(marker);
    return body.find("Construct") != std::string::npos && body.find("Default") != std::string::npos;
}

std::vector<std::string> parseMarkerFlags(const std::string& marker)
{
    std::vector<std::string> flags;
    std::stringstream stream(parseMarkerBody(marker));
    std::string token;
    while (std::getline(stream, token, ',')) {
        token = trim(token);
        if (token.empty() || token.find('=') != std::string::npos) {
            continue;
        }
        flags.push_back(token);
    }
    return flags;
}

bool hasFlag(const std::vector<std::string>& flags, const std::string& flag)
{
    return std::find(flags.begin(), flags.end(), flag) != flags.end();
}

std::string propertyHintExpression(const std::vector<std::string>& flags)
{
    std::vector<std::string> hints;
    if (hasFlag(flags, "Editor")) {
        hints.emplace_back("::arp::reflection::PropertyHint::Editor");
    }
    if (hasFlag(flags, "Serialize")) {
        hints.emplace_back("::arp::reflection::PropertyHint::Serialize");
    }
    if (hasFlag(flags, "Runtime")) {
        hints.emplace_back("::arp::reflection::PropertyHint::Runtime");
    }
    if (hints.empty()) {
        return "0";
    }
    std::string result = "static_cast<std::uint8_t>(";
    for (std::size_t index = 0; index < hints.size(); ++index) {
        if (index != 0) {
            result += " | ";
        }
        result += hints[index];
    }
    result += ")";
    return result;
}

std::string normalizeAccessorType(std::string type)
{
    type = trim(type);
    for (;;) {
        bool changed = false;
        for (const auto* qualifier : {"const ", "volatile "}) {
            if (startsWith(type, qualifier)) {
                type = trim(type.substr(std::string(qualifier).size()));
                changed = true;
            }
        }
        if (!changed) {
            break;
        }
    }
    while (type.size() >= 1u && type.back() == '&') {
        type.pop_back();
        type = trim(type);
    }

    std::string normalized;
    normalized.reserve(type.size());
    for (const char ch : type) {
        if (std::isspace(static_cast<unsigned char>(ch)) == 0) {
            normalized += ch;
        }
    }
    return normalized;
}

void inferAccessorProperties(ClassModel& cls)
{
    for (std::size_t getterIndex = 0; getterIndex < cls.methods.size(); ++getterIndex) {
        auto& getter = cls.methods[getterIndex];
        if (getter.access != "public" || getter.staticMethod || getter.parameters.size() != 0u
            || getter.returnType == "void" || startsWith(getter.reflectName, "set_")
            || std::find(getter.flags.begin(), getter.flags.end(), "MethodOnly")
                != getter.flags.end()) {
            continue;
        }

        const std::string setterName = "set_" + getter.reflectName;
        for (std::size_t setterIndex = 0; setterIndex < cls.methods.size(); ++setterIndex) {
            const auto& setter = cls.methods[setterIndex];
            if (setter.access != "public" || setter.staticMethod || setter.reflectName != setterName
                || setter.returnType != "void" || setter.parameters.size() != 1u
                || normalizeAccessorType(getter.returnType)
                    != normalizeAccessorType(setter.parameters[0].cppType)) {
                continue;
            }

            PropertyModel property;
            property.name = getter.reflectName;
            property.cppType = getter.returnType;
            property.access = "public";
            property.accessor = true;
            property.getterIndex = getterIndex;
            property.setterIndex = setterIndex;
            cls.properties.push_back(std::move(property));
            getter.accessorGetter = true;
            break;
        }
    }
}

std::vector<std::string> splitTopLevel(const std::string& text)
{
    std::vector<std::string> result;
    std::size_t start = 0;
    int angleDepth = 0;
    int parenDepth = 0;
    int bracketDepth = 0;
    for (std::size_t index = 0; index < text.size(); ++index) {
        switch (text[index]) {
        case '<':
            ++angleDepth;
            break;
        case '>':
            if (angleDepth > 0)
                --angleDepth;
            break;
        case '(':
            ++parenDepth;
            break;
        case ')':
            if (parenDepth > 0)
                --parenDepth;
            break;
        case '[':
            ++bracketDepth;
            break;
        case ']':
            if (bracketDepth > 0)
                --bracketDepth;
            break;
        case ',':
            if (angleDepth == 0 && parenDepth == 0 && bracketDepth == 0) {
                result.push_back(trim(text.substr(start, index - start)));
                start = index + 1;
            }
            break;
        default:
            break;
        }
    }
    const std::string tail = trim(text.substr(start));
    if (!tail.empty()) {
        result.push_back(tail);
    }
    return result;
}

std::string stripTopLevelDefault(const std::string& value)
{
    int angleDepth = 0;
    int parenDepth = 0;
    int bracketDepth = 0;
    for (std::size_t index = 0; index < value.size(); ++index) {
        switch (value[index]) {
        case '<':
            ++angleDepth;
            break;
        case '>':
            if (angleDepth > 0)
                --angleDepth;
            break;
        case '(':
            ++parenDepth;
            break;
        case ')':
            if (parenDepth > 0)
                --parenDepth;
            break;
        case '[':
            ++bracketDepth;
            break;
        case ']':
            if (bracketDepth > 0)
                --bracketDepth;
            break;
        case '=':
            if (angleDepth == 0 && parenDepth == 0 && bracketDepth == 0) {
                return trim(value.substr(0, index));
            }
            break;
        default:
            break;
        }
    }
    return trim(value);
}

std::size_t matchingParen(const std::string& value, std::size_t open)
{
    if (open >= value.size() || value[open] != '(') {
        return std::string::npos;
    }
    int depth = 0;
    for (std::size_t index = open; index < value.size(); ++index) {
        if (value[index] == '(') {
            ++depth;
        } else if (value[index] == ')') {
            --depth;
            if (depth == 0) {
                return index;
            }
        }
    }
    return std::string::npos;
}

std::string referenceKind(const std::string& cppType)
{
    if (cppType.find("&&") != std::string::npos) {
        return "rvalue";
    }
    if (cppType.find('&') != std::string::npos) {
        return "lvalue";
    }
    if (cppType.find('*') != std::string::npos) {
        return "pointer";
    }
    return "value";
}

bool nullableType(const std::string& cppType)
{
    return cppType.find('*') != std::string::npos || cppType.find("optional<") != std::string::npos
        || cppType.find("Optional<") != std::string::npos;
}

std::vector<ParameterModel> parseParameters(const std::string& args)
{
    std::vector<ParameterModel> parameters;
    for (const auto& raw : splitTopLevel(args)) {
        const std::string declaration = stripTopLevelDefault(raw);
        if (declaration.empty() || declaration == "void") {
            continue;
        }
        ParameterModel parameter;
        std::size_t nameEnd = declaration.size();
        while (nameEnd > 0
            && std::isspace(static_cast<unsigned char>(declaration[nameEnd - 1])) != 0) {
            --nameEnd;
        }
        std::size_t nameBegin = nameEnd;
        while (nameBegin > 0) {
            const char ch = declaration[nameBegin - 1];
            if (std::isalnum(static_cast<unsigned char>(ch)) == 0 && ch != '_') {
                break;
            }
            --nameBegin;
        }
        if (nameBegin < nameEnd) {
            parameter.name = declaration.substr(nameBegin, nameEnd - nameBegin);
            parameter.cppType = trim(declaration.substr(0, nameBegin));
        } else {
            parameter.cppType = declaration;
        }
        if (parameter.cppType.empty()) {
            parameter.cppType = declaration;
            parameter.name.clear();
        }
        parameter.referenceKind = referenceKind(parameter.cppType);
        parameter.nullable = nullableType(parameter.cppType);
        parameters.push_back(std::move(parameter));
    }
    return parameters;
}

void annotateSignature(const std::string& returnType,
    const std::string& args,
    const std::vector<std::string>& flags,
    std::vector<ParameterModel>& parameters,
    std::string& returnKind,
    bool& nullableReturn,
    bool& maySuspend)
{
    parameters = parseParameters(args);
    returnKind = referenceKind(returnType);
    nullableReturn =
        nullableType(returnType) || hasFlag(flags, "Nullable") || hasFlag(flags, "NullableReturn");
    maySuspend = hasFlag(flags, "Async") || hasFlag(flags, "Await") || hasFlag(flags, "MaySuspend");
}

std::vector<std::string> readLines(const std::filesystem::path& path, std::string& diagnostics)
{
    std::ifstream input(path);
    if (!input.good()) {
        diagnostics = "failed to open header: " + path.generic_string();
        return {};
    }
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(input, line)) {
        lines.push_back(line);
    }
    return lines;
}

std::size_t nextMeaningfulLine(const std::vector<std::string>& lines, std::size_t start)
{
    for (std::size_t index = start; index < lines.size(); ++index) {
        if (!cleanLine(lines[index]).empty()) {
            return index;
        }
    }
    return lines.size();
}

bool parseTypeName(const std::string& declaration, const std::string& keyword, std::string& outName)
{
    const std::size_t keywordPos = declaration.find(keyword);
    if (keywordPos == std::string::npos) {
        return false;
    }
    std::string tail = trim(declaration.substr(keywordPos + keyword.size()));
    const std::size_t brace = tail.find('{');
    if (brace != std::string::npos) {
        tail = tail.substr(0, brace);
    }
    const std::size_t inherit = tail.find(':');
    if (inherit != std::string::npos) {
        tail = tail.substr(0, inherit);
    }
    std::stringstream stream(trim(tail));
    std::string token;
    while (stream >> token) {
        if (token == "ARP_API" || token == "final" || token == "__attribute__"
            || token == "__declspec") {
            continue;
        }
        outName = token;
        return true;
    }
    return false;
}

std::vector<std::string> parseBaseNames(
    const std::string& declaration, const std::string& namespaceName)
{
    std::vector<std::string> bases;
    const std::size_t colon = declaration.find(':');
    if (colon == std::string::npos) {
        return bases;
    }
    const std::size_t brace = declaration.find('{', colon + 1);
    std::string baseList = declaration.substr(
        colon + 1, brace == std::string::npos ? std::string::npos : brace - colon - 1);
    std::stringstream stream(baseList);
    std::string token;
    while (std::getline(stream, token, ',')) {
        token = trim(token);
        std::stringstream words(token);
        std::string word;
        std::string base;
        while (words >> word) {
            if (word == "public" || word == "protected" || word == "private" || word == "virtual") {
                continue;
            }
            base = word;
        }
        if (base.empty()) {
            continue;
        }
        if (base.find("::") == std::string::npos && !namespaceName.empty()) {
            base = namespaceName + "::" + base;
        }
        bases.push_back(std::move(base));
    }
    return bases;
}

bool parseFieldDeclaration(const std::string& line, PropertyModel& out, std::string& diagnostics)
{
    std::string declaration = cleanLine(line);
    const std::size_t semicolon = declaration.find(';');
    if (semicolon == std::string::npos) {
        diagnostics = "APROP must be followed by a field declaration ending in ';'";
        return false;
    }
    declaration = trim(declaration.substr(0, semicolon));
    const std::size_t equals = declaration.find('=');
    const std::size_t braceInit = declaration.find('{');
    std::size_t initializer = std::string::npos;
    if (equals != std::string::npos && braceInit != std::string::npos) {
        initializer = std::min(equals, braceInit);
    } else if (equals != std::string::npos) {
        initializer = equals;
    } else {
        initializer = braceInit;
    }
    if (initializer != std::string::npos) {
        declaration = trim(declaration.substr(0, initializer));
    }
    if (declaration.empty()) {
        diagnostics = "APROP field declaration is empty";
        return false;
    }

    // Locate the field name, tolerating a trailing array declarator such as
    // `float color[4]` (the `[4]` suffix must not be mistaken for part of the name).
    std::string base = declaration;
    {
        std::size_t arrScan = base.size();
        while (arrScan > 0 && base[arrScan - 1] == ']') {
            std::size_t open = base.rfind('[', arrScan - 1);
            if (open == std::string::npos)
                break;
            bool dimOk = true;
            for (std::size_t k = open + 1; k + 1 < arrScan; ++k) {
                if (!std::isdigit(static_cast<unsigned char>(base[k]))) {
                    dimOk = false;
                    break;
                }
            }
            if (!dimOk)
                break;
            arrScan = open;
        }
        base = base.substr(0, arrScan);
    }
    std::size_t nameEnd = base.size();
    while (nameEnd > 0 && std::isspace(static_cast<unsigned char>(base[nameEnd - 1])) != 0) {
        --nameEnd;
    }
    std::size_t nameBegin = nameEnd;
    while (nameBegin > 0) {
        const char ch = base[nameBegin - 1];
        if (std::isalnum(static_cast<unsigned char>(ch)) == 0 && ch != '_') {
            break;
        }
        --nameBegin;
    }
    if (nameBegin == nameEnd) {
        diagnostics = "failed to parse APROP field name";
        return false;
    }

    out.name = base.substr(nameBegin, nameEnd - nameBegin);
    out.cppType = trim(declaration.substr(0, nameBegin));
    if (out.cppType.empty()) {
        diagnostics = "failed to parse APROP field type";
        return false;
    }
    return true;
}

bool collectMethodDeclaration(const std::vector<std::string>& lines,
    std::size_t startIndex,
    std::string& declaration,
    std::size_t& consumedIndex,
    std::string& diagnostics)
{
    declaration.clear();
    bool sawParameterList = false;
    int parenthesisDepth = 0;

    for (std::size_t index = startIndex; index < lines.size(); ++index) {
        const std::string line = cleanLine(lines[index]);
        if (line.empty()) {
            continue;
        }
        if (!declaration.empty()) {
            declaration += ' ';
        }
        declaration += line;

        bool closedParameterListOnThisLine = false;
        for (std::size_t offset = 0; offset < line.size(); ++offset) {
            const char ch = line[offset];
            if (ch == '(') {
                sawParameterList = true;
                ++parenthesisDepth;
                continue;
            }
            if (ch != ')' || !sawParameterList) {
                continue;
            }
            --parenthesisDepth;
            if (parenthesisDepth != 0) {
                continue;
            }
            closedParameterListOnThisLine = true;

            // A declaration is complete once the parameter list closes and
            // the declaration either starts its body or ends with ';'. If
            // qualifiers continue on the next line, keep collecting them.
            const std::string suffix = trim(line.substr(offset + 1));
            if (suffix.find('{') != std::string::npos || suffix.find(';') != std::string::npos) {
                consumedIndex = index;
                return true;
            }
        }

        if (!closedParameterListOnThisLine && sawParameterList && parenthesisDepth == 0
            && (line.find('{') != std::string::npos || line.find(';') != std::string::npos)) {
            consumedIndex = index;
            return true;
        }
    }

    diagnostics = "AFUNC must be followed by a complete method declaration";
    return false;
}

bool parseMethodDeclaration(const std::string& line, MethodModel& out, std::string& diagnostics)
{
    std::string declaration = cleanLine(line);
    const std::size_t body = declaration.find('{');
    if (body != std::string::npos) {
        declaration = trim(declaration.substr(0, body));
    }
    const std::size_t semicolon = declaration.find(';');
    if (semicolon != std::string::npos) {
        declaration = trim(declaration.substr(0, semicolon));
    }
    const std::size_t open = declaration.find('(');
    const std::size_t close = matchingParen(declaration, open);
    if (open == std::string::npos || close == std::string::npos || close < open) {
        diagnostics = "AFUNC must be followed by a method declaration";
        return false;
    }

    const std::string suffix = trim(declaration.substr(close + 1));
    out.constMethod = suffix.find("const") != std::string::npos;
    out.volatileMethod = suffix.find("volatile") != std::string::npos;
    out.noexceptMethod = suffix.find("noexcept") != std::string::npos;
    if (suffix.find("&&") != std::string::npos) {
        out.refQualifier = "&&";
    } else if (suffix.find('&') != std::string::npos) {
        out.refQualifier = "&";
    }

    std::string prefix = trim(declaration.substr(0, open));
    out.args = trim(declaration.substr(open + 1, close - open - 1));
    std::size_t nameEnd = prefix.size();
    while (nameEnd > 0 && std::isspace(static_cast<unsigned char>(prefix[nameEnd - 1])) != 0) {
        --nameEnd;
    }
    std::size_t nameBegin = nameEnd;
    while (nameBegin > 0) {
        const char ch = prefix[nameBegin - 1];
        if (std::isalnum(static_cast<unsigned char>(ch)) == 0 && ch != '_') {
            break;
        }
        --nameBegin;
    }
    if (nameBegin == nameEnd) {
        diagnostics = "failed to parse AFUNC method name";
        return false;
    }
    out.name = prefix.substr(nameBegin, nameEnd - nameBegin);
    out.returnType = trim(prefix.substr(0, nameBegin));
    if (out.returnType.empty()) {
        diagnostics = "AFUNC constructors are not supported in this generator slice";
        return false;
    }
    out.staticMethod = hasFlag(out.flags, "Static");
    for (;;) {
        bool stripped = false;
        for (const auto* qualifier :
            {"inline ", "constexpr ", "consteval ", "virtual ", "friend "}) {
            if (startsWith(out.returnType, qualifier)) {
                out.returnType = trim(out.returnType.substr(std::string(qualifier).size()));
                stripped = true;
                break;
            }
        }
        if (stripped) {
            continue;
        }
        if (startsWith(out.returnType, "static ")) {
            out.staticMethod = true;
            out.returnType = trim(out.returnType.substr(std::string("static ").size()));
            continue;
        }
        break;
    }
    return true;
}

bool parseConstructorDeclaration(const std::string& line,
    const std::string& className,
    std::vector<ParameterModel>& parameters,
    std::string& diagnostics)
{
    std::string declaration = cleanLine(line);
    const std::size_t body = declaration.find('{');
    if (body != std::string::npos) {
        declaration = trim(declaration.substr(0, body));
    }
    const std::size_t semicolon = declaration.find(';');
    if (semicolon != std::string::npos) {
        declaration = trim(declaration.substr(0, semicolon));
    }
    const std::size_t open = declaration.find('(');
    const std::size_t close = matchingParen(declaration, open);
    if (open == std::string::npos || close == std::string::npos || close < open) {
        diagnostics = "ACONSTRUCT must be followed by a constructor declaration";
        return false;
    }

    const std::string prefix = trim(declaration.substr(0, open));
    if (prefix != className) {
        diagnostics = "ACONSTRUCT declaration does not name " + className;
        return false;
    }
    parameters = parseParameters(trim(declaration.substr(open + 1, close - open - 1)));
    return true;
}

bool parseClass(const std::vector<std::string>& lines,
    std::size_t markerIndex,
    const std::vector<std::string>& namespaces,
    const char* marker,
    const char* keyword,
    ClassModel& out,
    std::size_t& consumedIndex,
    std::string& diagnostics)
{
    const std::string markerLine = cleanLine(lines[markerIndex]);
    const std::size_t declarationIndex = nextMeaningfulLine(lines, markerIndex + 1);
    if (declarationIndex >= lines.size()) {
        diagnostics = std::string(marker) + " must be followed by a type declaration";
        return false;
    }

    std::string typeName;
    if (!parseTypeName(cleanLine(lines[declarationIndex]), keyword, typeName)) {
        diagnostics = std::string(marker) + " must be followed by '" + keyword + " <Name>'";
        return false;
    }

    out.kind = keyword;
    out.namespaceName = joinNamespace(namespaces);
    out.name = typeName;
    out.cppName = qualifiedName(out.namespaceName, out.name);
    out.reflectName = parseNameOverride(markerLine, out.name);
    out.defaultConstruct = markerContainsDefaultConstruct(markerLine);
    out.ownership = parseMarkerValue(markerLine, "Ownership");
    if (out.ownership.empty()) {
        out.ownership = "unspecified";
    }
    out.lifecycle = parseMarkerValue(markerLine, "Lifecycle");
    if (out.lifecycle.empty()) {
        out.lifecycle = "unspecified";
    }
    out.moduleExportName = parseMarkerValue(markerLine, "Export");
    if (out.moduleExportName.size() >= 2u && out.moduleExportName.front() == '"'
        && out.moduleExportName.back() == '"') {
        out.moduleExportName = out.moduleExportName.substr(1u, out.moduleExportName.size() - 2u);
    }
    out.bases = parseBaseNames(cleanLine(lines[declarationIndex]), out.namespaceName);

    auto braceDelta = [](const std::string& value) {
        int delta = 0;
        for (const char ch : value) {
            if (ch == '{') {
                ++delta;
            } else if (ch == '}') {
                --delta;
            }
        }
        return delta;
    };
    auto applyBraceRange = [&](std::size_t first, std::size_t last, int& depth) {
        for (std::size_t current = first; current <= last && current < lines.size(); ++current) {
            depth += braceDelta(cleanLine(lines[current]));
        }
    };

    std::string currentAccess = std::string(keyword) == "struct" ? "public" : "private";
    int braceDepth = braceDelta(cleanLine(lines[declarationIndex]));
    bool sawOpenBrace = braceDepth > 0;
    for (std::size_t index = declarationIndex + 1; index < lines.size(); ++index) {
        const std::string line = cleanLine(lines[index]);
        if (line.empty()) {
            continue;
        }
        if (!sawOpenBrace) {
            braceDepth += braceDelta(line);
            sawOpenBrace = braceDepth > 0;
            continue;
        }
        if (line == "public:" || line == "protected:" || line == "private:") {
            currentAccess = line.substr(0, line.size() - 1);
            continue;
        }
        if (isTypeCloseLine(line)) {
            if (braceDepth <= 1) {
                consumedIndex = index;
                return true;
            }
            --braceDepth;
            continue;
        }
        if (markerStartsWith(line, "APROP")) {
            const std::size_t fieldIndex = nextMeaningfulLine(lines, index + 1);
            if (fieldIndex >= lines.size()) {
                diagnostics = "APROP must be followed by a field declaration";
                return false;
            }
            PropertyModel property;
            property.access = currentAccess;
            property.flags = parseMarkerFlags(line);
            property.doc = parseMarkerStringValue(line, "Doc");
            if (!parseFieldDeclaration(lines[fieldIndex], property, diagnostics)) {
                diagnostics += ": ";
                diagnostics += cleanLine(lines[fieldIndex]);
                return false;
            }
            out.properties.push_back(property);
            applyBraceRange(fieldIndex, fieldIndex, braceDepth);
            index = fieldIndex;
            continue;
        }
        if (markerStartsWith(line, "ACONSTRUCT")) {
            const std::size_t constructorIndex = nextMeaningfulLine(lines, index + 1);
            if (constructorIndex >= lines.size()) {
                diagnostics = "ACONSTRUCT must be followed by a constructor declaration";
                return false;
            }
            std::string declaration;
            std::size_t constructorConsumed = constructorIndex;
            if (!collectMethodDeclaration(
                    lines, constructorIndex, declaration, constructorConsumed, diagnostics)) {
                diagnostics += ": ";
                diagnostics += cleanLine(lines[constructorIndex]);
                return false;
            }
            if (currentAccess != "public") {
                diagnostics = "non-public ACONSTRUCT is not supported: " + out.cppName;
                return false;
            }
            std::vector<ParameterModel> parameters;
            if (!parseConstructorDeclaration(declaration, out.name, parameters, diagnostics)) {
                diagnostics += ": ";
                diagnostics += declaration;
                return false;
            }
            out.constructors.push_back(std::move(parameters));
            applyBraceRange(constructorIndex, constructorConsumed, braceDepth);
            index = constructorConsumed;
            continue;
        }
        if (markerStartsWith(line, "AFUNC")) {
            const std::size_t methodIndex = nextMeaningfulLine(lines, index + 1);
            if (methodIndex >= lines.size()) {
                diagnostics = "AFUNC must be followed by a method declaration";
                return false;
            }
            std::string declaration;
            std::size_t methodConsumed = methodIndex;
            if (!collectMethodDeclaration(
                    lines, methodIndex, declaration, methodConsumed, diagnostics)) {
                diagnostics += ": ";
                diagnostics += cleanLine(lines[methodIndex]);
                return false;
            }
            MethodModel method;
            method.access = currentAccess;
            method.flags = parseMarkerFlags(line);
            if (!parseMethodDeclaration(declaration, method, diagnostics)) {
                diagnostics += ": ";
                diagnostics += declaration;
                return false;
            }
            method.reflectName = parseNameOverride(line, method.name);
            annotateSignature(method.returnType,
                method.args,
                method.flags,
                method.parameters,
                method.returnReferenceKind,
                method.nullableReturn,
                method.maySuspend);
            out.methods.push_back(method);
            applyBraceRange(methodIndex, methodConsumed, braceDepth);
            index = methodConsumed;
            continue;
        }
        braceDepth += braceDelta(line);
    }

    diagnostics = "unterminated reflected type: " + out.cppName;
    return false;
}

bool parseFunction(const std::vector<std::string>& lines,
    std::size_t markerIndex,
    const std::vector<std::string>& namespaces,
    FunctionModel& out,
    std::size_t& consumedIndex,
    std::string& diagnostics)
{
    const std::string markerLine = cleanLine(lines[markerIndex]);
    const std::size_t declarationIndex = nextMeaningfulLine(lines, markerIndex + 1);
    if (declarationIndex >= lines.size()) {
        diagnostics = "AFUNC must be followed by a function declaration";
        return false;
    }

    std::string methodDeclaration;
    std::size_t declarationConsumed = declarationIndex;
    if (!collectMethodDeclaration(
            lines, declarationIndex, methodDeclaration, declarationConsumed, diagnostics)) {
        diagnostics += ": ";
        diagnostics += cleanLine(lines[declarationIndex]);
        return false;
    }

    MethodModel declaration;
    if (!parseMethodDeclaration(methodDeclaration, declaration, diagnostics)) {
        diagnostics += ": ";
        diagnostics += methodDeclaration;
        return false;
    }
    out.namespaceName = joinNamespace(namespaces);
    out.name = declaration.name;
    out.cppName = qualifiedName(out.namespaceName, out.name);
    out.reflectName = parseNameOverride(markerLine, out.name);
    out.returnType = declaration.returnType;
    out.args = declaration.args;
    out.defaultArguments = parseMarkerValue(markerLine, "DefaultArguments");
    out.flags = parseMarkerFlags(markerLine);
    annotateSignature(out.returnType,
        out.args,
        out.flags,
        out.parameters,
        out.returnReferenceKind,
        out.nullableReturn,
        out.maySuspend);
    consumedIndex = declarationConsumed;
    return true;
}

bool parseEnum(const std::vector<std::string>& lines,
    std::size_t markerIndex,
    const std::vector<std::string>& namespaces,
    EnumModel& out,
    std::size_t& consumedIndex,
    std::string& diagnostics)
{
    const std::string markerLine = cleanLine(lines[markerIndex]);
    const std::size_t declarationIndex = nextMeaningfulLine(lines, markerIndex + 1);
    if (declarationIndex >= lines.size()) {
        diagnostics = "AENUM must be followed by an enum class declaration";
        return false;
    }
    if (!parseTypeName(cleanLine(lines[declarationIndex]), "enum class", out.name)) {
        diagnostics = "AENUM must be followed by 'enum class <Name>'";
        return false;
    }
    out.namespaceName = joinNamespace(namespaces);
    out.cppName = qualifiedName(out.namespaceName, out.name);
    out.reflectName = parseNameOverride(markerLine, out.name);

    bool sawOpenBrace = cleanLine(lines[declarationIndex]).find('{') != std::string::npos;
    for (std::size_t index = declarationIndex + 1; index < lines.size(); ++index) {
        std::string line = cleanLine(lines[index]);
        if (line.empty()) {
            continue;
        }
        if (!sawOpenBrace) {
            sawOpenBrace = line.find('{') != std::string::npos;
            continue;
        }
        if (isTypeCloseLine(line)) {
            consumedIndex = index;
            return true;
        }

        std::stringstream stream(line);
        std::string item;
        while (std::getline(stream, item, ',')) {
            item = trim(item);
            if (item.empty()) {
                continue;
            }
            const std::size_t equals = item.find('=');
            if (equals != std::string::npos) {
                item = trim(item.substr(0, equals));
            }
            if (!item.empty()) {
                out.values.push_back(item);
            }
        }
    }

    diagnostics = "unterminated reflected enum: " + out.cppName;
    return false;
}

ParseResult parseHeaders(const std::vector<std::filesystem::path>& paths)
{
    ParseResult result;
    result.success = true;
    for (const auto& path : paths) {
        std::string diagnostics;
        const auto lines = readLines(path, diagnostics);
        if (!diagnostics.empty()) {
            return {false, diagnostics, {}};
        }

        HeaderModel header;
        header.path = path.generic_string();
        std::vector<std::string> namespaces;
        for (std::size_t index = 0; index < lines.size(); ++index) {
            const std::string rawLine = trim(lines[index]);
            if (isNamespaceCloseLine(rawLine) && !namespaces.empty()) {
                namespaces.pop_back();
                continue;
            }
            const std::string line = cleanLine(lines[index]);
            if (line.empty()) {
                continue;
            }
            if (startsWith(line, "namespace ") && line.find('{') != std::string::npos) {
                std::string namespaceName = trim(line.substr(std::string("namespace ").size()));
                namespaceName = trim(namespaceName.substr(0, namespaceName.find('{')));
                namespaces.push_back(namespaceName);
                continue;
            }
            if (isNamespaceCloseLine(line) && !namespaces.empty()) {
                namespaces.pop_back();
                continue;
            }
            if (startsWith(line, "using ") && line.back() == ';') {
                const auto declaration = trim(line.substr(6u, line.size() - 7u));
                const auto equals = declaration.find('=');
                AliasModel alias;
                alias.namespaceName = joinNamespace(namespaces);
                if (equals == std::string::npos) {
                    alias.target = trim(declaration);
                    const auto separator = alias.target.rfind("::");
                    alias.name = separator == std::string::npos
                        ? alias.target
                        : alias.target.substr(separator + 2u);
                } else {
                    alias.name = trim(declaration.substr(0u, equals));
                    alias.target = trim(declaration.substr(equals + 1u));
                }
                if (!alias.name.empty() && !alias.target.empty()
                    && alias.name.find("::") == std::string::npos) {
                    header.aliases.push_back(std::move(alias));
                }
                continue;
            }
            if (markerStartsWith(line, "ACLASS")) {
                ClassModel cls;
                std::size_t consumed = index;
                if (!parseClass(lines,
                        index,
                        namespaces,
                        "ACLASS",
                        "class",
                        cls,
                        consumed,
                        result.diagnostics)) {
                    result.success = false;
                    return result;
                }
                inferAccessorProperties(cls);
                header.classes.push_back(cls);
                index = consumed;
                continue;
            }
            if (markerStartsWith(line, "ASTRUCT")) {
                ClassModel cls;
                std::size_t consumed = index;
                if (!parseClass(lines,
                        index,
                        namespaces,
                        "ASTRUCT",
                        "struct",
                        cls,
                        consumed,
                        result.diagnostics)) {
                    result.success = false;
                    return result;
                }
                inferAccessorProperties(cls);
                header.classes.push_back(cls);
                index = consumed;
                continue;
            }
            if (markerStartsWith(line, "AFUNC")) {
                FunctionModel function;
                std::size_t consumed = index;
                if (!parseFunction(
                        lines, index, namespaces, function, consumed, result.diagnostics)) {
                    result.success = false;
                    return result;
                }
                header.functions.push_back(std::move(function));
                index = consumed;
                continue;
            }
            if (markerStartsWith(line, "AENUM")) {
                EnumModel enumeration;
                std::size_t consumed = index;
                if (!parseEnum(
                        lines, index, namespaces, enumeration, consumed, result.diagnostics)) {
                    result.success = false;
                    return result;
                }
                header.enums.push_back(enumeration);
                index = consumed;
                continue;
            }
        }
        result.headers.push_back(header);
    }

    // A generated manifest is an ABI surface, so ambiguous reflected names
    // must fail at generation time instead of depending on registration order.
    std::map<std::string, std::string> reflectedTypes;
    std::map<std::string, std::string> reflectedFunctions;
    std::string moduleObjectExport;
    for (const auto& header : result.headers) {
        for (const auto& cls : header.classes) {
            if (cls.ownership != "unspecified" && cls.ownership != "Engine"
                && cls.ownership != "Value") {
                result.success = false;
                result.diagnostics = "unsupported reflection ownership contract '" + cls.ownership
                    + "' for " + cls.cppName;
                return result;
            }
            if (cls.lifecycle != "unspecified" && cls.lifecycle != "Explicit"
                && cls.lifecycle != "Value") {
                result.success = false;
                result.diagnostics = "unsupported reflection lifecycle contract '" + cls.lifecycle
                    + "' for " + cls.cppName;
                return result;
            }
            const auto typeIt = reflectedTypes.find(cls.reflectName);
            if (typeIt != reflectedTypes.end()) {
                result.success = false;
                result.diagnostics = "duplicate reflected type '" + cls.reflectName + "' in "
                    + typeIt->second + " and " + header.path;
                return result;
            }
            reflectedTypes.emplace(cls.reflectName, header.path);
            if (!cls.moduleExportName.empty()) {
                if (!moduleObjectExport.empty()) {
                    result.success = false;
                    result.diagnostics = "multiple module object exports are not supported: '"
                        + moduleObjectExport + "' and '" + cls.moduleExportName + "'";
                    return result;
                }
                moduleObjectExport = cls.moduleExportName;
            }

            std::map<std::string, std::string> members;
            std::map<std::string, std::string> reflectedMethods;
            if (cls.constructors.size() > 1u
                || (cls.defaultConstruct && !cls.constructors.empty())) {
                result.success = false;
                result.diagnostics = "multiple reflected constructor paths are not supported for '"
                    + cls.cppName + "'; expose distinct factory methods instead";
                return result;
            }
            for (const auto& property : cls.properties) {
                if (members.find(property.name) != members.end()) {
                    result.success = false;
                    result.diagnostics =
                        "duplicate reflected member '" + property.name + "' in " + cls.cppName;
                    return result;
                }
                members.emplace(property.name, "property");
            }
            for (const auto& method : cls.methods) {
                if (method.accessorGetter) {
                    continue;
                }
                const auto memberIt = members.find(method.reflectName);
                if (memberIt != members.end()) {
                    result.success = false;
                    result.diagnostics =
                        "duplicate reflected member '" + method.reflectName + "' in " + cls.cppName;
                    return result;
                }
                const auto methodNameIt = reflectedMethods.find(method.reflectName);
                if (methodNameIt != reflectedMethods.end()) {
                    result.success = false;
                    result.diagnostics = "duplicate reflected method name '" + method.reflectName
                        + "' in " + cls.cppName
                        + "; overloaded C++ methods require distinct Name values";
                    return result;
                }
                reflectedMethods.emplace(method.reflectName, method.name);
                const std::string signature = method.reflectName + "(" + method.args + ")";
                if (!members.emplace(signature, "method").second) {
                    result.success = false;
                    result.diagnostics = "duplicate reflected method signature '" + signature
                        + "' in " + cls.cppName;
                    return result;
                }
            }
        }
        for (const auto& function : header.functions) {
            const auto functionIt = reflectedFunctions.find(function.reflectName);
            if (functionIt != reflectedFunctions.end()) {
                result.success = false;
                result.diagnostics = "duplicate reflected function '" + function.reflectName
                    + "' in " + functionIt->second + " and " + header.path;
                return result;
            }
            reflectedFunctions.emplace(function.reflectName, header.path);
        }
    }
    return result;
}

bool parseOptions(int argc, char** argv, ProgramOptions& options, std::string& diagnostics)
{
    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        auto requireValue = [&](const char* name) -> std::string {
            if (index + 1 >= argc) {
                diagnostics = std::string("missing value for ") + name;
                return {};
            }
            ++index;
            return argv[index];
        };

        if (arg == "--module") {
            options.module = requireValue("--module");
        } else if (arg == "--out-json") {
            options.outJson = requireValue("--out-json");
        } else if (arg == "--out-cpp") {
            options.outCpp = requireValue("--out-cpp");
        } else if (arg == "--header") {
            options.headers.push_back(requireValue("--header"));
        } else {
            diagnostics = "unknown argument: " + arg;
            return false;
        }
        if (!diagnostics.empty()) {
            return false;
        }
    }

    if (options.module.empty()) {
        diagnostics = "missing required --module";
        return false;
    }
    if (options.outJson.empty()) {
        diagnostics = "missing required --out-json";
        return false;
    }
    if (options.outCpp.empty()) {
        diagnostics = "missing required --out-cpp";
        return false;
    }
    if (options.headers.empty()) {
        diagnostics = "at least one --header is required";
        return false;
    }
    return true;
}

bool isIdentifierCharacter(char value)
{
    return std::isalnum(static_cast<unsigned char>(value)) != 0 || value == '_';
}

std::string qualifyKnownTypes(const std::string& type,
    const std::vector<HeaderModel>& headers,
    const std::string& namespaceName)
{
    std::string result = type;
    for (const auto& header : headers) {
        for (const auto& alias : header.aliases) {
            if (alias.namespaceName != namespaceName) {
                continue;
            }
            std::size_t position = 0;
            while ((position = result.find(alias.name, position)) != std::string::npos) {
                const bool leftIdentifier =
                    position > 0 && isIdentifierCharacter(result[position - 1]);
                const std::size_t end = position + alias.name.size();
                const bool rightIdentifier =
                    end < result.size() && isIdentifierCharacter(result[end]);
                const bool alreadyQualified =
                    position >= 2u && result[position - 1] == ':' && result[position - 2] == ':';
                if (!leftIdentifier && !rightIdentifier && !alreadyQualified) {
                    result.replace(position, alias.name.size(), alias.target);
                    position += alias.target.size();
                } else {
                    position = end;
                }
            }
        }
    }
    for (const auto& header : headers) {
        for (const auto& cls : header.classes) {
            if (cls.name.empty() || cls.namespaceName.empty()) {
                continue;
            }
            const std::string qualified = cls.cppName;
            std::size_t position = 0;
            while ((position = result.find(cls.name, position)) != std::string::npos) {
                const bool leftIdentifier =
                    position > 0 && isIdentifierCharacter(result[position - 1]);
                const std::size_t end = position + cls.name.size();
                const bool rightIdentifier =
                    end < result.size() && isIdentifierCharacter(result[end]);
                const bool alreadyQualified =
                    position >= 2u && result[position - 1] == ':' && result[position - 2] == ':';
                if (!leftIdentifier && !rightIdentifier && !alreadyQualified) {
                    result.replace(position, cls.name.size(), qualified);
                    position += qualified.size();
                } else {
                    position = end;
                }
            }
        }
        for (const auto& enumeration : header.enums) {
            if (enumeration.name.empty() || enumeration.namespaceName.empty()) {
                continue;
            }
            const std::string qualified = enumeration.cppName;
            std::size_t position = 0;
            while ((position = result.find(enumeration.name, position)) != std::string::npos) {
                const bool leftIdentifier =
                    position > 0 && isIdentifierCharacter(result[position - 1]);
                const std::size_t end = position + enumeration.name.size();
                const bool rightIdentifier =
                    end < result.size() && isIdentifierCharacter(result[end]);
                const bool alreadyQualified =
                    position >= 2u && result[position - 1] == ':' && result[position - 2] == ':';
                if (!leftIdentifier && !rightIdentifier && !alreadyQualified) {
                    result.replace(position, enumeration.name.size(), qualified);
                    position += qualified.size();
                } else {
                    position = end;
                }
            }
        }
    }
    return result;
}

bool isBuiltinTypeToken(const std::string& token)
{
    return token == "const" || token == "volatile" || token == "signed" || token == "unsigned"
        || token == "short" || token == "long" || token == "void" || token == "bool"
        || token == "char" || token == "wchar_t" || token == "char8_t" || token == "char16_t"
        || token == "char32_t" || token == "int" || token == "float" || token == "double"
        || token == "auto";
}

std::string qualifyUnqualifiedTypeTokens(const std::string& type, const std::string& namespaceName)
{
    if (namespaceName.empty() || type.find("::") != std::string::npos) {
        return type;
    }
    std::string result;
    std::size_t index = 0;
    while (index < type.size()) {
        if (!isIdentifierCharacter(type[index])) {
            result.push_back(type[index++]);
            continue;
        }
        const std::size_t start = index;
        while (index < type.size() && isIdentifierCharacter(type[index])) {
            ++index;
        }
        const std::string token = type.substr(start, index - start);
        if (isBuiltinTypeToken(token)) {
            result += token;
        } else if (token == "Value") {
            // Value is declared in the parent scriptvm namespace and is
            // intentionally consumed by several nested plugin namespaces.
            result += "::arp::scriptvm::Value";
        } else {
            result += namespaceName + "::" + token;
        }
    }
    return result;
}

std::string parameterTypeList(const std::vector<ParameterModel>& parameters,
    const std::vector<HeaderModel>& headers,
    const std::string& namespaceName)
{
    std::string result;
    for (std::size_t index = 0; index < parameters.size(); ++index) {
        if (index != 0) {
            result += ", ";
        }
        result += qualifyUnqualifiedTypeTokens(
            qualifyKnownTypes(parameters[index].cppType, headers, namespaceName), namespaceName);
    }
    return result;
}

std::string methodPointerExpression(
    const ClassModel& cls, const MethodModel& method, const std::vector<HeaderModel>& headers)
{
    const auto parameters = parameterTypeList(method.parameters, headers, cls.namespaceName);
    std::string signature = qualifyUnqualifiedTypeTokens(
        qualifyKnownTypes(method.returnType, headers, cls.namespaceName), cls.namespaceName);
    if (method.staticMethod) {
        signature += " (*) (";
        signature += parameters;
        signature += ")";
    } else {
        signature += " (";
        signature += cls.cppName;
        signature += "::*) (";
        signature += parameters;
        signature += ")";
        if (method.constMethod) {
            signature += " const";
        }
        if (method.volatileMethod) {
            signature += " volatile";
        }
        if (!method.refQualifier.empty()) {
            signature += " ";
            signature += method.refQualifier;
        }
    }
    if (method.noexceptMethod) {
        signature += " noexcept";
    }
    return "static_cast<" + signature + ">(&" + cls.cppName + "::" + method.name + ")";
}

std::string functionPointerExpression(
    const FunctionModel& function, const std::vector<HeaderModel>& headers)
{
    const auto parameters = parameterTypeList(function.parameters, headers, function.namespaceName);
    return "static_cast<"
        + qualifyUnqualifiedTypeTokens(
            qualifyKnownTypes(function.returnType, headers, function.namespaceName),
            function.namespaceName)
        + " (*) (" + parameters + ")>(&" + function.cppName + ")";
}

std::string registerFunctionName(const std::string& module)
{
    std::string result = "register_";
    for (char ch : module) {
        if (std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_') {
            result += ch;
        } else {
            result += '_';
        }
    }
    result += "_reflection";
    return result;
}

std::string ownershipExpression(const std::string& value)
{
    if (value == "Engine")
        return "::arp::reflection::RegistrationOwnership::Engine";
    if (value == "Value")
        return "::arp::reflection::RegistrationOwnership::Value";
    return "::arp::reflection::RegistrationOwnership::Unspecified";
}

std::string lifecycleExpression(const std::string& value)
{
    if (value == "Explicit")
        return "::arp::reflection::RegistrationLifecycle::Explicit";
    if (value == "Value")
        return "::arp::reflection::RegistrationLifecycle::Value";
    return "::arp::reflection::RegistrationLifecycle::Unspecified";
}

bool writeJson(const ProgramOptions& options,
    const std::vector<HeaderModel>& headers,
    std::string& diagnostics)
{
    std::filesystem::create_directories(options.outJson.parent_path());
    std::ofstream out(options.outJson);
    if (!out.good()) {
        diagnostics = "failed to write JSON: " + options.outJson.generic_string();
        return false;
    }

    out << "{\n";
    out << "  \"module\": \"" << jsonEscape(options.module) << "\",\n";
    out << "  \"headers\": [";
    for (std::size_t index = 0; index < headers.size(); ++index) {
        if (index != 0) {
            out << ", ";
        }
        out << "\"" << jsonEscape(headers[index].path) << "\"";
    }
    out << "],\n";
    out << "  \"classes\": [\n";
    bool firstClass = true;
    for (const auto& header : headers) {
        for (const auto& cls : header.classes) {
            if (!firstClass) {
                out << ",\n";
            }
            firstClass = false;
            out << "    {\n";
            out << "      \"kind\": \"" << jsonEscape(cls.kind) << "\",\n";
            out << "      \"cppName\": \"" << jsonEscape(cls.cppName) << "\",\n";
            out << "      \"reflectName\": \"" << jsonEscape(cls.reflectName) << "\",\n";
            out << "      \"defaultConstruct\": " << (cls.defaultConstruct ? "true" : "false")
                << ",\n";
            out << "      \"constructors\": [";
            for (std::size_t constructorIndex = 0; constructorIndex < cls.constructors.size();
                ++constructorIndex) {
                if (constructorIndex != 0) {
                    out << ", ";
                }
                out << "{ \"parameters\": [";
                const auto& parameters = cls.constructors[constructorIndex];
                for (std::size_t parameterIndex = 0; parameterIndex < parameters.size();
                    ++parameterIndex) {
                    if (parameterIndex != 0) {
                        out << ", ";
                    }
                    const auto& parameter = parameters[parameterIndex];
                    out << "{ \"name\": \"" << jsonEscape(parameter.name) << "\", \"cppType\": \""
                        << jsonEscape(parameter.cppType) << "\", \"referenceKind\": \""
                        << jsonEscape(parameter.referenceKind)
                        << "\", \"nullable\": " << (parameter.nullable ? "true" : "false") << " }";
                }
                out << "] }";
            }
            out << "],\n";
            out << "      \"ownership\": \"" << jsonEscape(cls.ownership) << "\",\n";
            out << "      \"lifecycle\": \"" << jsonEscape(cls.lifecycle) << "\",\n";
            out << "      \"moduleExport\": \"" << jsonEscape(cls.moduleExportName) << "\",\n";
            out << "      \"bases\": [";
            for (std::size_t baseIndex = 0; baseIndex < cls.bases.size(); ++baseIndex) {
                if (baseIndex != 0) {
                    out << ", ";
                }
                out << "\"" << jsonEscape(cls.bases[baseIndex]) << "\"";
            }
            out << "],\n";
            out << "      \"properties\": [";
            for (std::size_t propertyIndex = 0; propertyIndex < cls.properties.size();
                ++propertyIndex) {
                const auto& property = cls.properties[propertyIndex];
                if (propertyIndex != 0) {
                    out << ", ";
                }
                out << "{ \"name\": \"" << jsonEscape(property.name) << "\", \"cppType\": \""
                    << jsonEscape(property.cppType) << "\", \"access\": \""
                    << jsonEscape(property.access) << "\"";
                if (!property.doc.empty())
                    out << ", \"doc\": \"" << jsonEscape(property.doc) << "\"";
                out << ", \"source\": \"" << (property.accessor ? "accessor" : "field") << "\"";
                if (property.accessor) {
                    out << ", \"getter\": \""
                        << jsonEscape(cls.methods[property.getterIndex].reflectName)
                        << "\", \"setter\": \""
                        << jsonEscape(cls.methods[property.setterIndex].reflectName) << "\"";
                }
                out << ", \"flags\": [";
                for (std::size_t flagIndex = 0; flagIndex < property.flags.size(); ++flagIndex) {
                    if (flagIndex != 0) {
                        out << ", ";
                    }
                    out << "\"" << jsonEscape(property.flags[flagIndex]) << "\"";
                }
                out << "] }";
            }
            out << "],\n";
            out << "      \"methods\": [";
            bool firstMethod = true;
            for (std::size_t methodIndex = 0; methodIndex < cls.methods.size(); ++methodIndex) {
                const auto& method = cls.methods[methodIndex];
                if (method.accessorGetter) {
                    continue;
                }
                if (!firstMethod) {
                    out << ", ";
                }
                firstMethod = false;
                out << "{ \"name\": \"" << jsonEscape(method.name) << "\", \"reflectName\": \""
                    << jsonEscape(method.reflectName) << "\", \"returnType\": \""
                    << jsonEscape(method.returnType) << "\", \"args\": \""
                    << jsonEscape(method.args)
                    << "\", \"static\": " << (method.staticMethod ? "true" : "false")
                    << ", \"maySuspend\": " << (method.maySuspend ? "true" : "false")
                    << ", \"const\": " << (method.constMethod ? "true" : "false")
                    << ", \"volatile\": " << (method.volatileMethod ? "true" : "false")
                    << ", \"noexcept\": " << (method.noexceptMethod ? "true" : "false")
                    << ", \"refQualifier\": \"" << jsonEscape(method.refQualifier) << "\""
                    << ", \"nullableReturn\": " << (method.nullableReturn ? "true" : "false")
                    << ", \"returnReferenceKind\": \"" << jsonEscape(method.returnReferenceKind)
                    << "\", \"parameters\": [";
                for (std::size_t parameterIndex = 0; parameterIndex < method.parameters.size();
                    ++parameterIndex) {
                    if (parameterIndex != 0) {
                        out << ", ";
                    }
                    const auto& parameter = method.parameters[parameterIndex];
                    out << "{ \"name\": \"" << jsonEscape(parameter.name) << "\", \"cppType\": \""
                        << jsonEscape(parameter.cppType) << "\", \"referenceKind\": \""
                        << jsonEscape(parameter.referenceKind)
                        << "\", \"nullable\": " << (parameter.nullable ? "true" : "false") << " }";
                }
                out << "], \"flags\": [";
                for (std::size_t flagIndex = 0; flagIndex < method.flags.size(); ++flagIndex) {
                    if (flagIndex != 0) {
                        out << ", ";
                    }
                    out << "\"" << jsonEscape(method.flags[flagIndex]) << "\"";
                }
                out << "] }";
            }
            out << "]\n";
            out << "    }";
        }
    }
    out << "\n  ],\n";
    out << "  \"functions\": [\n";
    bool firstFunction = true;
    for (const auto& header : headers) {
        for (const auto& function : header.functions) {
            if (!firstFunction) {
                out << ",\n";
            }
            firstFunction = false;
            out << "    {\n";
            out << "      \"cppName\": \"" << jsonEscape(function.cppName) << "\",\n";
            out << "      \"reflectName\": \"" << jsonEscape(function.reflectName) << "\",\n";
            out << "      \"returnType\": \"" << jsonEscape(function.returnType) << "\",\n";
            out << "      \"args\": \"" << jsonEscape(function.args) << "\",\n";
            out << "      \"defaultArguments\": \"" << jsonEscape(function.defaultArguments)
                << "\",\n";
            out << "      \"maySuspend\": " << (function.maySuspend ? "true" : "false") << ",\n";
            out << "      \"nullableReturn\": " << (function.nullableReturn ? "true" : "false")
                << ",\n";
            out << "      \"returnReferenceKind\": \"" << jsonEscape(function.returnReferenceKind)
                << "\",\n";
            out << "      \"parameters\": [";
            for (std::size_t parameterIndex = 0; parameterIndex < function.parameters.size();
                ++parameterIndex) {
                if (parameterIndex != 0) {
                    out << ", ";
                }
                const auto& parameter = function.parameters[parameterIndex];
                out << "{ \"name\": \"" << jsonEscape(parameter.name) << "\", \"cppType\": \""
                    << jsonEscape(parameter.cppType) << "\", \"referenceKind\": \""
                    << jsonEscape(parameter.referenceKind)
                    << "\", \"nullable\": " << (parameter.nullable ? "true" : "false") << " }";
            }
            out << "],\n";
            out << "      \"flags\": [";
            for (std::size_t flagIndex = 0; flagIndex < function.flags.size(); ++flagIndex) {
                if (flagIndex != 0) {
                    out << ", ";
                }
                out << "\"" << jsonEscape(function.flags[flagIndex]) << "\"";
            }
            out << "]\n";
            out << "    }";
        }
    }
    out << "\n  ],\n";
    out << "  \"enums\": [\n";
    bool firstEnum = true;
    for (const auto& header : headers) {
        for (const auto& enumeration : header.enums) {
            if (!firstEnum) {
                out << ",\n";
            }
            firstEnum = false;
            out << "    {\n";
            out << "      \"cppName\": \"" << jsonEscape(enumeration.cppName) << "\",\n";
            out << "      \"reflectName\": \"" << jsonEscape(enumeration.reflectName) << "\",\n";
            out << "      \"values\": [";
            for (std::size_t valueIndex = 0; valueIndex < enumeration.values.size(); ++valueIndex) {
                if (valueIndex != 0) {
                    out << ", ";
                }
                out << "\"" << jsonEscape(enumeration.values[valueIndex]) << "\"";
            }
            out << "]\n";
            out << "    }";
        }
    }
    out << "\n  ]\n";
    out << "}\n";
    return true;
}

bool writeCpp(const ProgramOptions& options,
    const std::vector<HeaderModel>& headers,
    std::string& diagnostics)
{
    std::filesystem::create_directories(options.outCpp.parent_path());
    std::ofstream out(options.outCpp);
    if (!out.good()) {
        diagnostics = "failed to write C++: " + options.outCpp.generic_string();
        return false;
    }

    out << "// Generated by arp_reflect_codegen. Do not edit.\n";
    for (const auto& header : headers) {
        out << "#include \"" << jsonEscape(header.path) << "\"\n";
    }
    out << "#include \"reflection/reflection.h\"\n\n";
    out << "#include \"core/sha256.h\"\n\n";
    out << "#include \"scriptvm/runtime/module_registration.h\"\n\n";
    out << "#include <utility>\n\n";
    out << "namespace arp::reflection::generated {\n\n";
    out << "inline ::arp::core::pmr::String generatedReflectionSignatureHash(const char* seed)\n";
    out << "{\n";
    out << "    const auto digest = ::arp::core::Sha256::digest(::arp::core::StringView(seed));\n";
    out << "    static constexpr char digits[] = \"0123456789abcdef\";\n";
    out << "    ::arp::core::pmr::String result(\"sha256:\", &::arp::core::defaultCpuMemoryResource());\n";
    out << "    result.reserve(71u);\n";
    out << "    for (const auto byte : digest) { result.push_back(digits[(byte >> 4u) & 0x0fu]); result.push_back(digits[byte & 0x0fu]); }\n";
    out << "    return result;\n";
    out << "}\n\n";
    const auto functionName = registerFunctionName(options.module);
    std::string moduleObjectExport;
    for (const auto& header : headers) {
        for (const auto& cls : header.classes) {
            if (!cls.moduleExportName.empty()) {
                moduleObjectExport = cls.moduleExportName;
            }
        }
    }
    out << "bool " << functionName << "(::arp::scriptvm::ModuleRegistration* module";
    if (!moduleObjectExport.empty()) {
        out << ", const ::arp::reflection::object* moduleObject";
    }
    out << ")\n";
    out << "{\n";
    out << "    using namespace ::arp::reflection;\n\n";

    for (const auto& header : headers) {
        for (const auto& cls : header.classes) {
            out << "    {\n";
            out << "        auto cls = registration::class_<" << cls.cppName << ">(\""
                << jsonEscape(cls.reflectName) << "\");\n";
            out << "        if (cls.info() == nullptr) { return false; }\n";
            if (cls.defaultConstruct) {
                out << "        cls.construct<>();\n";
            }
            for (const auto& constructor : cls.constructors) {
                out << "        cls.construct<";
                for (std::size_t parameterIndex = 0; parameterIndex < constructor.size();
                    ++parameterIndex) {
                    if (parameterIndex != 0) {
                        out << ", ";
                    }
                    out << constructor[parameterIndex].cppType;
                }
                out << ">();\n";
            }
            for (const auto& base : cls.bases) {
                out << "        cls.inherit<" << base << ">();\n";
            }
            for (const auto& property : cls.properties) {
                if (property.access != "public") {
                    diagnostics =
                        "non-public reflected property is not supported in this generator slice: "
                        + cls.cppName + "::" + property.name;
                    return false;
                }
                if (property.accessor) {
                    const auto& getter = cls.methods[property.getterIndex];
                    const auto& setter = cls.methods[property.setterIndex];
                    out << "        cls.accessor_property(\"" << jsonEscape(property.name) << "\", "
                        << methodPointerExpression(cls, getter, headers) << ", "
                        << methodPointerExpression(cls, setter, headers) << ", \"\", 0);\n";
                } else {
                    out << "        cls.property(\"" << jsonEscape(property.name) << "\", &"
                        << cls.cppName << "::" << property.name << ", \""
                        << jsonEscape(property.doc) << "\", "
                        << propertyHintExpression(property.flags) << ");\n";
                }
            }
            for (const auto& method : cls.methods) {
                if (method.accessorGetter) {
                    continue;
                }
                if (method.access != "public") {
                    diagnostics = "non-public AFUNC is not supported in this generator slice: "
                        + cls.cppName + "::" + method.name;
                    return false;
                }
                out << "        cls.method(\"" << jsonEscape(method.reflectName) << "\", "
                    << methodPointerExpression(cls, method, headers) << ");\n";
            }
            for (const auto& method : cls.methods) {
                if (method.accessorGetter) {
                    continue;
                }
                const std::string target =
                    options.module + "." + cls.reflectName + "." + method.reflectName;
                const std::string binding = "arp-reflect:" + target + "/v1";
                const std::string seed = "reflection-method|" + target
                    + "|return:" + method.returnType + "|args:" + method.args
                    + "|arity:" + std::to_string(method.parameters.size())
                    + "|static:" + (method.staticMethod ? "1" : "0") + "|suspend:"
                    + (method.maySuspend ? "1" : "0") + "|const:" + (method.constMethod ? "1" : "0")
                    + "|volatile:" + (method.volatileMethod ? "1" : "0") + "|noexcept:"
                    + (method.noexceptMethod ? "1" : "0") + "|ref:" + method.refQualifier
                    + "|nullable-return:" + (method.nullableReturn ? "1" : "0")
                    + "|return-ref:" + method.returnReferenceKind;
                std::string signatureSeed = seed;
                for (const auto& parameter : method.parameters) {
                    signatureSeed += "|param:" + parameter.cppType + ":" + parameter.referenceKind
                        + ":" + (parameter.nullable ? "1" : "0");
                }
                out << "        { auto reflectedMethod = cls.info()->get_field(\""
                    << jsonEscape(method.reflectName)
                    << "\").cast<::arp::reflection::function&>();\n";
                out << "          ::arp::scriptvm::ReflectionCallableContract contract;\n";
                out << "          contract.bindingId = \"" << jsonEscape(binding) << "\";\n";
                out << "          contract.targetId = \"" << jsonEscape(target) << "\";\n";
                out << "          contract.signatureHash = generatedReflectionSignatureHash(\""
                    << jsonEscape(signatureSeed) << "\");\n";
                out << "          contract.fixedArity = " << method.parameters.size() << ";\n";
                out << "          contract.maySuspend = " << (method.maySuspend ? "true" : "false")
                    << ";\n";
                out << "          contract.lifetimeVersion = 1u;\n";
                out << "          contract.functionIdentity = reflectedMethod.identity();\n";
                out << "          if (module != nullptr && !module->registerReflectionCallable(\""
                    << jsonEscape(target) << "\", std::move(contract))) { return false; }\n";
                out << "        }\n";
            }
            const auto classContract = classDescriptorContract(cls);
            out << "        if (!::arp::reflection::declareRegistrationClassContract(\"class:"
                << jsonEscape(cls.reflectName) << "\", \"" << jsonEscape(classContract)
                << "\", cls.info(), " << ownershipExpression(cls.ownership) << ", "
                << lifecycleExpression(cls.lifecycle) << ", \"" << jsonEscape(cls.moduleExportName)
                << "\")) { return false; }\n";
            out << "        if (module != nullptr && !module->bindType(\""
                << jsonEscape(cls.reflectName) << "\", cls.info())) {\n";
            out << "            return false;\n";
            out << "        }\n";
            out << "    }\n\n";
        }
    }

    if (!moduleObjectExport.empty()) {
        out << "    if (module != nullptr && moduleObject != nullptr && !module->bindObject(\""
            << jsonEscape(moduleObjectExport) << "\", *moduleObject)) {\n";
        out << "        return false;\n";
        out << "    }\n\n";
    }

    for (const auto& header : headers) {
        for (const auto& function : header.functions) {
            const std::string target = options.module + "." + function.reflectName;
            const std::string binding = "arp-reflect:" + target + "/v1";
            const std::string seed = "reflection-function|" + target
                + "|return:" + function.returnType + "|args:" + function.args
                + "|arity:" + std::to_string(function.parameters.size())
                + "|suspend:" + (function.maySuspend ? "1" : "0")
                + "|nullable-return:" + (function.nullableReturn ? "1" : "0") + "|return-ref:"
                + function.returnReferenceKind + "|defaults:" + function.defaultArguments;
            std::string signatureSeed = seed;
            for (const auto& parameter : function.parameters) {
                signatureSeed += "|param:" + parameter.cppType + ":" + parameter.referenceKind + ":"
                    + (parameter.nullable ? "1" : "0");
            }
            out << "    {\n";
            out << "        auto reflectedFunction = ::arp::reflection::function(\""
                << jsonEscape(function.reflectName) << "\", "
                << functionPointerExpression(function, headers) << ");\n";
            if (!function.defaultArguments.empty()) {
                out << "        reflectedFunction.set_default_arguments("
                    << function.defaultArguments << ");\n";
            }
            out << "        ::arp::scriptvm::ReflectionCallableContract contract;\n";
            out << "        contract.bindingId = \"" << jsonEscape(binding) << "\";\n";
            out << "        contract.targetId = \"" << jsonEscape(target) << "\";\n";
            out << "        contract.signatureHash = generatedReflectionSignatureHash(\""
                << jsonEscape(signatureSeed) << "\");\n";
            out << "        contract.fixedArity = " << function.parameters.size() << ";\n";
            out << "        contract.maySuspend = " << (function.maySuspend ? "true" : "false")
                << ";\n";
            out << "        contract.lifetimeVersion = 1u;\n";
            out << "        contract.functionIdentity = reflectedFunction.identity();\n";
            out << "        if (module != nullptr && !module->exportFunction(\""
                << jsonEscape(function.reflectName)
                << "\", std::move(reflectedFunction), std::move(contract))) {\n";
            out << "            return false;\n";
            out << "        }\n";
            const auto functionContract = functionDescriptorContract(function);
            out << "        if (!::arp::reflection::declareRegistrationContractRecord(\"function:"
                << jsonEscape(function.reflectName) << "\", \"" << jsonEscape(functionContract)
                << "\")) { return false; }\n";
            out << "    }\n\n";
        }
    }

    std::size_t enumIndex = 0;
    for (const auto& header : headers) {
        for (const auto& enumeration : header.enums) {
            const auto enumVariable = "enum_cls_" + std::to_string(enumIndex++);
            out << "    auto " << enumVariable << " = registration::class_<" << enumeration.cppName
                << ">(\"" << jsonEscape(enumeration.reflectName) << "\");\n";
            out << "    if (" << enumVariable << ".info() == nullptr) { return false; }\n";
            out << "    " << enumVariable << ".constant<" << enumeration.cppName << ">(\n";
            out << "            \"" << jsonEscape(enumeration.reflectName) << "\",\n";
            out << "            {\n";
            for (std::size_t valueIndex = 0; valueIndex < enumeration.values.size(); ++valueIndex) {
                const auto& value = enumeration.values[valueIndex];
                out << "                enum_value(\"" << jsonEscape(value) << "\", "
                    << enumeration.cppName << "::" << value << ")";
                out << (valueIndex + 1 == enumeration.values.size() ? "\n" : ",\n");
            }
            out << "            });\n";
            out << "    if (module != nullptr && !module->bindEnum(\""
                << jsonEscape(enumeration.reflectName) << "\", " << enumVariable
                << ".info()->get_field(\"" << jsonEscape(enumeration.reflectName)
                << "\").cast<enumeration&>())) {\n";
            out << "        return false;\n";
            out << "    }\n\n";
            const auto enumerationContract = enumDescriptorContract(enumeration);
            out << "    if (!::arp::reflection::declareRegistrationClassContract(\"enum:"
                << jsonEscape(enumeration.reflectName) << "\", \""
                << jsonEscape(enumerationContract) << "\", " << enumVariable
                << ".info(), ::arp::reflection::RegistrationOwnership::Unspecified, "
                << "::arp::reflection::RegistrationLifecycle::Unspecified, \"\")) { return false; }\n\n";
        }
    }

    out << "    return true;\n";
    out << "}\n\n";
    if (!moduleObjectExport.empty()) {
        out << "bool " << functionName << "(::arp::scriptvm::ModuleRegistration* module)\n";
        out << "{\n";
        out << "    return " << functionName << "(module, nullptr);\n";
        out << "}\n\n";
    }
    out << "void " << functionName << "()\n";
    out << "{\n";
    out << "    (void)" << functionName << "(nullptr);\n";
    out << "}\n\n";
    out << "} // namespace arp::reflection::generated\n";
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc == 2 && std::string(argv[1]) == "--version") {
        std::cout << "arp_reflect_codegen 3\n";
        return 0;
    }
    ProgramOptions options;
    std::string diagnostics;
    if (!parseOptions(argc, argv, options, diagnostics)) {
        std::cerr << "arp_reflect_codegen: " << diagnostics << "\n";
        return 1;
    }

    ParseResult parseResult = parseHeaders(options.headers);
    if (!parseResult.success) {
        std::cerr << "arp_reflect_codegen: " << parseResult.diagnostics << "\n";
        return 1;
    }

    if (!writeJson(options, parseResult.headers, diagnostics)) {
        std::cerr << "arp_reflect_codegen: " << diagnostics << "\n";
        return 1;
    }
    if (!writeCpp(options, parseResult.headers, diagnostics)) {
        std::cerr << "arp_reflect_codegen: " << diagnostics << "\n";
        return 1;
    }
    return 0;
}
