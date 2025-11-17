# f25shell - Custom Shell Implementation

A comprehensive custom shell implementation in C that supports advanced shell operations including file operations, I/O redirection, piping, conditional execution, background processes, and more.

## Overview

`f25shell` is a feature-rich shell interpreter that extends basic command execution with custom operators and built-in commands for file manipulation, process management, and advanced command chaining.

## Features

### Built-in Commands
- **killterm** - Kill the current shell instance
- **killallterms** - Kill all f25shell instances
- **numbg** - Count the number of background processes in the current session
- **killbp** - Kill all processes except the current shell and bash
- **exit** - Exit the shell

### File Operations
- **Word Count (`#`)** - Count words in a text file
- **File Concatenation (`+`)** - Concatenate multiple files and display output
- **File Append (`++`)** - Mutually append two files (each file gets the other's content appended)

### I/O Redirection
- **Output Redirection (`>`)** - Redirect command output to a file (overwrites)
- **Append Redirection (`>>`)** - Append command output to a file
- **Input Redirection (`<`)** - Read command input from a file

### Command Chaining
- **Pipes (`|`)** - Connect commands where output of one becomes input of the next
- **Reverse Pipes (`~`)** - Execute commands in reverse order with piping
- **Sequential Execution (`;`)** - Execute commands one after another
- **Conditional Execution (`&&`, `||`)** - Execute commands based on previous command's success/failure

### Process Management
- **Background Processes (`&`)** - Run commands in the background without blocking

## Compilation

Compile the shell using GCC:

```bash
gcc -o f25shell f25shell.c
```

## Usage

Run the compiled shell:

```bash
./f25shell
```

You'll see the prompt `f25shell$:` where you can enter commands.

## Testing Commands

### 1. File Operations

#### Word Count
```bash
# words.txt
```
Counts the number of words in `words.txt` and displays the total.

#### File Concatenation
```bash
file1.txt + file2.txt
```
Concatenates and displays the contents of `file1.txt` followed by `file2.txt`.

**Expected Output:**
```
hello world from file one
content from file two
```

### 2. I/O Redirection

#### Output Redirection
```bash
echo demo test > demo.txt
```
Creates or overwrites `demo.txt` with the output of the `echo` command.

#### Input Redirection
```bash
cat < demo.txt
```
Reads input from `demo.txt` and passes it to the `cat` command, displaying the file contents.

### 3. Pipes

#### Basic Pipe
```bash
ls | grep txt
```
Lists files and filters to show only files containing "txt" in their names.

#### Word Count Pipe
```bash
cat words.txt | wc -w
```
Pipes the contents of `words.txt` to the `wc -w` command to count words.

### 4. Reverse Pipe

#### Reverse Pipe Example
```bash
wc -l ~ cat words.txt
```
Executes commands in reverse order: first runs `cat words.txt`, then pipes its output to `wc -l` to count lines. The `~` operator reverses the command order while maintaining the pipe flow.

### 5. Sequential Execution

#### Sequential Commands
```bash
echo step1 ; echo step2 ; echo step3
```
Executes multiple commands sequentially, one after another, regardless of whether previous commands succeed or fail.

**Expected Output:**
```
step1
step2
step3
```

### 6. Conditionals

#### AND Conditional
```bash
echo success && echo next
```
Executes the second command (`echo next`) only if the first command (`echo success`) succeeds.

**Expected Output:**
```
success
next
```

#### OR Conditional
```bash
invalidcmd || echo fallback
```
If the first command (`invalidcmd`) fails, the second command (`echo fallback`) is executed as a fallback.

**Expected Output:**
```
Exec failed for invalidcmd
fallback
```

### 7. Background Process

#### Run Command in Background
```bash
sleep 10 &
```
Runs the `sleep` command in the background, allowing the shell to continue accepting commands while the process runs.

**Expected Output:**
```
Background process started with PID: <process_id>
```

#### Count Background Processes
```bash
numbg
```
Displays the number of background processes currently running in the session.

**Expected Output:**
```
Number of background processes in current session: <count>
```

### 8. File Append

#### Mutually Append Files
```bash
file1.txt ++ file2.txt
```
Appends the contents of `file2.txt` to `file1.txt`, and also appends the original contents of `file1.txt` to `file2.txt`.

**Expected Output:**
```
Files appended successfully
```

#### Verify Append Operation
```bash
cat file1.txt
```
Displays the contents of `file1.txt` to verify that `file2.txt`'s content has been appended.

**Expected Output (after append):**
```
hello world from file one
content from file two
```

## Command Syntax and Limitations

### File Operations
- Word count: `# <filename>` (exactly 2 arguments required)
- Concatenation: `file1 + file2 [+ file3 ...]` (2-5 files, max 4 `+` operators)
- Append: `file1 ++ file2` (exactly 3 arguments required)

### I/O Redirection
- Command part: 1-5 arguments
- Redirection operators: `>`, `>>`, `<`
- File argument: 1 argument after the operator

### Pipes
- Maximum 4 pipe operators (`|`) per command
- Each command: 1-5 arguments
- Supports chaining of multiple commands

### Reverse Pipes
- Maximum 5 reverse pipe operators (`~`) per command
- Each command: 1-5 arguments
- Commands are executed in reverse order

### Sequential Execution
- Maximum 4 sequential commands (`;` separators)
- Each command: 1-5 arguments
- Commands execute regardless of previous success/failure

### Conditional Execution
- Maximum 5 conditional operators (`&&` or `||`) per command
- Each command: 1-5 arguments
- `&&`: Execute next command only if previous succeeds
- `||`: Execute next command only if previous fails

## File Structure

```
Assignment 3/
├── f25shell.c      # Main shell implementation source code
├── file1.txt       # Sample test file 1
├── file2.txt       # Sample test file 2
├── words.txt       # Sample test file for word counting
├── input.txt       # Additional test file
└── README.md       # This file
```

## Implementation Details

### Process Management
- The shell tracks background processes in an array
- Process group IDs are used to manage related processes
- `/proc` filesystem is read to collect process information

### Error Handling
- Comprehensive error messages for invalid syntax
- File operation error checking
- Process creation and execution error handling

### Memory Management
- Static arrays used for command storage (limits: MAX_ARGS=64, MAX_COMMANDS=25)
- Dynamic memory allocation avoided for simplicity

## Example Test Session

```bash
$ ./f25shell
f25shell$: # words.txt
Total word count is: 3

f25shell$: file1.txt + file2.txt
hello world from file one
content from file two

f25shell$: echo demo test > demo.txt

f25shell$: cat < demo.txt
demo test

f25shell$: ls | grep txt
file1.txt
file2.txt
words.txt

f25shell$: cat words.txt | wc -w
3

f25shell$: echo step1 ; echo step2 ; echo step3
step1
step2
step3

f25shell$: echo success && echo next
success
next

f25shell$: invalidcmd || echo fallback
Exec failed for invalidcmd
fallback

f25shell$: sleep 10 &
Background process started with PID: 12345

f25shell$: numbg
Number of background processes in current session: 1

f25shell$: file1.txt ++ file2.txt
Files appended successfully

f25shell$: cat file1.txt
hello world from file one
content from file two

f25shell$: exit
```

## Notes

- The shell runs in an interactive loop until `exit` is called or EOF is encountered
- Background processes are tracked and can be counted using `numbg`
- File operations use low-level file I/O (`open`, `read`, `write`) for efficiency
- All commands respect the argument count limitations (1-5 arguments per command segment)
- The shell handles tokenization internally, splitting input by spaces

## Troubleshooting

### Common Issues

1. **Command not found**: Ensure the command exists in your PATH or use full path
2. **Too many arguments**: Each command segment supports max 5 arguments
3. **File operation fails**: Check file permissions and ensure files exist
4. **Background process count mismatch**: Use `numbg` to check actual count vs. expected

### Error Messages

- `"Command argc must be between 1 and 5"` - Invalid number of arguments
- `"Maximum X operations allowed"` - Exceeded operator limit
- `"Exec failed for <command>"` - Command execution failed
- `"Failed to open file <filename>"` - File access error

## Development

This shell was implemented as part of an Advanced Systems Programming (ASP) assignment, demonstrating:
- Process creation and management (`fork`, `execvp`, `wait`)
- Inter-process communication (pipes)
- File I/O operations
- Signal handling
- Command parsing and tokenization

## License

Educational project - Assignment submission.

