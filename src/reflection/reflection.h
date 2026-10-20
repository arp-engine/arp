#pragma once

// reflection.h -- umbrella header for the arp::reflection module.
//
// This file replaces the former monolithic reflection.h with a multi-file
// structure that mirrors the original ae::hybrid layout. Existing code that
// #include "reflection/reflection.h" continues to work without changes.
//
// File structure:
//   def.h          -- value_t, type_trait_mask, derived_info, macros, traits
//   type.h         -- typeinfo
//   variant.h      -- variant (SBO + vtable value member)
//   object.h       -- object (shared variant wrapper)
//   function.h    -- function + parameter
//   property.h    -- property (with arp indirect extensions)
//   class_info.h  -- class_info (mirrors original class.h)
//   instance.h    -- instance
//   enum.h        -- enumeration + enum_pair/enum_value
//   db.h          -- db (class/type registry)
//   registry.h    -- registration::class_<C>
//   builtin.h     -- registerBuiltinReflectionTypes()
//
//   internal/      -- template implementations (included by their public headers)

#include "reflection/builtin.h"
#include "reflection/class_info.h"
#include "reflection/db.h"
#include "reflection/def.h"
#include "reflection/enum.h"
#include "reflection/function.h"
#include "reflection/instance.h"
#include "reflection/object.h"
#include "reflection/property.h"
#include "reflection/registration_transaction.h"
#include "reflection/registry.h"
#include "reflection/type.h"
#include "reflection/variant.h"
