# LukeLang Standard Library

This document outlines the standard library provided by LukeLang, including functions for input/output, file system operations, and other utilities.

## 1. Input/Output

LukeLang provides simple keywords for handling console input and output.

### `SPEAK`

The `SPEAK` keyword (and its synonyms `YELL`, `SHOUT`, `SAY`) prints a message to the console.

```luke
SPEAK "Hello, World!"

LET name BE "Luke"
SPEAK "Hello, " AND name
```

### `ASK USER`

The `ASK USER` keyword prompts the user for input and assigns it to a variable.

```luke
// Verbose
SET name TO ASK USER FOR "What is your name? "

// Shorthand
name = ASK USER FOR "What is your name? "
```

## 2. File System

File system operations are not yet implemented in the standard library but are planned for a future release.

## 3. Utility Functions

### `RANDOM`

Returns a random number between 0 and 1.

```luke
LET randomNumber BE RANDOM
SPEAK "Your lucky number is: " AND randomNumber
```

### `ROUND`

Rounds a number to the nearest integer.

```luke
LET num BE 3.14
SPEAK ROUND(num) // Outputs: 3
```

This standard library is continuously evolving. For the latest updates, please refer to the official LukeLang repository.