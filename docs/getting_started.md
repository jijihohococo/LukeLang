# Getting Started with LukeLang

This guide will walk you through setting up your development environment, writing your first LukeLang program, and running it.

## Installation

LukeLang is built on Node.js and can be run from the command line. To get started, you will need to have Node.js and npm (Node Package Manager) installed on your system.

1.  **Install Node.js and npm**:
    If you don't have Node.js installed, download it from the [official Node.js website](https://nodejs.org/). npm is included with the Node.js installation.

2.  **Clone the LukeLang Repository**:
    Open your terminal and run the following command to clone the LukeLang repository from GitHub:
    ```bash
    git clone https://github.com/your-username/lukelang.git
    cd lukelang
    ```

3.  **Install Dependencies**:
    Install the required npm packages by running:
    ```bash
    npm install
    ```

## Your First Program

Let's write a classic "Hello, World!" program in LukeLang. Create a new file named `hello.luke` and add the following code:

```luke
// This is your first LukeLang program
SPEAK "Hello, World!"
```

This simple program uses the `SPEAK` keyword to print the message "Hello, World!" to the console.

## Running LukeLang Code

To run your LukeLang program, you will use the `main.js` transpiler, which converts your `.luke` file into a `.js` file that can be executed by Node.js.

1.  **Transpile the Code**:
    In your terminal, run the following command:
    ```bash
    node main.js hello.luke
    ```
    This will generate a `hello.js` file in the same directory.

2.  **Run the JavaScript File**:
    Now, execute the generated JavaScript file using Node.js:
    ```bash
    node hello.js
    ```

    You should see the following output in your console:
    ```
    Hello, World!
    ```

Congratulations! You have successfully written and executed your first LukeLang program. You are now ready to explore more advanced features in the **[Language Reference](./language_reference.md)**.