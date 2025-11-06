# Advanced Topics in LukeLang

This guide covers advanced features and concepts in LukeLang, including interoperability with JavaScript, error handling, and code organization.

## 1. Interoperability with JavaScript

Since LukeLang transpiles to JavaScript, you can seamlessly interoperate with existing JavaScript code and libraries.

### Calling JavaScript Functions

You can call any JavaScript function directly from your LukeLang code.

```luke
// Assuming you have a JS file with:
// function greet(name) { console.log(`Hello, ${name}!`); }

// In your LukeLang code:
greet("Luke")
```

### Using JavaScript Libraries

To use an external JavaScript library, simply include it in your project and call its functions as you would in regular JavaScript.

## 2. Error Handling

LukeLang does not yet have a dedicated error handling mechanism (e.g., `try-catch` blocks). However, since it transpiles to JavaScript, you can rely on JavaScript's native error handling when needed.

Future versions of LukeLang will introduce more robust error handling features.

## 3. Modules and Code Organization

Currently, LukeLang does not have a built-in module system. For larger projects, it is recommended to organize your code into multiple `.luke` files and transpile them separately.

We are actively working on a module system that will allow you to import and export code between files, making it easier to manage large codebases.

## What's Next?

LukeLang is a growing language, and we are committed to adding more features and improvements. If you are interested in contributing, please see our **[Contributor Guide](./contributor_guide.md)**.