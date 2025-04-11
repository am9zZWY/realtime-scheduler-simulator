# Realtime Scheduling Simulator

This code was part of the lecture "Realtime Systems" from Prof. Dr. Michael Mächtel at the HTWG Konstanz (UAS).
It was written from scratch in C over the span of multiple weeks. Each week we had to extend the existing code basis. This is the final code basis.

## Requirements

- Make
- GCC

## Compiling it

```bash
make simulator
```

## Running the Simulator

To run the simulator, use the following command structure:

```bash
./simulator <input_file> [options]
```

Where:

- `<input_file>`: Path to the input file describing the tasks and simulation parameters (e.g., `input-8.txt`).
- `[options]`: Optional flags to configure the simulator's behavior.

**Available Options:**

- **Scheduler Type (Choose one):**
  - `-dm`: Use the Deadline Monotonic (DM) scheduling algorithm. Tasks with shorter deadlines have higher priority.
  - `-edf`: Use the Earliest Deadline First (EDF) scheduling algorithm. Tasks with the earliest absolute deadline have higher priority.
    *Note: You must specify either `-dm` or `-edf`.*
- **Deadline Handling (Choose one, optional):**
  - `-nokill`: If a task misses its deadline, it is removed from the current queue but future instances of the task can still run.
  - `-kill`: If a task misses its deadline, the task is aborted and will not run again.
    *Note: If neither is specified, the default behavior depends on the implementation (check code if needed, but `-nokill` seems implied by the example).*
- **Protocol (Optional):**
  - `-pip`: Enable the Priority Inheritance Protocol (PIP) to handle priority inversion when tasks share resources.

## Example

Run the simulator with the Deadline Monotonic scheduler using `input-8.txt`:

```bash
./simulator input-8.txt -dm
```

Output:

```text
Time    Job             Ready-Q         Blocked-Q               Actions
0       C0              C0
1       C0              C0                                      C0 locks X -> ok
2       B0              C0B0                                    [C0 preempted]
3       B0              C0B0
4       C0              C0              B0                      B0 locks X -> blocked
5       C0              C0              B0
6       A0              C0A0            B0                      [C0 preempted]
7       A0              C0A0            B0
8       C0              C0              B0A0                    A0 locks X -> blocked
9       C0              C0              B0A0                    C0 unlocks: X
10      A0              A0              B0                      A0 locks X -> ok
11      A0              A0              B0
12      A0              A0              B0                      A0 unlocks: X
13      B0              B0                                      B0 locks X -> ok
14      B0              B0
15      B0              B0
16      B0              B0
17      B0              B0                                      B0 unlocks: X
18      I
19      I
20      C1              C1
-----------------------------------------------
Reaction Times: 
Task: A max. Reaction Time: 7
Task: B max. Reaction Time: 16
Task: C max. Reaction Time: 10
```

## Acknowledgments and Code History

This README was partially generated with the assistance of Gemini 2.5 Pro, a large language model, based on the provided simulator.c code and example input/output files. The original code was written as part of a university course on Realtime Systems at HTWG Konstanz (UAS). While efforts have been made to ensure the accuracy of the information, the code's complexity and age may have introduced some inconsistencies or areas for improvement.
