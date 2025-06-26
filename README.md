# 10xclick.sys — Windows Mouse Click Multiplication Filter Driver

**Author:** [Amr Hamail]  
**Type:** Windows Kernel-Mode WDM Filter Driver  
**Purpose:** Multiply physical mouse left-click events at the kernel level by synthesizing fake IRPs.  
**Status:** In Development — Functional Prototype.  

---

## 🚀 Overview

`10xclick.sys` is a Windows kernel-mode filter driver designed to multiply mouse left-click events in real time. By injecting synthesized `MOUSE_INPUT_DATA` structures directly into the HID class stack, this driver intercepts raw mouse input and triggers additional "fake" left clicks — amplifying a single hardware click up to 5 times (10 total clicks: down-up pairs).  

This project was built entirely from scratch, using the Windows Driver Model (WDM) — not the more modern KMDF — for full manual control over IRP stack manipulation, completion handling, and precise timing behavior.

It was developed not as an academic assignment or company deliverable, but as a pure passion project by someone obsessed with how Windows handles low-level input. 

---

## 🔧 Technical Highlights

- **Filter Driver Architecture:** Attaches to the HID mouse stack to intercept and duplicate `IRP_MJ_READ` requests.
- **IRP Stack Cloning:** Utilizes `IoCopyCurrentIrpStackLocationToNext`, `IoCallDriver`, and custom completion routines to safely propagate and re-inject synthesized IRPs.
- **Input Injection:** Creates new `MOUSE_INPUT_DATA` structs directly in kernel space, modifying only the `ButtonFlags` and injecting sequences of click-down/up events.
- **Stack Safety:** Handles IRQL and stack constraints with care — including `IoSetCompletionRoutine` to ensure injected IRPs don’t leak or crash.
- **Multiplication Cap:** Multiplication factor limited to 5 injected click pairs to avoid overwhelming the I/O manager or risking input handler instability.
- **Built in Under 12 Hours:** This was a focused, intense build. Every hour went into getting one thing right: raw input manipulation at the deepest level Windows allows.

---

## ⚠️ Limitations

- **Not production-grade** — intended for testing, experimentation, and driver development practice only.
- **No symbolic link or IOCTL interface** — click multiplication factor is currently hardcoded and capped.
- **Multiplication cap** — 5 pairs max (10 clicks total) per event for safety and predictability.
- **No digital signature** — must disable driver signature enforcement to load on modern Windows versions.
- **Tested in VM environments only.**

---

## 📚 Why WDM?

Choosing WDM over KMDF was deliberate. WDM requires you to manually manage every step — from IRP propagation to memory cleanup to device stack handling. It's harder, riskier, and more error-prone — but it teaches you exactly how Windows internals behave when input flows from a mouse to a kernel stack.

For `10xclick.sys`, I wanted that control. I wanted to see what would happen when I intercepted a read request and created my own.

---

## 🛠️ Build Instructions

1. Clone the repository and open the `.sln` in Visual Studio.
2. Use the Windows Driver Kit (WDK) with kernel-mode support.
3. Set configuration to **Release** and **x64**.
4. Compile. The `.sys` file will appear in the output directory.

---

## 📦 Installation (Test Signing Only)

1. Enable test mode:  
   `bcdedit /set testsigning on`  
   Restart your machine.

2. Use tools like OSR Loader or `sc` to install and load `10xclick.sys`.

3. Ensure you're attaching it above the HID mouse class driver (usually `mouclass.sys`).

---

## 🙏 A Note From the Author

This project was born from obsession, not obligation. I didn't build `10xclick.sys` to solve a business problem — I built it because I *had to know* if I could. Every detail was hand-written: the IRP stack logic, the injected data structs, even the limits to keep the system stable.

---

## 🧠 Future Plans

- IOCTL interface to configure click multiplier in real time
- GUI companion controller for injection toggling
- URB-level injection for USB HID devices
- KMDF port with structured memory safety

---

## 📜 License

This project is released under the MIT License for learning and personal use only. Commercial use or distribution is prohibited.

