#!/usr/bin/env python3
"""Calculate memory usage and heap capacity from an ELF file.

This tool uses `arm-none-eabi-size` and `arm-none-eabi-readelf` to inspect
the compiled ELF and report memory usage statistics.

Example:
  python Tools/pico_memory_usage.py build/test_executable.elf --min-heap-bytes 2048
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from typing import Dict, Iterable, Optional, Tuple


def run_tool(cmd: Iterable[str]) -> str:
    try:
        result = subprocess.run(list(cmd), capture_output=True, text=True, check=True)
    except FileNotFoundError:
        raise RuntimeError(f"Required tool not found: {cmd[0]}")
    except subprocess.CalledProcessError as exc:
        raise RuntimeError(f"Tool {cmd[0]} failed:\n{exc.stderr.strip()}")
    return result.stdout


def parse_size_output(output: str) -> Dict[str, int]:
    lines = [line.strip() for line in output.splitlines() if line.strip()]
    if len(lines) < 2:
        raise ValueError("Unexpected size output")

    header = lines[0].split()
    values = lines[1].split()
    if len(values) < len(header):
        raise ValueError("Could not parse size output")

    parsed: Dict[str, int] = {}
    for name, value in zip(header, values):
        if name in {"text", "data", "bss", "dec"}:
            parsed[name] = int(value)
    return parsed


def parse_symbol_table(output: str) -> Dict[str, int]:
    symbols: Dict[str, int] = {}
    pattern = re.compile(r"^\s*\d+:\s*([0-9a-fA-F]+)\s+\d+\s+\w+\s+\w+\s+\w+\s+\w+\s+(.+)$")
    for line in output.splitlines():
        match = pattern.match(line)
        if not match:
            continue
        value_text, name = match.groups()
        name = name.strip()
        try:
            symbols[name] = int(value_text, 16)
        except ValueError:
            pass
    return symbols


def parse_section_headers(output: str) -> Dict[str, Tuple[int, int]]:
    sections: Dict[str, Tuple[int, int]] = {}
    pattern = re.compile(r"^\s*\[\s*\d+\]\s+(\S+)\s+\S+\s+([0-9a-fA-F]+)\s+\S+\s+([0-9a-fA-F]+)")
    for line in output.splitlines():
        match = pattern.match(line)
        if not match:
            continue
        name, addr, size = match.groups()
        try:
            sections[name] = (int(addr, 16), int(size, 16))
        except ValueError:
            pass
    return sections


def load_size_info(path: str) -> Dict[str, int]:
    for tool in ("arm-none-eabi-size", "size"):
        try:
            output = run_tool((tool, path))
        except RuntimeError:
            continue
        return parse_size_output(output)
    raise RuntimeError("Unable to locate arm-none-eabi-size or size.")


def load_symbol_info(path: str) -> Dict[str, int]:
    for tool in ("readelf", "arm-none-eabi-readelf", "objdump"):
        try:
            if tool == "objdump":
                output = run_tool((tool, "-t", path))
            else:
                output = run_tool((tool, "-Ws", path))
        except RuntimeError:
            continue
        return parse_symbol_table(output)
    raise RuntimeError("Unable to locate readelf, arm-none-eabi-readelf, or objdump.")


def load_section_info(path: str) -> Dict[str, Tuple[int, int]]:
    for tool in ("readelf", "arm-none-eabi-readelf", "objdump"):
        try:
            if tool == "objdump":
                output = run_tool((tool, "-h", path))
            else:
                output = run_tool((tool, "-S", path))
        except RuntimeError:
            continue
        return parse_section_headers(output)
    raise RuntimeError("Unable to locate readelf, arm-none-eabi-readelf, or objdump.")


def format_bytes(value: int) -> str:
    return f"{value} bytes ({value / 1024:.2f} KiB)"


def main() -> int:
    parser = argparse.ArgumentParser(description="Calculate Pico ELF memory usage and heap capacity.")
    parser.add_argument("elf", help="Path to the ARM ELF file.")
    parser.add_argument("--min-heap-bytes", type=int, default=None,
                        help="Require at least this many free heap bytes.")
    args = parser.parse_args()

    if not os.path.isfile(args.elf):
        print(f"Error: ELF file not found: {args.elf}", file=sys.stderr)
        return 2

    size_info = load_size_info(args.elf)
    symbol_info = load_symbol_info(args.elf)
    section_info = load_section_info(args.elf)

    text_size = size_info.get("text", 0)
    data_size = size_info.get("data", 0)
    bss_size = size_info.get("bss", 0)
    total_static_ram = data_size + bss_size

    stack_top = symbol_info.get("__StackTop")
    stack_bottom = symbol_info.get("__StackBottom")
    end_addr = symbol_info.get("__end__") or symbol_info.get("__end")

    heap_section = section_info.get(".heap")
    
    if stack_bottom is not None and end_addr is not None:
        heap_capacity = max(0, stack_bottom - end_addr)
    else:
        heap_capacity = heap_section[1] if heap_section else None

    # Calculate overall RAM usage (assuming typical Pico memory map where RAM starts at 0x20000000)
    ram_start = 0x20000000
    total_ram_capacity = stack_top - ram_start if stack_top else None
    sram_remaining = heap_capacity
    
    total_ram_used = None
    if total_ram_capacity is not None and sram_remaining is not None:
        total_ram_used = total_ram_capacity - sram_remaining

    print("======================================================")
    print(f" Memory Usage Report: {os.path.basename(args.elf)}")
    print("======================================================")
    print("")

    print("[ CODE (.text) ]")
    print(f" Code Size:                {format_bytes(text_size)}")
    print("")

    print("[ DATA ]")
    print(f" Initialized Data (.data): {format_bytes(data_size)}")
    print(f" Uninitialized Data (.bss): {format_bytes(bss_size)}")
    print(f" Total Static Memory:      {format_bytes(total_static_ram)}")
    print("")

    # Calculate and display total memory usage
    total_memory = text_size + data_size + bss_size
    print("[ TOTAL MEMORY USAGE ]")
    print(f" Total:                    {format_bytes(total_memory)}")
    print("")

    if heap_capacity is not None or total_ram_capacity is not None:
        print("[ DYNAMIC MEMORY ]")
        if total_ram_capacity is not None and total_ram_used is not None:
            print(f" Total RAM Capacity:       {format_bytes(total_ram_capacity)}")
            print(f" RAM Used:                 {format_bytes(total_ram_used)}")
            if stack_top is not None and stack_bottom is not None:
                print(f"   +-- Stack:              {format_bytes(stack_top - stack_bottom)}")
            print(f" RAM Remaining (Heap):     {format_bytes(sram_remaining)}")
        elif heap_capacity is not None:
            if heap_section is not None:
                print(f" Heap section:             .heap @ 0x{heap_section[0]:08x} size {format_bytes(heap_section[1])}")
            elif end_addr is not None:
                print(f" Heap start address:       0x{end_addr:08x}")
            print(f" Heap Capacity:            {format_bytes(heap_capacity)}")
        print("")

    if args.min_heap_bytes is not None:
        print("---------------------------------")
        print(f"Minimum required heap:     {format_bytes(args.min_heap_bytes)}")
        if heap_capacity is None:
            print("Error: heap capacity unknown; cannot verify minimum requirement.", file=sys.stderr)
            return 3
        if heap_capacity < args.min_heap_bytes:
            print(f"Error: heap capacity is below required threshold.", file=sys.stderr)
            return 4
        print("Heap capacity meets the minimum requirement.")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
