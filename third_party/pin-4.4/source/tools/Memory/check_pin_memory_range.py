#!/usr/bin/env python3

#
# Copyright (C) 2026-2026 Intel Corporation.
# SPDX-License-Identifier: MIT
#

import sys
import re

PIN_IMAGE_NAMES = (
    'libpincrt.so',
    'pin.ld.so',
    'pincrt4.dll',
    'pinvm.dll',
)

def is_pin_image_line(line):
    line_lower = line.lower()
    return any(name in line_lower for name in PIN_IMAGE_NAMES)

def check_memory_range(memory_map_file, pin_image_range):
    """
    Check if a Pin runtime image is loaded within the specified memory range.
    
    Args:
        memory_map_file: Path to the memory map output file
        pin_image_range: Range in format "0x<start>:0x<end>"
    
    Returns:
        0 if found within range, 1 if not found or outside range
    """
    if pin_image_range == "0:0":
        return 0  # No range specified, test passes
    
    # Parse the range
    try:
        start_str, end_str = pin_image_range.split(':')
        start_addr = int(start_str, 16)
        end_addr = int(end_str, 16)
    except ValueError:
        print(f"Error: Invalid range format '{pin_image_range}'", file=sys.stderr)
        return 1
    
    # Validate that start < end
    if start_addr >= end_addr:
        print(f"Error: Invalid range - start address 0x{start_addr:x} must be less than end address 0x{end_addr:x}", file=sys.stderr)
        return 1
    
    # Read and check the memory map
    try:
        with open(memory_map_file, 'r') as f:
            for line in f:
                line = line.strip()
                
                # Check if this line contains a Pin runtime image.
                if is_pin_image_line(line):
                    # Extract the starting address (before the first '-')
                    addr_match = re.match(r'^(?:0x)?([0-9a-fA-F]+)-', line)
                    if addr_match:
                        addr_str = addr_match.group(1)
                        addr = int(addr_str, 16)
                        
                        # Check if address is within range
                        filename = line.split()[-1] if line.split() else "unknown"
                        if start_addr <= addr < end_addr:
                            status = "within"
                            result = 0
                        else:
                            status = "outside"
                            result = 1
                        
                        output = sys.stdout if result == 0 else sys.stderr
                        print(f"Found {filename} at 0x{addr:x} {status} range 0x{start_addr:x}:0x{end_addr:x}", file=output)
                        return result
        
        print("No Pin runtime image found in memory map", file=sys.stderr)
        return 1
        
    except FileNotFoundError:
        print(f"Error: Memory map file '{memory_map_file}' not found", file=sys.stderr)
        return 1
    except Exception as e:
        print(f"Error reading memory map: {e}", file=sys.stderr)
        return 1

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: check_pin_memory_range.py <memory_map_file> <pin_image_range>", file=sys.stderr)
        sys.exit(1)
    
    memory_map_file = sys.argv[1]
    pin_image_range = sys.argv[2]
    
    sys.exit(check_memory_range(memory_map_file, pin_image_range))