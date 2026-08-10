# LukeLang Language Reference

This document provides a comprehensive reference for the LukeLang programming language, covering its syntax, data types, control flow, functions, and object-oriented programming features.

## 1. Variables and Data Types

In LukeLang, variables are declared using the `LET` or `VAR` keywords, followed by the variable name and an optional initial value.

### Declaration

```luke
// Verbose declaration
LET myVariable BE 10
VAR anotherVariable BE "Hello"

// Shorthand declaration
myVariable = 10
anotherVariable = "Hello"
```

### Data Types

LukeLang supports the following data types:
-   **Numbers**: `10`, `3.14`
-   **Strings**: `"Hello"`, `'World'`
-   **Booleans**: `TRUE`, `FALSE`
-   **Lists (Arrays)**: `[1, 2, 3]`, `["apple", "banana"]`
-   **Objects**: Handled through classes.

## 2. Operators

LukeLang supports standard arithmetic, comparison, and logical operators.

-   **Arithmetic**: `+`, `-`, `*`, `/`, `%`
-   **Comparison**: `IS`, `IS NOT`, `>`, `<`, `>=`, `<=`
-   **Logical**: `AND`, `OR`, `NOT`

## 3. Control Flow

### Conditional Statements

```luke
IF condition THEN
  // Code to execute if condition is true
ELSE IF anotherCondition THEN
  // Code to execute if anotherCondition is true
ELSE
  // Code to execute otherwise
END IF
```

### Loops

**While Loop**

```luke
WHILE condition
  // Code to repeat as long as condition is true
END WHILE
```

**For Loop**

```luke
FOR i FROM 0 TO 10
  // Code to repeat 10 times
END FOR
```

## 4. Functions

Functions are defined using the `ROUTINE` or `FUNC` keyword.

```luke
// Verbose function
ROUTINE add(a, b)
  RETURN a + b
END ROUTINE

// Shorthand function
func multiply(a, b) {
  RETURN a * b
}
```

## 5. Classes and OOP

LukeLang provides robust support for object-oriented programming, including classes, inheritance, and methods.

### Class Definition

```luke
BLUEPRINT Ticket {
  init(ticketPrice, totalSeats) {
    price = ticketPrice
    seats = totalSeats
  }

  book(num) {
    seats = seats - num
    SPEAK "Booked " AND num AND " seats"
  }
}
```

### Inheritance

```luke
BLUEPRINT ConcertTicket FOLLOWS Ticket {
  artist

  init(artistName, price, seats) {
    PARENT TAKES price, seats
    artist = artistName
  }

  showInfo() {
    SPEAK "Artist: " AND artist
  }
}
```

For more details on advanced features, refer to the **[Advanced Topics](./advanced_topics.md)** guide.
## Multiple Inheritance (Mixin-Based)

LukeLang supports declaring multiple parents via `FOLLOWS <BaseA> AND <BaseB>` on the **Play VM** path. Build blueprints currently use a single `FOLLOWS` parent — prefer that for `luke BUILD` programs.

### Explicit Ancestor Calls

When a class has multiple parents (Play VM), `CALL PARENT <method>` refers to the primary base. To invoke a specific ancestor’s implementation, use:

- `CALL PARENT <method> OF <Ancestor> WITH <args>`

### Example

```
BLUEPRINT Bird IMPLEMENTS Flyable DO
  METHOD fly WITH height DO
    SPEAK "Bird flying at " AND height AND " meters high"
  END METHOD
END CLASS

BLUEPRINT Fish IMPLEMENTS Swimmable DO
  METHOD swim WITH speed DO
    SPEAK "Fish swimming smoothly at " AND speed AND " m/s"
  END METHOD
END CLASS

BLUEPRINT Duck FOLLOWS Bird AND Fish IMPLEMENTS Flyable AND Swimmable DO
  WHEN BORN DO
  END BORN

  METHOD fly WITH height DO
    CALL PARENT fly WITH height
    SPEAK "Duck flapping wings cutely at " AND height AND " meters"
  END METHOD

  METHOD swim WITH speed DO
    CALL PARENT swim OF Fish WITH speed
    SPEAK "Duck paddling in circles at " AND speed AND " m/s"
  END METHOD
END CLASS
```

Output:

```
Bird flying at 10 meters high
Duck flapping wings cutely at 10 meters
Fish swimming smoothly at 2 m/s
Duck paddling in circles at 2 m/s
```

### Composition Alternative

If you prefer explicit composition rather than mixins, you can model the second parent as a field and delegate:

```
HAS fishPart SET TO NEW Fish
METHOD swim WITH speed DO
  CALL fishPart swim WITH speed
  SPEAK "Duck paddling in circles at " AND speed AND " m/s"
END METHOD
```