#pragma once

// Compatibility include for Interpreter consumers. The canonical suspended
// continuation contract belongs to the ScriptVM runtime so DirectNative does
// not depend on the Interpreter include hierarchy.
#include "scriptvm/runtime/suspended_frame.h"
