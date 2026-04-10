# Comment Format Guidelines

This document describes the agreed-upon comment style for the codebase.

## File Header Format

Use the following structure for header comments in `.h` and `.cpp` files.

```cpp
#ifndef HEADERDEF
#define HEADERDEF

/**
 * @file Filename
 * @brief Brief Description
 *
 * Detailed Description
 *
 * @author Ryan Massie (rmassie)
 * @date Creation Date
 */
```

For Doxygen compatibility, include `@file` on file headers and use `@brief` for one-line summaries. Use `@details` or a second paragraph for longer descriptions if needed.

## Doxygen Comment Style

Prefer Doxygen-style block comments for public declarations and documented behavior.

```cpp
/**
 * @brief Initialize the pressure sensor module.
 * @param baud_rate UART baud rate for the sensor.
 * @return true on success, false on error.
 * @note This function may block while the UART is configured.
 */
```

Common Doxygen tags:

- `@brief` - short summary
- `@param` - parameter description
- `@return` - return value explanation
- `@note` - important detail
- `@warning` - caution about usage
- `@todo` - future work
- `@see` - related symbol or module

## Section Headers / Comments

Use section comments to clearly separate logical blocks of code.

- Leave one blank line before a section break.
- Keep section headers short and focused on the block's responsibility.
- Use them to group related functions, data, or implementation details, not to label every line.

Option 1:

```cpp
/**
 * Section Name
 */
```

Option 2:

```cpp
/**
 *
 *
 * Section Description
 *
 *
 */
```

Option 3: use a visible separator line for larger logical sections.

```cpp
/**
 * --------------------
 * Section Name
 * --------------------
 */
```

## Function Description

Document functions with a brief description, parameter details, and return information.

```cpp
/**
 * @brief Constructs a HardUart object.
 * @param variable1: Description1
 * @param variable2: Description2
 * @return what return is
 */
```

## Class Member Variable Definitions

Use a short `@brief` comment for member variables.

```cpp
/**
 * @brief Destructor
 */
```
