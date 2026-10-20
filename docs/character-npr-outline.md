# Character NPR And Outline Production Pass

Status: Contract Complete.

`src/renderer/character_npr.h` defines the character material contract:

- skin, face, eye, hair, cloth, and accessory domains
- rim strength
- ramp texture reference
- outline mode and width
- mobile fallback from inverted-hull outline to cheaper depth/normal outline
- explicit shader variant key generation

Future shader work must keep variant generation explicit and validate material
inputs against shader reflection.
